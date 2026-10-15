// ReSharper disable IdentifierTypo
// ReSharper disable CppClangTidyBugproneImplicitWideningOfMultiplicationResult
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "packet.hpp"

namespace ycnet::rudp
{
    struct packet_buffer {
        char data[max_packet_size] {};
        packet_size_type len {};
        volatile bool is_used {};
        packet_sequence_type sequence{-1};
    };
    struct receive_packet_buffer {
        char data[max_packet_size] {};
        packet_size_type len {};
    };
    struct send_packet_buffer : packet_buffer {
        int64_t timestamp{};
        bool is_resend_packet{};
        int retry_interval_ms{};
    };

    struct rto_state {
        bool has_sample{};
        double srtt_ms{};
        double rttvar_ms{};
        int rto_ms{100};

        void reset() {
            has_sample = false;
            srtt_ms = 0.0;
            rttvar_ms = 0.0;
            rto_ms = 100;
        }
    };
    struct rudp_buffers {
        std::vector<packet_buffer> pkt_buffer;
        std::vector<receive_packet_buffer> receive_buffer;
        std::vector<send_packet_buffer> send_buffer;

        explicit rudp_buffers(const int io_thread_count)
                        : pkt_buffer(reliable_window_size * io_thread_count * 2)
                        , receive_buffer(reliable_window_size * io_thread_count * 2)
                        , send_buffer(reliable_window_size + 1) {
        }

        void clear() {
            for (auto &pkt : pkt_buffer) {
                pkt.is_used = false;
            }
            for (auto &pkt : receive_buffer) {
                pkt.len = 0;
            }
            for (auto &pkt : send_buffer) {
                pkt.is_used = false;
                pkt.is_resend_packet = false;
            }
        }
    };
    
    [[nodiscard]] inline int64_t get_timestamp() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    inline void update_rto(rto_state& state, const int sample_ms) {
        const auto sample = static_cast<double>((std::max)(sample_ms, 1));
        if (!state.has_sample) {
            state.srtt_ms = sample;
            state.rttvar_ms = sample / 2.0;
            state.has_sample = true;
        }
        else {
            const auto error = std::abs(state.srtt_ms - sample);
            state.rttvar_ms = 0.75 * state.rttvar_ms + 0.25 * error;
            state.srtt_ms = 0.875 * state.srtt_ms + 0.125 * sample;
        }

        state.rto_ms = (std::min)(1000, (std::max)(
            10,
            static_cast<int>(std::lround(state.srtt_ms + (std::max)(1.0, 4.0 * state.rttvar_ms)))));
    }
    
    // 받아야할 패킷 번호 보다 뒷 번호 인지 확인 하는 함수.
    inline bool is_later_seq(packet_sequence_type seq1, packet_sequence_type seq2) {
        return seq1 > seq2;
    }
    //패킷에서 seq를 뽑아내는 함수
    inline packet_sequence_type get_seq(const char* buf) {
        ycnet::packet::udp::ack_header ack;
        ack.load(buf);
        return ack.use_ack ? ack.counter : -1;
    }
    //ack처리용 패킷인지를 확인 하는 함수
    inline bool is_ack_packet(const char* buf) {
        ycnet::packet::udp::ack_header ack;
        ack.load(buf);
        return ack.is_ack_packet;
    }
    inline packet_sequence_type get_next_seq(const packet_sequence_type seq) {
        // max_packet_sequence + 1 is an exhausted-session sentinel, never encoded.
        return seq <= max_packet_sequence ? seq + 1 : seq;
    }
    inline int sequence_slot(const packet_sequence_type seq) {
        return static_cast<int>(seq % reliable_window_size);
    }
    packet_sequence_type each_seq(const packet_sequence_type start, const packet_sequence_type end, auto f) {
        if (start == end) return end;
        for(auto i = start; i < end; ++i) f(i);
        return end;
    }
    
