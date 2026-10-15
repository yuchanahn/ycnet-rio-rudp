#pragma once
#include "rio_transport.hpp"
#include "packet.hpp"
#include "rudp.hpp"
#include "mpsc_buffer.hpp"
#include <atomic>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>

namespace ycnet {
enum class send_result { accepted, would_block, closed, packet_too_large, sequence_exhausted };
enum class connection_state { connecting, connected, closed };
enum class failure_reason { progress_timeout, handshake_timeout, sequence_exhausted, local_close };
struct delivery_failure {
  failure_reason reason;
  std::size_t unconfirmed_messages; // Delivery is unknown, not necessarily absent.
};
struct reliability_options {
  int max_rto_ms{1000};
  int progress_timeout_ms{10000};
  int handshake_timeout_ms{10000};
};
// For an already negotiated session, e.g. an isolated ARQ benchmark.
// IDs must be nonzero, fresh for each connection, and reversed at the other peer.
struct session_binding { std::uint64_t local{}, remote{}; };
inline std::uint64_t new_session_id() {
  static std::atomic<std::uint64_t> next{[] {
    std::random_device random;
    return (static_cast<std::uint64_t>(random()) << 32) | random() | 1;
  }()};
  auto id = next.load();
  for (;;) {
    if (id == 0 || id == (std::numeric_limits<std::uint64_t>::max)())
      throw std::runtime_error("session identity exhausted");
    if (next.compare_exchange_weak(id, id + 1)) return id;
  }
}
struct rudp_metrics {
  std::uint64_t reliable_sent{}, unreliable_sent{}, retransmissions{}, acks_received{};
  std::uint64_t duplicate_reacks{}, rejected_reliable_sends{}, rejected_session_packets{};
  int current_rto_ms{100};
  bool connection_timed_out{};
  std::size_t pending_reliable{};
};
template <typename T> concept is_ack_packet = requires { T::packet_id; T::is_reliable; };
template <typename T> concept is_no_ack_packet = requires { T::packet_id; } && !is_ack_packet<T>;
struct received_datagram {
  char buf[max_packet_size];
  size_t len;
  rio::endpoint ep;
  int thread_id;
};
template <size_t N>
void release_receive_buffer(detail::mpsc_buffer<received_datagram, N>& mem, received_datagram* buf) {
  mem.read_end(buf);
}
template <size_t N>
std::vector<received_datagram*> get_received_packets(detail::mpsc_buffer<received_datagram, N>& mem) {
  return mem.get_read_ranges();
}
inline packet_ack_type make_ack_packet(packet_sequence_type seq, session_binding session = {}) {
  packet::udp::ack_header header(seq);
  header.is_ack_packet = true;
  header.source_session = session.local;
  header.destination_session = session.remote;
  return header.to_ack();
}

// All protocol operations, including callbacks, run on one owning thread.
// Callbacks may send, but must not destroy/reenter the handler's receive/poll methods.
class rudp_handler {
  using control_type = packet::udp::ack_header::control_type;
  rudp::rudp_buffers buffers_{1};
  std::vector<int> resend_;
  std::vector<rudp::receive_packet_buffer> unreliable_;
  rudp::rto_state rto_;
  packet_sequence_type send_sequence_{}, receive_sequence_{};
  std::function<void(char*, size_t)> send_func_;
  std::function<void(std::string)> log_func_;
  std::function<int64_t()> clock_;
  std::function<void(delivery_failure)> failure_callback_;
  std::function<void()> connect_callback_;
  std::function<void(packet::raw_packet)> receive_callback_;
  rudp_metrics metrics_{};
  reliability_options options_;
  session_binding session_;
  connection_state state_;
  bool handshake_started_{};
  int64_t handshake_start_{}, handshake_last_sent_{}, send_progress_at_{}, receive_progress_at_{};
  control_type handshake_reply_{control_type::hello};

