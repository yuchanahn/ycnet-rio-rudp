#include "ycnet/ycnet.hpp"
#include <deque>
#include <iostream>
#include <vector>

namespace {
struct message { static constexpr int packet_id = 93; static constexpr bool is_reliable = true; int value; };
int failures{};
void check(bool ok, const char* name) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    failures += !ok;
}
using bytes = std::vector<char>;
using queue = std::deque<bytes>;
void drain(queue& wire, ycnet::rudp_handler& peer) {
    queue pending;
    pending.swap(wire);
    for (auto& packet : pending) peer.on_receive(packet.data(), packet.size());
}
void test_backpressure_and_progress_failure() {
    int64_t now{};
    queue wire;
    ycnet::rudp_handler sender([](auto){}, [&](char* data, size_t n) { wire.emplace_back(data, data+n); },
        [&]{ return now; }, {101, 202});
    sender.set_reliability_options({1000, 10000, 10000});
    int callback_count{};
    ycnet::delivery_failure failure{};
    sender.set_failure_callback([&](auto event) { ++callback_count; failure = event; });
    message packet{1};
    bool accepted = true;
    for (int i=0; i<64; ++i) accepted &= sender.try_send(packet) == ycnet::send_result::accepted;
    check(accepted && sender.try_send(packet) == ycnet::send_result::would_block
        && sender.metrics().pending_reliable == 64 && wire.size() == 64,
        "65th send reports would_block without evicting pending data");
    auto ack = ycnet::make_ack_packet(0, {202, 101});
    now = 50;
    sender.on_receive(ack.data(), ack.size());
    check(sender.try_send(packet) == ycnet::send_result::accepted && sender.metrics().pending_reliable == 64,
        "matching ACK makes room for a retried send");
    for (now=100; now<=9950; now+=50) sender.process_retransmissions();
    check(sender.state() == ycnet::connection_state::connected && callback_count == 0
        && sender.metrics().retransmissions > 0 && sender.metrics().pending_reliable == 64,
        "RTO backoff does not discard data or terminate before the progress deadline");
    // Duplicate ACKs must not extend the delivery deadline.
    now=10049;
    sender.on_receive(ack.data(), ack.size());
    sender.process_retransmissions();
    check(callback_count == 0, "duplicate ACK does not complete another packet");
    now=10050;
    sender.process_retransmissions();
    check(callback_count == 1 && failure.reason == ycnet::failure_reason::progress_timeout
        && failure.unconfirmed_messages == 64 && sender.state() == ycnet::connection_state::closed
        && sender.metrics().pending_reliable == 0,
        "failure reports all unconfirmed messages once before connection reuse");
    auto late_ack = ycnet::make_ack_packet(1, {202, 101});
    sender.on_receive(late_ack.data(), late_ack.size());
    sender.process_retransmissions();
    check(callback_count == 1 && sender.try_send(packet) == ycnet::send_result::closed
        && sender.metrics().acks_received == 1, "closed connection rejects late ACKs and new sends");
}
void test_session_replay() {
    int64_t now{};
    queue old_wire, old_acks;
    ycnet::rudp_handler old_sender([](auto){}, [&](char* p,size_t n){old_wire.emplace_back(p,p+n);}, [&]{return now;}, {101,202});
    ycnet::rudp_handler old_receiver([](auto){}, [&](char* p,size_t n){old_acks.emplace_back(p,p+n);}, [&]{return now;}, {202,101});
    int deliveries{};
    old_receiver.set_receive_callback([&](auto){++deliveries;});
    message packet{7};
    old_sender.send(packet);
    auto old_data = old_wire.front();
    drain(old_wire,old_receiver);
    auto old_ack = old_acks.front();
    queue new_wire, new_acks;
    // Deliberately reuse client identity and packet 0; the server identity changes.
    ycnet::rudp_handler sender([](auto){}, [&](char* p,size_t n){new_wire.emplace_back(p,p+n);}, [&]{return now;}, {101,404});
    ycnet::rudp_handler receiver([](auto){}, [&](char* p,size_t n){new_acks.emplace_back(p,p+n);}, [&]{return now;}, {404,101});
    receiver.set_receive_callback([&](auto){++deliveries;});
    sender.send(packet);
    sender.on_receive(old_ack.data(),old_ack.size());
    receiver.on_receive(old_data.data(),old_data.size());
    check(sender.metrics().pending_reliable == 1 && sender.metrics().acks_received == 0 && deliveries == 1,
        "old session data and ACK cannot affect a new connection's packet zero");
    drain(new_wire,receiver); drain(new_acks,sender);
    check(deliveries == 2 && sender.metrics().pending_reliable == 0,
        "current session succeeds after rejecting both old packet types");
}
void test_handshake_loss(int drop_control) {
    int64_t now{};
    queue forward, reverse;
    bool dropped{};
    auto enqueue = [&](queue& q, char* p, size_t n) {
        ycnet::packet::udp::ack_header header;
        header.load(p);
        if (!dropped && static_cast<int>(header.control) == drop_control) { dropped=true; return; }
        q.emplace_back(p,p+n);
    };
    ycnet::rudp_handler sender([](auto){}, [&](char* p,size_t n){enqueue(forward,p,n);}, [&]{return now;});
    ycnet::rudp_handler receiver([](auto){}, [&](char* p,size_t n){enqueue(reverse,p,n);}, [&]{return now;});
    int delivered{}, sender_connected{}, receiver_connected{};
    receiver.set_receive_callback([&](auto){++delivered;});
    sender.set_connect_callback([&]{++sender_connected;});
    receiver.set_connect_callback([&]{++receiver_connected;});
    message packet{8};
    check(sender.try_send(packet) == ycnet::send_result::accepted && delivered == 0,
        "reliable message is retained during handshake");
    for (now=0;now<2000 && sender.metrics().pending_reliable;now+=10) {
        drain(forward,receiver); drain(reverse,sender);
        sender.process_retransmissions(); receiver.process_retransmissions();
    }
    check(dropped && delivered == 1 && sender_connected == 1 && receiver_connected == 1
        && sender.metrics().pending_reliable == 0 && sender.session().local == receiver.session().remote
        && sender.session().remote == receiver.session().local,
        "HELLO/CHALLENGE/CONFIRM loss recovers with one ordered delivery");
}
void test_handshake_timeout() {
    int64_t now{};
    ycnet::rudp_handler sender([](auto){}, [](char*,size_t){}, [&]{return now;});
    sender.set_reliability_options({1000,10000,500});
    int count{};
    sender.set_failure_callback([&](auto event) {
        if (event.reason == ycnet::failure_reason::handshake_timeout && event.unconfirmed_messages == 1) ++count;
    });
    message packet{1}; sender.send(packet);
    now=499; sender.process_retransmissions();
    check(sender.state() == ycnet::connection_state::connecting, "handshake waits until its own deadline");
    now=500; sender.process_retransmissions(); sender.process_retransmissions();
    check(count == 1 && sender.state() == ycnet::connection_state::closed,
        "unreachable peer causes explicit handshake failure once");
}
void test_ack_blackhole_delivery_is_unknown() {
    int64_t now{};
    queue wire;
    int delivered{}, unknown{};
    ycnet::rudp_handler sender([](auto){}, [&](char* p,size_t n){wire.emplace_back(p,p+n);}, [&]{return now;}, {101,202});
    ycnet::rudp_handler receiver([](auto){}, [](char*,size_t){}, [&]{return now;}, {202,101});
    sender.set_reliability_options({1000,500,500});
    sender.set_failure_callback([&](auto event){unknown=static_cast<int>(event.unconfirmed_messages);});
    receiver.set_receive_callback([&](auto){++delivered;});
    message packet{1}; sender.send(packet);
    for(now=0;now<=500;now+=10) { drain(wire,receiver); sender.process_retransmissions(); }
    check(delivered == 1 && unknown == 1 && sender.state() == ycnet::connection_state::closed,
        "ACK blackhole reports unknown delivery even though receiver delivered once");
}
void test_receive_gap_deadline_and_setup_bound() {
    int64_t now{};
    queue wire;
    ycnet::rudp_handler sender([](auto){}, [&](char* p,size_t n){wire.emplace_back(p,p+n);}, [&]{return now;}, {101,202});
    ycnet::rudp_handler receiver([](auto){}, [](char*,size_t){}, [&]{return now;}, {202,101});
    receiver.set_reliability_options({1000,500,500});
    int failures_seen{}, delivered{};
    receiver.set_failure_callback([&](auto event){if(event.reason == ycnet::failure_reason::progress_timeout) ++failures_seen;});
    receiver.set_receive_callback([&](auto){++delivered;});
    message packet{1}; sender.send(packet); sender.send(packet);
    receiver.on_receive(wire.back().data(),wire.back().size());
    now=500; receiver.process_retransmissions();
    check(failures_seen == 1 && delivered == 0 && receiver.state() == ycnet::connection_state::closed,
        "receiver with an unresolved ordered gap also terminates explicitly");
    ycnet::rudp_handler pending([](auto){}, [](char*,size_t){}, [&]{return now;});
    bool accepted=true;
    for(int i=0;i<64;++i) accepted &= pending.try_send(packet)==ycnet::send_result::accepted;
    check(accepted && pending.try_send(packet)==ycnet::send_result::would_block
        && pending.metrics().pending_reliable==64, "connection setup also obeys the 64-message memory bound");
}