    /**
     * \brief 패킷 버퍼에 패킷을 넣는 함수
     * \param pkt_buf 패킷이 담길 버퍼 buf -> [t1 | t2... || t1(no ack) | t2(no ack)...]
     * \param thread_id 현재 스레드의 id
     * \param thread_cnt_max 스레드의 최대 개수
     * \param buf 패킷의 데이터
     * \param len 패킷의 길이
     * \return 성공시 pushed index, 실패시 -1
     */
    inline int push_packet(
        std::vector<packet_buffer>& pkt_buf,
        const int thread_id,
        const int thread_cnt_max,
        const char* buf,
        const size_t len
        ) {
        const auto seq = get_seq(buf);
        int idx = -1;
        if(seq == -1) {
            const auto s = reliable_window_size * thread_cnt_max + reliable_window_size * thread_id;
            for(int i = 0; i < reliable_window_size; ++i) {
                if(!pkt_buf[i + s].is_used) {
                    idx = i + s;
                    break;
                }
            }
        } else {
            idx = thread_id * reliable_window_size + sequence_slot(seq);
        }
        if (idx < 0 || len < transport_header_size || len > max_packet_size) return -1;
        auto& packet = pkt_buf[idx];
        if(packet.is_used) return -1;
        std::copy_n(buf + transport_header_size, len - transport_header_size, packet.data);
        packet.len = static_cast<packet_size_type>(len - transport_header_size);
        packet.sequence = seq;
        packet.is_used = true;
        
        return idx;
    }
    
    inline packet_buffer* find_seq_of_packets(
        std::vector<packet_buffer>& pkt_buf,
        const int thread_id,
        const packet_sequence_type seq
        ) {
        auto& packet = pkt_buf[thread_id * reliable_window_size + sequence_slot(seq)];
        return packet.is_used && packet.sequence == seq ? &packet : nullptr;
    }
    
    /**
     * \brief 패킷을 읽어 올 범위를 가져오는 함수
     * 이 함수는 보호 되어야 한다.
     * 아니면 하나의 스레드에서만 실행된 다는 것이 보장되어야 한다.
     * \param pkt_buf 사용할 패킷 버퍼
     * \param recv_buf [OUT] 읽어온 패킷을 담을 버퍼
     * \param no_ack_read_buf [OUT] ack 패킷이 아닌 패킷을 담을 버퍼
     * \param thread_cnt_max 스레드의 최대 개수
     * \param end 읽어온 패킷의 마지막 위치
     * \return 읽을 패킷의 범위. 실패시 (-1, -1) 반환. 실패시 buffer를 비워서 패킷을 버린다.
     */
    inline std::pair<packet_sequence_type, packet_sequence_type> get_read_range(
        std::vector<packet_buffer>& pkt_buf,
        std::vector<receive_packet_buffer>& recv_buf,
        std::vector<receive_packet_buffer>& no_ack_read_buf,
        const int thread_cnt_max,
        packet_sequence_type end
        ) {
        const auto start = end;
        int cnt = 0;
        for(auto i = start; i < start + reliable_window_size; ++i) {
            packet_buffer* ptr = nullptr;
            for(int thread_id = 0; thread_id < thread_cnt_max; ++thread_id) {
                ptr = find_seq_of_packets(pkt_buf, thread_id, i);
                if(ptr) break;
            }
            if(ptr == nullptr) {
                end = i;
                break;
            }
            const int idx = sequence_slot(i);
            std::copy_n(ptr->data, ptr->len, recv_buf[idx].data);
            recv_buf[idx].len = ptr->len;
            ptr->is_used = false;
            end = i + 1;
            cnt++;
        }
        
        for(int thread_id = 0; thread_id < thread_cnt_max; ++thread_id) {
            for(int i = 0; i < reliable_window_size; ++i) {
                auto& [data, len, is_used, sequence] = pkt_buf[thread_cnt_max * reliable_window_size + thread_id * reliable_window_size + i];
                if(!is_used) break;
                no_ack_read_buf.emplace_back();
                std::copy_n(data, len, no_ack_read_buf.back().data);
                no_ack_read_buf.back().len = len;
                is_used = false;
            }
        }
        if(cnt > reliable_window_size) return {-1, -1};
        return { start, end };
    }