  bool has_receive_gap() const {
    for (int i = 0; i < reliable_window_size; ++i)
      if (buffers_.pkt_buffer[i].is_used) return true;
    return false;
  }
  void fail(failure_reason reason) {
    if (state_ == connection_state::closed) return;
    const delivery_failure event{reason, resend_.size()};
    state_ = connection_state::closed;
    metrics_.connection_timed_out = reason == failure_reason::progress_timeout
        || reason == failure_reason::handshake_timeout;
    buffers_.clear();
    resend_.clear();
    unreliable_.clear();
    if (failure_callback_) failure_callback_(event);
  }
  void send_control(control_type type) {
    packet::udp::ack_header header;
    header.control = type;
    header.source_session = session_.local;
    header.destination_session = type == control_type::hello ? 0 : session_.remote;
    auto bytes = header.to_ack();
    handshake_last_sent_ = clock_();
    send_func_(bytes.data(), bytes.size());
  }
  void start_handshake() {
    if (handshake_started_) return;
    handshake_started_ = true;
    handshake_start_ = clock_();
    handshake_reply_ = control_type::hello;
    send_control(handshake_reply_);
  }
  void establish() {
    if (state_ != connection_state::connecting) return;
    state_ = connection_state::connected;
    send_progress_at_ = receive_progress_at_ = clock_();
    for (const auto slot : resend_) {
      auto& pkt = buffers_.send_buffer[slot];
      packet::udp::ack_header header(pkt.sequence);
      header.source_session = session_.local;
      header.destination_session = session_.remote;
      const auto bytes = header.to_ack();
      std::copy(bytes.begin(), bytes.end(), pkt.data);
      pkt.timestamp = clock_();
      send_func_(pkt.data, pkt.len);
    }
    if (connect_callback_) connect_callback_();
  }
  void on_control(const packet::udp::ack_header& header) {
    if (header.control == control_type::hello) {
      if (session_.remote != 0 && session_.remote != header.source_session) {
        ++metrics_.rejected_session_packets;
        return;
      }
      session_.remote = header.source_session;
      if (!handshake_started_) { handshake_started_ = true; handshake_start_ = clock_(); }
      handshake_reply_ = control_type::challenge;
      send_control(control_type::challenge);
      return;
    }
    if (header.destination_session != session_.local
        || (session_.remote != 0 && header.source_session != session_.remote)) {
      ++metrics_.rejected_session_packets;
      return;
    }
    if (header.control == control_type::challenge) {
      if (!handshake_started_) { ++metrics_.rejected_session_packets; return; }
      session_.remote = header.source_session;
      send_control(control_type::confirm);
      establish();
    } else if (header.control == control_type::confirm && session_.remote != 0) {
      establish();
    }
  }
  void dispatch(char* data) {
    auto pkt = packet::unpack(data);
    if (receive_callback_) receive_callback_(pkt);
    else if (packet_events[pkt.id]) packet_events[pkt.id](pkt.body, pkt.size, 0);
  }
  template <typename T> send_result send_packet(T& value, bool reliable) {
    if (state_ == connection_state::closed) return send_result::closed;
    if (reliable && send_sequence_ > max_packet_sequence) {
      fail(failure_reason::sequence_exhausted);
      return send_result::sequence_exhausted;
    }
    auto pkt = packet::pack(value);
    if (pkt.size < packet::header_size || pkt.size > max_packet_size - transport_header_size)
      return send_result::packet_too_large;
    // Unreliable messages are not silently queued during connection setup.
    if (!reliable && state_ != connection_state::connected) {
      start_handshake();
      return send_result::would_block;
    }
    char bytes[max_packet_size]{};
    std::memcpy(bytes, &pkt.size, sizeof(pkt.size));
    std::memcpy(bytes + sizeof(pkt.size), &pkt.id, sizeof(pkt.id));
    std::copy_n(pkt.body, pkt.size - packet::header_size, bytes + packet::header_size);
    const bool was_pending = !resend_.empty();
    const int slot = rudp::ready_to_send(buffers_.send_buffer, resend_, bytes, pkt.size,
        reliable, reliable ? send_sequence_ : 0, clock_(), session_.local, session_.remote,
        (std::min)(rto_.rto_ms, options_.max_rto_ms));
    if (slot < 0) {
      if (reliable) ++metrics_.rejected_reliable_sends;
      log_func_("reliable send window full");
      return send_result::would_block;
    }
    if (reliable && !was_pending) send_progress_at_ = clock_();
    if (reliable) { ++send_sequence_; ++metrics_.reliable_sent; }
    else ++metrics_.unreliable_sent;
    if (state_ == connection_state::connected) {
      auto& out = buffers_.send_buffer[slot];
      send_func_(out.data, out.len);
    } else start_handshake();
    return send_result::accepted;
  }
public:
  explicit rudp_handler(std::function<void(std::string)> log_func,
                        std::function<void(char*, size_t)> send_func,
                        std::function<int64_t()> clock = rudp::get_timestamp,
                        session_binding session = {})
      : send_func_(std::move(send_func)), log_func_(std::move(log_func)), clock_(std::move(clock)),
        session_{session.local ? session.local : new_session_id(), session.remote},
        state_(session.local && session.remote ? connection_state::connected : connection_state::connecting) {
    resend_.reserve(reliable_window_size);
    unreliable_.reserve(reliable_window_size);
  }
  void set_reliability_options(reliability_options options) {
    if (options.max_rto_ms <= 0 || options.progress_timeout_ms <= 0 || options.handshake_timeout_ms <= 0)
      throw std::invalid_argument("reliability timeouts must be positive");
    options_ = options;
  }
  void set_failure_callback(std::function<void(delivery_failure)> callback) { failure_callback_ = std::move(callback); }
  void set_connect_callback(std::function<void()> callback) { connect_callback_ = std::move(callback); }
  void set_receive_callback(std::function<void(packet::raw_packet)> callback) { receive_callback_ = std::move(callback); }
  [[nodiscard]] connection_state state() const { return state_; }
  [[nodiscard]] session_binding session() const { return session_; }
  void close() { fail(failure_reason::local_close); }
  // accepted means retained by the transport, not confirmed by the receiver.
  send_result try_send(is_ack_packet auto& packet) { return send_packet(packet, true); }
  send_result try_send(is_no_ack_packet auto& packet) { return send_packet(packet, false); }
  bool send(is_ack_packet auto& packet) { return try_send(packet) == send_result::accepted; }
  send_result send(is_no_ack_packet auto& packet) { return try_send(packet); }

