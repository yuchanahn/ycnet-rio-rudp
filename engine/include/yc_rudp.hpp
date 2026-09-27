// ReSharper disable IdentifierTypo
// ReSharper disable CppClangTidyBugproneImplicitWideningOfMultiplicationResult
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "yc_packet.hpp"

namespace yc_rudp
{
    struct packet_raw {
        char data[PACKET_SIZE_MAX] {};
        packet_size_type len {};
        volatile bool is_used {};
    };
    struct receive_packet_raw {
        char data[PACKET_SIZE_MAX] {};
        packet_size_type len {};
    };
    struct send_packet_raw : packet_raw {
        int64_t timestamp{};
        bool is_resend_packet{};
    };

    struct rto_state {
        bool has_sample{};
        bool timed_out{};
        double srtt_ms{};
        double rttvar_ms{};
        int rto_ms{100};

        void reset() {
            has_sample = false;
            timed_out = false;
            srtt_ms = 0.0;
            rttvar_ms = 0.0;
            rto_ms = 100;
        }
    };
    struct rudp_buffer_t {
        std::vector<packet_raw> pkt_buffer;
        std::vector<receive_packet_raw> receive_buffer;
        std::vector<send_packet_raw> send_buffer;

        explicit rudp_buffer_t(const int io_thread_count)
                        : pkt_buffer(ACK_COUNTER_MAX * io_thread_count * 2)
                        , receive_buffer(ACK_COUNTER_MAX * io_thread_count * 2)
                        , send_buffer(ACK_COUNTER_MAX + 1) {
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

        state.rto_ms = (std::max)(
            10,
            static_cast<int>(std::lround(state.srtt_ms + 4.0 * state.rttvar_ms)));
        state.timed_out = false;
    }
    
    // 받아야할 패킷 번호 보다 뒷 번호 인지 확인 하는 함수.
    inline bool is_later_seq(int seq1, int seq2) {
        seq1 += seq1 < ACK_COUNTER_MAX / 2 ? ACK_COUNTER_MAX : 0;
        seq2 += seq2 < ACK_COUNTER_MAX / 2 ? ACK_COUNTER_MAX : 0;
        return seq1 > seq2;
    }
    //패킷에서 seq를 뽑아내는 함수
    inline int get_seq(const char* buf) {
        yc_pack::udp::convert_ack ack;
        ack.load(*buf);
        return ack.use_ack ? ack.counter : -1;
    }
    //ack처리용 패킷인지를 확인 하는 함수
    inline bool is_ack_packet(const char* buf) {
        yc_pack::udp::convert_ack ack;
        ack.load(*buf);
        return ack.is_ack_packet;
    }
    inline int get_next_seq(const int seq) {
        return (seq + 1) % ACK_COUNTER_MAX;
    }
    inline int make_seq(const int seq) {
        return seq % ACK_COUNTER_MAX;
    }
    int each_seq(const int start, const int end, auto f) {
        if (start == end) return end;
        for(int i = start; i < end; ++i) f(i % ACK_COUNTER_MAX);
        return end % ACK_COUNTER_MAX;
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
        std::vector<packet_raw>& pkt_buf,
        const int thread_id,
        const int thread_cnt_max,
        const char* buf,
        const size_t len
        ) {
        const auto seq = get_seq(buf);
        int idx = -1;
        if(seq == -1) {
            const auto s = ACK_COUNTER_MAX * thread_cnt_max + ACK_COUNTER_MAX * thread_id;
            for(int i = 0; i < ACK_COUNTER_MAX; ++i) {
                if(!pkt_buf[i + s].is_used) {
                    idx = i + s;
                    break;
                }
            }
        } else {
            idx = thread_id * ACK_COUNTER_MAX + seq;
        }
        auto& [data, pkt_len, is_used] = pkt_buf[idx];

        if(is_used) return -1;

        std::copy_n(buf + 1, len - 1, data);
        pkt_len = static_cast<packet_size_type>(len - 1);
        is_used = true;
        
        return idx;
    }
    
