#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "ycnet/rudp.hpp"

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
    ycnet::packet::udp::ack_header ack;
    ack.use_ack = true;
    ack.is_ack_packet = false;
    ack.counter = 70001;
    const auto raw = ack.to_ack();

    ycnet::packet::udp::ack_header decoded;
    decoded.load(raw);
    check(decoded.use_ack && !decoded.is_ack_packet && decoded.counter == 70001,
          "ACK header roundtrip keeps flags and full 32-bit sequence");
}

void test_sequence_order_across_midpoint_and_wrap() {
    check(ycnet::rudp::is_later_seq(33, 32), "seq=33 is later than seq=32");
    check(!ycnet::rudp::is_later_seq(31, 32), "seq=31 is older than seq=32");
    check(ycnet::rudp::is_later_seq(64, 63), "seq=64 is later than seq=63 across a slot cycle");
    check(!ycnet::rudp::is_later_seq(0, 64), "old seq=0 stays older than seq=64");
}

void test_packet_validation_rejects_malformed_ack() {
    char malformed_ack[1] = {static_cast<char>(0x40)}; // is_ack_packet=1, use_ack=0
    check(!ycnet::packet::validate_packet(malformed_ack, sizeof(malformed_ack)),
          "ACK packet requires both ACK and sequence-use flags");

    ycnet::packet::udp::ack_header ack;
    ack.use_ack = true;
    ack.is_ack_packet = true;
    ack.counter = 7;
    auto valid_ack = ack.to_ack();
    check(ycnet::packet::validate_packet(valid_ack.data(), valid_ack.size()),
          "Well-formed full-sequence ACK is accepted");
    check(!ycnet::packet::validate_packet(valid_ack.data(), valid_ack.size() - 1),
          "Truncated sequence header is rejected");
}

void test_reliable_send_and_ack_complete() {
    ycnet::rudp::rudp_buffers buffers(1);
    std::vector<int> resend;
    char payload[] = {'A', 'B', 'C'};
    const int seq = 5;
    const int slot = ycnet::rudp::ready_to_send(
        buffers.send_buffer, resend, payload, static_cast<int>(sizeof(payload)), true, seq);

    check(slot == seq, "Reliable send uses sequence slot");
    check(buffers.send_buffer[seq].is_used, "Reliable send keeps packet until ACK");
    check(resend.size() == 1 && resend.front() == seq, "Reliable send registers resend candidate");
    check(ycnet::rudp::get_seq(buffers.send_buffer[seq].data) == seq, "Reliable packet encodes sequence");

    ycnet::rudp::rto_state rto;
    buffers.send_buffer[seq].timestamp = 1000;
    const int completed = ycnet::rudp::set_send_complete(buffers.send_buffer, resend, rto, seq, 1080);
    check(completed == seq, "ACK completion returns sequence");
    check(!buffers.send_buffer[seq].is_used && resend.empty(), "ACK completion releases send slot");
    check(rto.has_sample, "First ACK records an RTT sample");
    check(rto.rto_ms == 240, "First 80ms RTT sample produces 240ms RTO");
}

void test_rto_smoothing_and_karn_rule() {
    ycnet::rudp::rudp_buffers buffers(1);
    std::vector<int> resend;
    char payload[] = {'R'};
    ycnet::rudp::rto_state rto;

    ycnet::rudp::ready_to_send(buffers.send_buffer, resend, payload, 1, true, 1);
    buffers.send_buffer[1].timestamp = 1000;
    ycnet::rudp::set_send_complete(buffers.send_buffer, resend, rto, 1, 1080);

    ycnet::rudp::ready_to_send(buffers.send_buffer, resend, payload, 1, true, 2);
    buffers.send_buffer[2].timestamp = 2000;
    ycnet::rudp::set_send_complete(buffers.send_buffer, resend, rto, 2, 2100);
    check(rto.rto_ms == 223, "RTO smooths successive 80ms and 100ms RTT samples");

    const int before_retransmit_ack = rto.rto_ms;
    ycnet::rudp::ready_to_send(buffers.send_buffer, resend, payload, 1, true, 3);
    buffers.send_buffer[3].timestamp = 3000;
    buffers.send_buffer[3].is_resend_packet = true;
    ycnet::rudp::set_send_complete(buffers.send_buffer, resend, rto, 3, 3400);
    check(rto.rto_ms == before_retransmit_ack,
          "ACK after retransmission does not contaminate RTT estimate");
}