  void on_receive(char* buf, size_t len) {
    if (state_ == connection_state::closed) return;
    if (!packet::validate_packet(buf, len)) { log_func_("packet verification failed"); return; }
    packet::udp::ack_header header;
    header.load(buf);
    if (header.control != control_type::none) { on_control(header); return; }
    if (session_.remote == 0 || header.source_session != session_.remote
        || header.destination_session != session_.local) {
      ++metrics_.rejected_session_packets;
      return;
    }
    // A data retransmission also confirms the challenge if CONFIRM was lost.
    establish();
    if (state_ == connection_state::closed) return;
    if (header.is_ack_packet) {
      if (rudp::set_send_complete(buffers_.send_buffer, resend_, rto_, header.counter, clock_()) >= 0) {
        ++metrics_.acks_received;
        send_progress_at_ = clock_();
      }
      return;
    }
    auto acknowledge = [&](packet_sequence_type seq) {
      auto ack = make_ack_packet(seq, session_);
      send_func_(ack.data(), ack.size());
    };
    const auto seq = header.use_ack ? header.counter : -1;
    if (seq >= 0 && seq < receive_sequence_) {
      ++metrics_.duplicate_reacks;
      acknowledge(seq);
      return;
    }
    if (seq >= 0 && seq - receive_sequence_ >= reliable_window_size) return;
    const bool was_pending = has_receive_gap();
    if (rudp::push_packet(buffers_.pkt_buffer, 0, 1, buf, len) < 0) return;
    if (!was_pending) receive_progress_at_ = clock_();
    auto [start, end] = rudp::get_read_range(buffers_.pkt_buffer, buffers_.receive_buffer, unreliable_, 1, receive_sequence_);
    // Advance before callbacks so a send from a callback sees the current state.
    receive_sequence_ = end;
    if (end > start) receive_progress_at_ = clock_();
    for (auto seq = start; seq < end && state_ != connection_state::closed; ++seq) {
      acknowledge(seq);
      dispatch(buffers_.receive_buffer[rudp::sequence_slot(seq)].data);
    }
    for (auto& pkt : unreliable_) {
      if (state_ == connection_state::closed) break;
      dispatch(pkt.data);
      if (state_ == connection_state::closed) return;
    }
    unreliable_.clear();
  }
  void process_retransmissions() {
    if (state_ == connection_state::closed) return;
    const auto now = clock_();
    if (state_ == connection_state::connecting) {
      if (!handshake_started_) return;
      if (now - handshake_start_ >= options_.handshake_timeout_ms) { fail(failure_reason::handshake_timeout); return; }
      if (now - handshake_last_sent_ >= 100) send_control(handshake_reply_);
      return;
    }
    if ((!resend_.empty() && now - send_progress_at_ >= options_.progress_timeout_ms)
        || (has_receive_gap() && now - receive_progress_at_ >= options_.progress_timeout_ms)) {
      fail(failure_reason::progress_timeout);
      return;
    }
    const auto due = rudp::get_resend_packets(buffers_.send_buffer, resend_, rto_, options_.max_rto_ms, now);
    metrics_.retransmissions += due.size();
    for (auto slot : due) {
      auto& pkt = buffers_.send_buffer[slot];
      send_func_(pkt.data, pkt.len);
    }
  }
  [[nodiscard]] rudp_metrics metrics() const {
    auto result = metrics_;
    result.current_rto_ms = rto_.rto_ms;
    result.pending_reliable = resend_.size();
    return result;
  }
};

template <size_t max_clients, size_t io_thread_count, int port = 51234>
class rudp_server {
  detail::mpsc_buffer<received_datagram, io_thread_count> packets_buffer_;
  rio::rio_udp_server_controller server_controller_;
  std::vector<rio::endpoint> endpoints_{max_clients};
  std::unordered_map<rio::endpoint, size_t> ids_;
  std::vector<size_t> free_ids_;
  std::vector<int> send_threads_ = std::vector<int>(max_clients);
  std::vector<bool> processing_ = std::vector<bool>(max_clients);
  std::vector<std::unique_ptr<rudp_handler>> clients_{max_clients};
  std::function<void(std::string)> log_func_;
  std::function<void(rio::endpoint, size_t)> timeout_callback_, connect_callback_;
  std::function<void(rio::endpoint, size_t, delivery_failure)> failure_callback_;
  reliability_options options_;