    inline packet_raw* find_seq_of_packets(
        std::vector<packet_raw>& pkt_buf,
        const int thread_id,
        const int seq
        ) {
        return pkt_buf[thread_id * ACK_COUNTER_MAX + seq].is_used ? &pkt_buf[thread_id * ACK_COUNTER_MAX + seq] : nullptr;
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
    inline std::pair<int, int> get_read_range(
        std::vector<packet_raw>& pkt_buf,
        std::vector<receive_packet_raw>& recv_buf,
        std::vector<receive_packet_raw>& no_ack_read_buf,
        const int thread_cnt_max,
        int end
        ) {
        int start = end;
        int cnt = 0;
        for(int i = start; i < start + ACK_COUNTER_MAX; ++i) {
            packet_raw* ptr = nullptr;
            for(int thread_id = 0; thread_id < thread_cnt_max; ++thread_id) {
                ptr = find_seq_of_packets(pkt_buf, thread_id, i % ACK_COUNTER_MAX);
                if(ptr) break;
            }
            if(ptr == nullptr) {
                end = i;
                break;
            }
            const int idx = i % ACK_COUNTER_MAX;
            std::copy_n(ptr->data, ptr->len, recv_buf[idx].data);
            recv_buf[idx].len = ptr->len;
            ptr->is_used = false;
            cnt++;
        }
        
        for(int thread_id = 0; thread_id < thread_cnt_max; ++thread_id) {
            for(int i = 0; i < ACK_COUNTER_MAX; ++i) {
                auto& [data, len, is_used] = pkt_buf[thread_cnt_max * ACK_COUNTER_MAX + thread_id * ACK_COUNTER_MAX + i];
                if(!is_used) break;
                no_ack_read_buf.emplace_back();
                std::copy_n(data, len, no_ack_read_buf.back().data);
                no_ack_read_buf.back().len = len;
                is_used = false;
            }
        }
        if(cnt > ACK_COUNTER_MAX) return {-1, -1};
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
     * \return ack 패킷의 seq, 버퍼가 전부 차있을 경우 -1, ack를 사용하지 않는 패킷일 경우 ACK_COUNTER_MAX
     */
    inline int ready_to_send(
        std::vector<send_packet_raw>& send_buf,
        std::vector<int>& resend_idx_buf,
        char* buf,
        const int len,
        const bool use_ack,
        const int seq
        ){
        if(use_ack && send_buf[seq].is_used) return -1;

        yc_pack::udp::convert_ack ack;
        ack.use_ack = use_ack;
        ack.is_ack_packet = false;
        ack.counter = seq;
        const int s = use_ack ? seq : ACK_COUNTER_MAX;
        send_buf[s].data[0] = ack.to_ack();
        std::copy_n(buf, len, send_buf[s].data + 1);
        send_buf[s].timestamp = get_timestamp();
        send_buf[s].len = static_cast<packet_size_type>(len + 1);
        send_buf[s].is_used = use_ack;
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
        std::vector<send_packet_raw>& send_buf,
        std::vector<int>& resend_idx_buf,
        rto_state& rto,
        const int seq,
        const int64_t now = get_timestamp()
        ) {
        if(!send_buf[seq].is_used) return -1;

        auto& packet = send_buf[seq];
        if (!packet.is_resend_packet) {
            const auto sample = static_cast<int>((std::max)(int64_t{1}, now - packet.timestamp));
            update_rto(rto, sample);
        }

        const auto& it = std::find(resend_idx_buf.begin(), resend_idx_buf.end(), seq);
        if(it != resend_idx_buf.end())
            resend_idx_buf.erase(it);
        packet.is_used = false;
        packet.is_resend_packet = false;
        return seq;
    }

    /**
     * \brief 재전송이 필요한 패킷을 찾습니다.
     * \param send_buf [OUT] resend가 필요한지 검사하는 버퍼
     * \param rto [OUT] timeout 시 backoff되는 RTO 상태
     * \param timeout 연결 timeout을 발생시킬 최대 RTO (ms)
     * \param now 테스트와 호출 시점 고정을 위한 현재 시각
     * \return resend가 필요한 패킷의 seq 목록
     */
    inline std::vector<int> get_resend_packets(
        std::vector<send_packet_raw>& send_buf,
        std::vector<int>& resend_idx_buf,
        rto_state& rto,
        const int timeout,
        const int64_t now = get_timestamp()
        ) {
        std::vector<int> result;
        for(const int& i : resend_idx_buf) {
            auto& pkt = send_buf[i];
            if (pkt.is_used && pkt.timestamp + rto.rto_ms < now) {
                result.push_back(i);
                pkt.is_resend_packet = true;
                pkt.timestamp = now;
            }
        }

        if (result.empty()) return result;

        const auto backed_off = rto.rto_ms * 2;
        if (backed_off > timeout) {
            rto.timed_out = true;
            return {};
        }

        rto.rto_ms = (std::max)(10, backed_off);
        return result;
    }
}