void test_resend_backoff_and_timeout() {
    ycnet::rudp::rudp_buffers buffers(1);
    std::vector<int> resend;
    char payload[] = {'X'};
    const int seq = 3;
    ycnet::rudp::ready_to_send(buffers.send_buffer, resend, payload, 1, true, seq);

    ycnet::rudp::rto_state rto;
    buffers.send_buffer[seq].timestamp = 1000;
    auto due = ycnet::rudp::get_resend_packets(buffers.send_buffer, resend, rto, 1000, 1101);
    check(due.size() == 1 && due.front() == seq, "Expired reliable packet is selected for resend");
    check(buffers.send_buffer[seq].retry_interval_ms == 200, "Timeout applies per-packet 2x backoff");
    check(buffers.send_buffer[seq].timestamp == 1101, "Resend refreshes packet send timestamp");

    rto.rto_ms = 800;
    buffers.send_buffer[seq].timestamp = 2000;
    buffers.send_buffer[seq].retry_interval_ms = 800;
    auto timed_out = ycnet::rudp::get_resend_packets(buffers.send_buffer, resend, rto, 1000, 2801);
    check(timed_out.size() == 1 && buffers.send_buffer[seq].retry_interval_ms == 1000, "RTO is capped without declaring connection failure");
}

void test_ordered_delivery_waits_for_gap() {
    ycnet::rudp::rudp_buffers buffers(1);
    std::vector<ycnet::rudp::receive_packet_buffer> no_ack;

    auto make_packet = [](int seq, char value) {
        std::vector<char> bytes(transport_header_size + 1);
        ycnet::packet::udp::ack_header ack;
        ack.use_ack = true;
        ack.is_ack_packet = false;
        ack.counter = seq;
        const auto header = ack.to_ack();
        std::copy(header.begin(), header.end(), bytes.begin());
        bytes[transport_header_size] = value;
        return bytes;
    };

    const auto p1 = make_packet(1, 'B');
    const auto p0 = make_packet(0, 'A');

    check(ycnet::rudp::push_packet(buffers.pkt_buffer, 0, 1, p1.data(), p1.size()) >= 0,
          "Out-of-order seq=1 packet is buffered");
    auto [s1, e1] = ycnet::rudp::get_read_range(
        buffers.pkt_buffer, buffers.receive_buffer, no_ack, 1, 0);
    check(s1 == 0 && e1 == 0, "Delivery waits while seq=0 is missing");

    check(ycnet::rudp::push_packet(buffers.pkt_buffer, 0, 1, p0.data(), p0.size()) >= 0,
          "Missing seq=0 packet is buffered");
    auto [s2, e2] = ycnet::rudp::get_read_range(
        buffers.pkt_buffer, buffers.receive_buffer, no_ack, 1, 0);
    check(s2 == 0 && e2 == 2, "Buffered seq=0 and seq=1 become contiguous");
    check(buffers.receive_buffer[0].data[0] == 'A' && buffers.receive_buffer[1].data[0] == 'B',
          "Ordered delivery preserves payload order");
}

void test_stable_rtt_timer_granularity() {
    ycnet::rudp::rto_state rto;
    for (int i = 0; i < 200; ++i) ycnet::rudp::update_rto(rto, 80);
    check(rto.rto_ms == 81, "Stable RTT retains clock granularity instead of expiring at ACK arrival");
}

void test_sequence_wrap() {
    check(ycnet::rudp::get_next_seq(reliable_window_size - 1) == reliable_window_size, "Sequence identity does not wrap at slot count");
    check(ycnet::rudp::sequence_slot(reliable_window_size + 2) == 2, "Sequence normalization wraps modulo 64");
    check(ycnet::rudp::get_next_seq(max_packet_sequence) == max_packet_sequence + 1,
          "Wire sequence exhaustion produces a non-encodable sentinel");
}
}

int main() {
    test_ack_header_roundtrip();
    test_sequence_order_across_midpoint_and_wrap();
    test_packet_validation_rejects_malformed_ack();
    test_reliable_send_and_ack_complete();
    test_rto_smoothing_and_karn_rule();
    test_resend_backoff_and_timeout();
    test_ordered_delivery_waits_for_gap();
    test_stable_rtt_timer_granularity();
    test_sequence_wrap();

    std::cout << "failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