  void release_closed(size_t id) {
    if (processing_[id] || !clients_[id] || clients_[id]->state() != connection_state::closed) return;
    ids_.erase(endpoints_[id]);
    clients_[id].reset();
    endpoints_[id] = {};
    free_ids_.push_back(id);
  }
  void create_client(size_t id, rio::endpoint endpoint) {
    endpoints_[id] = endpoint;
    ids_[endpoint] = id;
    clients_[id] = std::make_unique<rudp_handler>(log_func_, [this, id](char* data, size_t len) {
      const auto result = rio::send_udp(data, static_cast<int>(len), endpoints_[id], send_threads_[id]);
      if (!result) log_func_(result.err);
    });
    auto& client = *clients_[id];
    client.set_reliability_options(options_);
    client.set_connect_callback([this, id] {
      if (connect_callback_) connect_callback_(endpoints_[id], id);
    });
    client.set_receive_callback([id](packet::raw_packet pkt) {
      if (packet_events[pkt.id]) packet_events[pkt.id](pkt.body, pkt.size, id);
    });
    client.set_failure_callback([this, id](delivery_failure event) {
      if (failure_callback_) failure_callback_(endpoints_[id], id, event);
      if ((event.reason == failure_reason::progress_timeout || event.reason == failure_reason::handshake_timeout)
          && timeout_callback_) timeout_callback_(endpoints_[id], id);
    });
  }
public:
  explicit rudp_server(std::function<void(std::string)> log_func) : log_func_(std::move(log_func)) {
    for (size_t i = max_clients; i > 0; --i) free_ids_.push_back(i - 1);
  }
  void set_reliability_options(reliability_options options) {
    if (options.max_rto_ms <= 0 || options.progress_timeout_ms <= 0 || options.handshake_timeout_ms <= 0)
      throw std::invalid_argument("reliability timeouts must be positive");
    options_ = options;
    for (auto& client : clients_) if (client) client->set_reliability_options(options);
  }
  void set_progress_timeout(int ms) { auto options = options_; options.progress_timeout_ms = ms; set_reliability_options(options); }
  [[deprecated("Use set_progress_timeout or set_reliability_options")]]
  void set_retransmission_timeout(int ms) { set_progress_timeout(ms); }
  void set_timeout_callback(std::function<void(rio::endpoint, size_t)> callback) { timeout_callback_ = std::move(callback); }
  void set_failure_callback(std::function<void(rio::endpoint, size_t, delivery_failure)> callback) { failure_callback_ = std::move(callback); }
  void set_connect_callback(std::function<void(rio::endpoint, size_t)> callback) { connect_callback_ = std::move(callback); }
  void process_received_packets() {
    // Pending handshakes must expire even when no application client ID exists.
    for (size_t id = 0; id < max_clients; ++id) process_retransmissions(id);
    for (auto* datagram : get_received_packets(packets_buffer_)) {
      packet::udp::ack_header header;
      header.load(datagram->buf);
      auto found = ids_.find(datagram->ep);
      size_t id;
      if (found == ids_.end()) {
        // Unknown data/ACK/CONFIRM must never resurrect a previous connection.
        if (header.control != packet::udp::ack_header::control_type::hello) {
          release_receive_buffer(packets_buffer_, datagram);
          continue;
        }
        if (free_ids_.empty()) {
          log_func_("server client capacity reached");
          release_receive_buffer(packets_buffer_, datagram);
          continue;
        }
        id = free_ids_.back(); free_ids_.pop_back();
        create_client(id, datagram->ep);
      } else id = found->second;
      processing_[id] = true;
      clients_[id]->on_receive(datagram->buf, datagram->len);
      processing_[id] = false;
      release_receive_buffer(packets_buffer_, datagram);
      release_closed(id);
    }
  }
  send_result try_send(auto& packet, size_t id, int thread_num = 0) {
    if (id >= max_clients || !clients_[id]) return send_result::closed;
    if (thread_num < 0 || thread_num >= io_thread_count) throw std::invalid_argument("RIO send thread out of range");
    send_threads_[id] = thread_num;
    return clients_[id]->try_send(packet);
  }
  bool send(is_ack_packet auto& packet, size_t id, int thread_num = 0) {
    return try_send(packet, id, thread_num) == send_result::accepted;
  }
  send_result send(is_no_ack_packet auto& packet, size_t id, int thread_num = 0) {
    return try_send(packet, id, thread_num);
  }
  void process_retransmissions(size_t id, int thread_num = 0) {
    if (id >= max_clients || !clients_[id]) return;
    if (thread_num < 0 || thread_num >= io_thread_count) throw std::invalid_argument("RIO send thread out of range");
    send_threads_[id] = thread_num;
    processing_[id] = true;
    clients_[id]->process_retransmissions();
    processing_[id] = false;
    release_closed(id);
  }
  void disconnect(size_t id) {
    if (id >= max_clients || !clients_[id]) return;
    clients_[id]->close();
    release_closed(id);
  }
  void start_io() {
    const auto result = rio::start_udp_server(io_thread_count, port,
      [this](const char* data, int size, const rio::endpoint& endpoint, int thread_id) {
        if (!packet::validate_packet(const_cast<char*>(data), size)) return;
        received_datagram* datagram = nullptr;
        while (!packets_buffer_.try_push(thread_id, datagram))
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::copy_n(data, size, datagram->buf);
        datagram->len = size;
        datagram->ep = endpoint;
        packets_buffer_.make_readable(datagram);
      }, printf);
    if (result) server_controller_ = *result;
    else log_func_(result.err);
  }
  ycnet::detail::result<size_t> user_id(const rio::endpoint& endpoint) {
    const auto found = ids_.find(endpoint);
    if (found == ids_.end() || clients_[found->second]->state() != connection_state::connected)
      return "user not connected";
    return ycnet::detail::result(found->second);
  }
  [[nodiscard]] ycnet::detail::result<int> stop() const { return server_controller_.stop(); }
};
} // namespace ycnet