void test_incoming_traffic_cannot_mask_ack_stall() {
    int64_t now{};
    queue outgoing, incoming;
    ycnet::rudp_handler sender([](auto){}, [&](char* p,size_t n){outgoing.emplace_back(p,p+n);}, [&]{return now;}, {101,202});
    ycnet::rudp_handler peer([](auto){}, [&](char* p,size_t n){incoming.emplace_back(p,p+n);}, [&]{return now;}, {202,101});
    sender.set_receive_callback([](auto){});
    sender.set_reliability_options({1000,500,500});
    message packet{1}; sender.send(packet);
    for (now=100;now<=500;now+=100) {
        peer.send(packet);
        drain(incoming,sender);
        sender.process_retransmissions();
    }
    check(sender.state() == ycnet::connection_state::closed && sender.metrics().acks_received == 0,
        "incoming deliveries cannot extend the opposite direction's stalled ACK deadline");
}

}
int main() {
    test_backpressure_and_progress_failure();
    test_session_replay();
    for(int control=1;control<=3;++control) test_handshake_loss(control);
    test_handshake_timeout();
    test_ack_blackhole_delivery_is_unknown();
    test_receive_gap_deadline_and_setup_bound();
    test_incoming_traffic_cannot_mask_ack_stall();
    return failures ? 1 : 0;
}