    /**
     * \brief packet에 ack header를 붙입니다.
     * 함수를 호출 한 뒤 바로 send 해야 합니다.
     * \param send_buf [OUT] ack 패킷을 담을 버퍼
     * \param buf 패킷 몸체
     * \param len 패킷의 길이
     * \param use_ack ack를 사용할지 여부
     * \param seq ack 패킷의 seq
     * \return ack 패킷의 seq, 버퍼가 전부 차있을 경우 -1, ack를 사용하지 않는 패킷일 경우 reliable_window_size
     */
    inline int ready_to_send(
        std::vector<send_packet_buffer>& send_buf,
        std::vector<int>& resend_idx_buf,
        char* buf,
        const int len,
        const bool use_ack,
        const packet_sequence_type seq,
        const int64_t now = get_timestamp(),
        const std::uint64_t source_session = 0,
        const std::uint64_t destination_session = 0,
        const int initial_rto_ms = 100
        ){
        if (len < 0 || len > max_packet_size - transport_header_size) return -1;
        if (use_ack) {
            if (seq < 0 || seq > max_packet_sequence || send_buf[sequence_slot(seq)].is_used) return -1;
            for (const int slot : resend_idx_buf) {
                const auto& pending = send_buf[slot];
                if (pending.is_used && (seq < pending.sequence || seq - pending.sequence >= reliable_window_size))
                    return -1;
            }
        }

        ycnet::packet::udp::ack_header ack;
        ack.use_ack = use_ack;
        ack.is_ack_packet = false;
        ack.counter = seq;
        ack.source_session = source_session;
        ack.destination_session = destination_session;
        const int s = use_ack ? sequence_slot(seq) : reliable_window_size;
        const auto header = ack.to_ack();
        std::copy(header.begin(), header.end(), send_buf[s].data);
        std::copy_n(buf, len, send_buf[s].data + transport_header_size);
        send_buf[s].timestamp = now;
        send_buf[s].retry_interval_ms = initial_rto_ms;
        send_buf[s].len = static_cast<packet_size_type>(len + transport_header_size);
        send_buf[s].is_used = use_ack;
        send_buf[s].sequence = use_ack ? seq : -1;
        send_buf[s].is_resend_packet = false;
        if(use_ack) resend_idx_buf.push_back(s);
        return s;
    }

    /**
     * \brief ack을 받았을 때 호출합니다.
     * \param send_buf [OUT] send 완료 처리를 위한 버퍼
     * \param rto [OUT] ACK sample로 갱신되는 RTO 상태
     * \param seq ack 패킷의 seq
     * \return send 완료 처리한 버퍼의 index, 실패시 -1
     */
    inline int set_send_complete(
        std::vector<send_packet_buffer>& send_buf,
        std::vector<int>& resend_idx_buf,
        rto_state& rto,
        const packet_sequence_type seq,
        const int64_t now = get_timestamp()
        ) {
        if (seq < 0 || seq > max_packet_sequence) return -1;
        const int slot = sequence_slot(seq);
        auto& packet = send_buf[slot];
        if(!packet.is_used || packet.sequence != seq) return -1;
        if (!packet.is_resend_packet) {
            const auto sample = static_cast<int>((std::max)(int64_t{1}, now - packet.timestamp));
            update_rto(rto, sample);
        }

        const auto& it = std::find(resend_idx_buf.begin(), resend_idx_buf.end(), slot);
        if(it != resend_idx_buf.end())
            resend_idx_buf.erase(it);
        packet.is_used = false;
        packet.is_resend_packet = false;
        return slot;
    }

    /**
     * \brief 재전송이 필요한 패킷을 찾습니다.
     * \param send_buf [OUT] resend가 필요한지 검사하는 버퍼
     * \param rto [OUT] timeout 시 backoff되는 RTO 상태
     * \param timeout Maximum retransmission interval (ms), not a connection deadline
     * \param now 테스트와 호출 시점 고정을 위한 현재 시각
     * \return resend가 필요한 패킷의 seq 목록
     */
    inline std::vector<int> get_resend_packets(
        std::vector<send_packet_buffer>& send_buf,
        std::vector<int>& resend_idx_buf,
        rto_state& rto,
        const int max_rto_ms,
        const int64_t now = get_timestamp()
        ) {
        std::vector<int> result;
        for (const int i : resend_idx_buf) {
            auto& pkt = send_buf[i];
            if (pkt.is_used && now - pkt.timestamp >= pkt.retry_interval_ms) {
                result.push_back(i);
                pkt.is_resend_packet = true;
                pkt.timestamp = now;
                pkt.retry_interval_ms = static_cast<int>((std::min)(
                    static_cast<int64_t>(max_rto_ms), static_cast<int64_t>(pkt.retry_interval_ms) * 2));
            }
        }
        return result;
    }
}
