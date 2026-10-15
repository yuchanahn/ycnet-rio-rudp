#pragma once
#include <SDKDDKVer.h>
// Winsock types must be defined before the RIO extension declarations.
#include <WinSock2.h>
#include <MSWsock.h>
#include <WS2tcpip.h>
#include <algorithm>
#include <format>
#include <functional>
#include <iostream>
#include <ranges>
#include <sstream>
#include <tchar.h>
#include <thread>
#include <unordered_map>
#include <vector>

#include "support/result.hpp"
#include "support/srw_lock.hpp"

#pragma comment(lib, "ws2_32.lib")

#define ycnet_error_text(msg)                                                  \
  std::format("{} Error: {}", #msg, ::GetLastError()).c_str()

namespace ycnet {
namespace rio {
inline RIO_EXTENSION_FUNCTION_TABLE g_rio_ft;
inline SOCKET g_socket;
inline detail::srw_lock g_cqrq_lock;

enum rio_config {
  pending_receive_count = 1000,
  pending_send_count = 1000,
  receive_buffer_size = 1024,
  send_buffer_size = 1024,

  address_buffer_size = 64,

  address_buffer_bytes =
      (pending_receive_count + pending_send_count) * address_buffer_size,

  max_completion_results = 1000
};

inline std::vector<std::thread> worker_threads;

enum completion_key { completion_stop = 0, completion_start = 1 };

enum operation_type {
  operation_none = 0,
  operation_receive = 1,
  operation_send = 2
};

struct extended_rio_buffer : RIO_BUF {
  operation_type operation;
  int thread_number;
  DWORD index;
};

inline char *allocate_buf(const DWORD size) {
  return static_cast<char *>(VirtualAllocEx(GetCurrentProcess(), nullptr, size,
                                            MEM_COMMIT | MEM_RESERVE,
                                            PAGE_READWRITE));
}

struct rio_udp_server_controller {
  std::function<ycnet::detail::result<int>()> stop;
};

extended_rio_buffer *addr_buf;

RIO_BUFFERID send_buf_id;
RIO_BUFFERID recv_buf_id;
RIO_BUFFERID addr_buf_id;

char *send_buf_ptr;
char *recv_buf_ptr;
char *addr_buf_ptr;

// 엔드포인트를 사용할 때 메모리 복사가 일어나는데 그것을 방지해야함.
// 아마 Union으로 구현하면 될 것 같음.
struct endpoint : SOCKADDR_IN {
  std::string to_string() const {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sin_addr, ip, INET_ADDRSTRLEN);
    return std::format("{}:{}", ip, ntohs(sin_port));
  }
  friend std::hash<endpoint>;
  friend bool operator==(const endpoint &my, const endpoint &other) {
    return my.sin_addr.S_un.S_addr == other.sin_addr.S_un.S_addr &&
           my.sin_port == other.sin_port;
  }
};
struct pooled_receive_data {
  DWORD read_buf_size;
  DWORD read_buf_offset;
  endpoint endpoint;
  volatile bool is_used;
};

inline RIO_CQ g_cq;
inline RIO_RQ g_rq;

struct send_buffer {
  extended_rio_buffer buf;
  extended_rio_buffer addr_buf;
  volatile bool is_used;
};

inline std::vector<std::vector<send_buffer>> send_bufs;
inline bool is_running = true;
static auto is_not_used = [](const auto &x) { return !x.is_used; };
static auto is_used = [](const auto &x) { return x.is_used; };

/**
 * \brief send가 thread safe 하지 않기 때문에 호출 할 때 알아서 해줘야함.
 * \param buf 전송할 데이터
 * \param size 전송할 데이터 크기
 * \param addr 전송할 주소
 * \param thread_number 전송할 스레드 번호, 스레드 번호는 스레드의 개수보다
 * 작아야함. \return 전송한 바이트 크기
 */
ycnet::detail::result<int> send_udp(const char *buf, int size,
                                    const endpoint addr,
                                    const int thread_number) {

  if (size > send_buffer_size)
    return ycnet_error_text(SEND SIZE > send_buffer_size(1024));

  // busy wait
  while (std::ranges::empty(send_bufs[thread_number] |
                            std::views::filter(is_not_used))) {
    std::this_thread::sleep_for(std::chrono::microseconds(1));
  }
  for (auto &context :
       send_bufs[thread_number] | std::views::filter(is_not_used)) {
    char *send_offset = send_buf_ptr + context.buf.Offset;
    std::copy_n(buf, size, send_offset);
    std::copy_n((char *)&addr, sizeof(sockaddr_in),
                addr_buf_ptr + context.addr_buf.Offset);
    context.buf.Length = size;
    context.is_used = true;

    g_cqrq_lock.lock_exclusive();
    const auto r =
        g_rio_ft.RIOSendEx(g_rq, &context.buf, 1, nullptr, &context.addr_buf,
                           nullptr, nullptr, 0, &context.buf);
    g_cqrq_lock.unlock_exclusive();
    if (!r)
      return ycnet_error_text(RIOSend);
    return ycnet::detail::result(size);
  }
  return ycnet_error_text(NO SEND BUFFER);
}

inline ycnet::detail::result<DWORD> bind_receive_udp(extended_rio_buffer *buf) {
  g_cqrq_lock.lock_exclusive();
  const auto r = g_rio_ft.RIOReceiveEx(
      g_rq, buf, 1, nullptr, &addr_buf[buf->index], nullptr, nullptr, 0, buf);
  g_cqrq_lock.unlock_exclusive();
  if (!r)
    return ycnet_error_text(RIOReceive);
  return ycnet::detail::result(buf->index);
}

inline ycnet::detail::result<rio_udp_server_controller> start_udp_server(
    const int thread_count, const u_short port,
    const std::function<void(const char *, int, endpoint, int)> &recv_callback,
    const std::function<void(const char *)> &io_thread_msg_callback) {
  worker_threads.reserve(thread_count);

  static WSADATA data;
  if (::WSAStartup(0x202, &data))
    return ycnet_error_text(WSAStartup);

  g_socket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0,
                       WSA_FLAG_REGISTERED_IO);
  if (g_socket == INVALID_SOCKET)
    return ycnet_error_text(WSASocket);

  static sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (SOCKET_ERROR ==
      ::bind(g_socket, reinterpret_cast<sockaddr *>(&addr), sizeof addr))
    return ycnet_error_text(Bind);

  static GUID function_table_id = WSAID_MULTIPLE_RIO;
  static DWORD dw_bytes = 0;

  if (0 != WSAIoctl(g_socket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
                    &function_table_id, sizeof(GUID), &g_rio_ft,
                    sizeof g_rio_ft, &dw_bytes, nullptr, nullptr))
    return ycnet_error_text(WSAIoctl);

  g_cq = g_rio_ft.RIOCreateCompletionQueue(
      pending_receive_count + pending_send_count, nullptr);
  if (g_cq == RIO_INVALID_CQ)
    return ycnet_error_text(RIOCreateCompletionQueue);

  g_rq = g_rio_ft.RIOCreateRequestQueue(g_socket, pending_receive_count, 1,
                                        pending_send_count, 1, g_cq, g_cq,
                                        nullptr);

  if (g_rq == RIO_INVALID_RQ)
    return ycnet_error_text(RIOCreateRequestQueue);
  {
    send_bufs.reserve(thread_count);
    send_buf_ptr = allocate_buf(pending_send_count * send_buffer_size);
    send_buf_id = g_rio_ft.RIORegisterBuffer(
        send_buf_ptr, pending_send_count * send_buffer_size);
    if (send_buf_id == RIO_INVALID_BUFFERID)
      return ycnet_error_text(RIORegisterBuffer);
    DWORD offset = 0;
    const auto size = pending_send_count / thread_count;

    for (int i = 0; i < thread_count; ++i) {
      send_bufs.emplace_back();
      send_bufs[i].reserve(size);
      for (int j = 0; j < size; ++j) {
        send_bufs[i].emplace_back();
        auto &[buf, addr_buf_, is_used] = send_bufs[i].back();
        buf.operation = operation_send;
        buf.BufferId = send_buf_id;
        buf.Offset = offset;
        buf.Length = send_buffer_size;
        buf.index = j;
        buf.thread_number = i;
        is_used = false;
        offset += send_buffer_size;
      }
    }
  }

  addr_buf_ptr = allocate_buf(address_buffer_bytes);
  addr_buf_id = g_rio_ft.RIORegisterBuffer(addr_buf_ptr, address_buffer_bytes);

  if (addr_buf_id == RIO_INVALID_BUFFERID)
    return ycnet_error_text(RIORegisterBuffer);

  DWORD offset = 0;
  addr_buf = new extended_rio_buffer[pending_receive_count];

  for (DWORD j = 0; j < pending_receive_count; ++j) {
    extended_rio_buffer *p_buffer = addr_buf + j;

    p_buffer->operation = operation_none;
    p_buffer->BufferId = addr_buf_id;
    p_buffer->Offset = offset;
    p_buffer->Length = address_buffer_size;

    offset += address_buffer_size;
  }

  const auto size = pending_send_count / thread_count;
  for (int i = 0; i < thread_count; ++i) {
    for (int j = 0; j < size; ++j) {
      auto &[buf, addr_buf_, is_used] = send_bufs[i][j];

      addr_buf_.operation = operation_none;
      addr_buf_.BufferId = addr_buf_id;
      addr_buf_.Offset = offset;
      addr_buf_.Length = address_buffer_size;
      offset += address_buffer_size;
    }
  }

  recv_buf_ptr = allocate_buf(pending_receive_count * receive_buffer_size);
  recv_buf_id = g_rio_ft.RIORegisterBuffer(
      recv_buf_ptr, pending_receive_count * receive_buffer_size);
  if (recv_buf_id == RIO_INVALID_BUFFERID)
    return ycnet_error_text(RIORegisterBuffer);
  offset = 0;
  const auto buf = new extended_rio_buffer[pending_receive_count];

  for (DWORD j = 0; j < pending_receive_count; ++j) {
    extended_rio_buffer *buffer_ptr = buf + j;

    buffer_ptr->operation = operation_receive;
    buffer_ptr->index = j;
    buffer_ptr->BufferId = recv_buf_id;
    buffer_ptr->Offset = offset;
    buffer_ptr->Length = receive_buffer_size;
    offset += receive_buffer_size;

    if (auto r = bind_receive_udp(buffer_ptr); !r.has_value())
      return r.err.c_str();
  }

  for (int i = 0; i < thread_count; ++i) {
    worker_threads.emplace_back([thread_number = i, recv_callback,
                                 io_thread_msg_callback] {
      while (is_running) {
        RIORESULT results[max_completion_results] = {};

        g_cqrq_lock.lock_exclusive();
        const ULONG r_num = g_rio_ft.RIODequeueCompletion(
            g_cq, results, max_completion_results);
        g_cqrq_lock.unlock_exclusive();

        if (0 == r_num) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          continue;
        }
        for (DWORD i = 0; i < r_num; ++i) {
          if (const auto buffer_ptr = reinterpret_cast<extended_rio_buffer *>(
                  results[i].RequestContext);
              operation_receive == buffer_ptr->operation) {
            const char *source = recv_buf_ptr + buffer_ptr->Offset;
            const auto addr_ptr = reinterpret_cast<endpoint *>(
                addr_buf_ptr + addr_buf[buffer_ptr->index].Offset);
            recv_callback(source, static_cast<int>(results[i].BytesTransferred),
                          *addr_ptr, thread_number);
            if (auto r = bind_receive_udp(buffer_ptr); !r.has_value())
              io_thread_msg_callback(r.err.c_str());
          } else if (operation_send == buffer_ptr->operation) {
            send_bufs[buffer_ptr->thread_number][buffer_ptr->index].is_used =
                false;
          } else {
            io_thread_msg_callback(ycnet_error_text(Unknown operation));
            break;
          }
        }
      }
      std::stringstream ss;
      ss << std::this_thread::get_id();
      std::string str = ss.str();
      io_thread_msg_callback(
          std::format(" -- thread[{} : id-{}] exit -- \n", thread_number, str)
              .c_str());
    });
  }

  return ycnet::detail::result(
      rio_udp_server_controller{[]() -> ycnet::detail::result<int> {
        is_running = false;
        std::ranges::for_each(worker_threads, [](std::thread &t) { t.join(); });

        if (SOCKET_ERROR == ::closesocket(g_socket))
          return ycnet_error_text(closesocket);

        g_rio_ft.RIOCloseCompletionQueue(g_cq);
        g_rio_ft.RIODeregisterBuffer(send_buf_id);
        g_rio_ft.RIODeregisterBuffer(recv_buf_id);
        g_rio_ft.RIODeregisterBuffer(addr_buf_id);

        return ycnet::detail::result(1);
      }});
}
} // namespace rio
} // namespace ycnet

template <class value_type>
void hash_combine(std::size_t &s, const value_type &v) {
  std::hash<value_type> h;
  s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
}

template <> struct std::hash<ycnet::rio::endpoint> {
  size_t operator()(const ycnet::rio::endpoint &ep) const noexcept {
    size_t hash = 0;
    hash_combine(hash, ep.sin_family);
    hash_combine(hash, ep.sin_port);
    hash_combine(hash, ep.sin_addr.s_addr);
    return hash;
  }
};
