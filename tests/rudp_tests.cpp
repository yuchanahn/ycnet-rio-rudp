#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "yc_rudp.hpp"

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << std::endl;
    } else {
        std::cout << "[FAIL] " << name << std::endl;
        ++failures;
    }
}

void test_ack_header_roundtrip() {
    yc_pack::udp::convert_ack ack;
    ack.use_ack = true;
    ack.is_ack_packet = false;
    ack.counter = 37;
    const auto raw = ack.to_ack();

    yc_pack::udp::convert_ack decoded;
    decoded.load(raw);
    check(decoded.use_ack && !decoded.is_ack_packet && decoded.counter == 37,
          "ACK header roundtrip keeps flags and 6-bit sequence");
}

void test_reliable_send_and_ack_complete() {
    yc_rudp::rudp_buffer_t buffers(1);
    std::vector<int> resend;
    char payload[] = {'A', 'B', 'C'};
    const int seq = 5;
    const int slot = yc_rudp::ready_to_send(
        buffers.send_buffer, resend, payload, static_cast<int>(sizeof(payload)), true, seq);

    check(slot == seq, "Reliable send uses sequence slot");
    check(buffers.send_buffer[seq].is_used, "Reliable send keeps packet until ACK");
    check(resend.size() == 1 && resend.front() == seq, "Reliable send registers resend candidate");
    check(yc_rudp::get_seq(buffers.send_buffer[seq].data) == seq, "Reliable packet encodes sequence");

    int rtt = 100;
    const int completed = yc_rudp::set_send_complete(buffers.send_buffer, resend, rtt, seq);
    check(completed == seq, "ACK completion returns sequence");
    check(!buffers.send_buffer[seq].is_used && resend.empty(), "ACK completion releases send slot");
    check(rtt == 100, "ACK completion currently leaves RTT unchanged");
}

void test_resend_backoff_and_timeout() {
    yc_rudp::rudp_buffer_t buffers(1);
    std::vector<int> resend;
    char payload[] = {'X'};
    const int seq = 3;
    yc_rudp::ready_to_send(buffers.send_buffer, resend, payload, 1, true, seq);

    int rtt = 100;
    buffers.send_buffer[seq].timestamp = yc_rudp::get_timestamp() - 101;
    auto due = yc_rudp::get_resend_packets(buffers.send_buffer, resend, rtt, 1000);
    check(due.size() == 1 && due.front() == seq, "Expired reliable packet is selected for resend");
    check(rtt == 150, "Resend applies 1.5x timeout backoff");

    rtt = 900;
    buffers.send_buffer[seq].timestamp = yc_rudp::get_timestamp() - 901;
    auto timed_out = yc_rudp::get_resend_packets(buffers.send_buffer, resend, rtt, 1000);
    check(timed_out.empty() && rtt == -1, "Backoff beyond timeout marks connection timed out");
}

void test_ordered_delivery_waits_for_gap() {
    yc_rudp::rudp_buffer_t buffers(1);
    std::vector<yc_rudp::receive_packet_raw> no_ack;

    auto make_packet = [](int seq, char value) {
        std::vector<char> bytes(2);
        yc_pack::udp::convert_ack ack;
        ack.use_ack = true;
        ack.is_ack_packet = false;
        ack.counter = seq;
        bytes[0] = static_cast<char>(ack.to_ack());
        bytes[1] = value;
        return bytes;
    };

    const auto p1 = make_packet(1, 'B');
    const auto p0 = make_packet(0, 'A');

    check(yc_rudp::push_packet(buffers.pkt_buffer, 0, 1, p1.data(), p1.size()) >= 0,
          "Out-of-order seq=1 packet is buffered");
    auto [s1, e1] = yc_rudp::get_read_range(
        buffers.pkt_buffer, buffers.receive_buffer, no_ack, 1, 0);
    check(s1 == 0 && e1 == 0, "Delivery waits while seq=0 is missing");

    check(yc_rudp::push_packet(buffers.pkt_buffer, 0, 1, p0.data(), p0.size()) >= 0,
          "Missing seq=0 packet is buffered");
    auto [s2, e2] = yc_rudp::get_read_range(
        buffers.pkt_buffer, buffers.receive_buffer, no_ack, 1, 0);
    check(s2 == 0 && e2 == 2, "Buffered seq=0 and seq=1 become contiguous");
    check(buffers.receive_buffer[0].data[0] == 'A' && buffers.receive_buffer[1].data[0] == 'B',
          "Ordered delivery preserves payload order");
}

void test_sequence_wrap() {
    check(yc_rudp::get_next_seq(ACK_COUNTER_MAX - 1) == 0, "Sequence wraps at 64");
    check(yc_rudp::make_seq(ACK_COUNTER_MAX + 2) == 2, "Sequence normalization wraps modulo 64");
}
}

int main() {
    test_ack_header_roundtrip();
    test_reliable_send_and_ack_complete();
    test_resend_backoff_and_timeout();
    test_ordered_delivery_waits_for_gap();
    test_sequence_wrap();

    std::cout << "failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

