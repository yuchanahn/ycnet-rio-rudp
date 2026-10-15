#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

using packet_sequence_type = std::int64_t;
constexpr packet_sequence_type max_packet_sequence = (std::numeric_limits<std::uint32_t>::max)();
constexpr int transport_header_size = 21; // version/flags + uint32 sequence + two uint64 session IDs
using packet_ack_type = std::array<char, transport_header_size>;
using packet_id_type = int8_t;
using packet_size_type = int16_t;
using packet_session_id_type = int64_t;

constexpr int packet_id_base = __COUNTER__;
constexpr int max_packet_types = 1000;
constexpr int reliable_window_size = 64; // storage slots, independent of packet identity
constexpr int max_packet_size = 1024; // 2^10

#define ycnet_packet(name, field) \
struct packet_##name { \
constexpr const static int packet_id = __COUNTER__ - (packet_id_base + 1); \
field \
static void bind(std::function<void(packet_##name, size_t)> f) { \
packet_events[packet_##name::packet_id] = [f](void* data, packet_size_type size, size_t client_id) { \
f(*((packet_##name*)data), client_id); \
}; \
} \
};

#define ycnet_variable_packet(name, type, value) \
struct variable_packet_##name { \
constexpr const static int packet_id = __COUNTER__ - (packet_id_base + 1); \
packet_session_id_type session_id;\
packet_size_type size;\
type value \
constexpr const static int type_size = sizeof(type); \
static void bind(std::function<void(variable_packet_##name, size_t)> f) { \
packet_events[variable_packet_##name::packet_id] = [f](void* data, packet_size_type size, size_t client_id) { \
variable_packet_##name data_;\
memcpy(&data_, data, size); \
f(data_, client_id); \
}; \
} \
};

#define ycnet_reliable_packet(name, field) \
struct reliable_packet_##name { \
constexpr const static int packet_id = __COUNTER__ - (packet_id_base + 1); \
constexpr const static bool is_reliable = true; \
field \
static void bind(std::function<void(reliable_packet_##name, size_t)> f) { \
packet_events[reliable_packet_##name::packet_id] = [f](void* data, packet_size_type size, size_t client_id) { \
f(*((reliable_packet_##name*)data), client_id); \
}; \
} \
};

#define ycnet_reliable_variable_packet(name, type, value) \
struct reliable_variable_packet_##name { \
constexpr const static int packet_id = __COUNTER__ - (packet_id_base + 1); \
constexpr const static bool is_reliable = true; \
packet_session_id_type session_id;\
packet_size_type size;\
type value \
constexpr const static int type_size = sizeof(type); \
static void bind(std::function<void(reliable_variable_packet_##name, size_t)> f) { \
packet_events[reliable_variable_packet_##name::packet_id] = [f](void* data, packet_size_type size, size_t client_id) { \
reliable_variable_packet_##name data_;\
memcpy(&data_, data, size); \
f(data_, client_id); \
}; \
} \
};


std::vector<std::function<void(void*, packet_size_type, size_t)>> packet_events(max_packet_types);
auto call_packet_event(void* data, packet_id_type packet_id, packet_size_type size, size_t client_id) {
	packet_events[packet_id](data, size, client_id);
};

namespace ycnet::packet {
	namespace udp
	{
		struct ack_header {

			enum ack_type : int {
				no_ack = -1,
			};
			
			bool use_ack;
			bool is_ack_packet;
			packet_sequence_type counter;
            std::uint64_t source_session{};
            std::uint64_t destination_session{};
            enum class control_type : std::uint8_t { none, hello, challenge, confirm };
            control_type control{};

			ack_header(packet_sequence_type cnt = no_ack) : use_ack(cnt >= 0), is_ack_packet(false), counter(cnt >= 0 ? cnt : 0) {}
			
			void load(const char* bytes) {
				const auto flags = static_cast<std::uint8_t>(bytes[0]);
				use_ack = (flags & 128) != 0;
				is_ack_packet = (flags & 64) != 0;
				std::uint32_t sequence = 0;
				for (int i = 1; i < 5; ++i)
					sequence = (sequence << 8) | static_cast<std::uint8_t>(bytes[i]);
				counter = sequence;
                source_session = destination_session = 0;
                for (int i = 0; i < 8; ++i) {
                    source_session = (source_session << 8) | static_cast<std::uint8_t>(bytes[5 + i]);
                    destination_session = (destination_session << 8) | static_cast<std::uint8_t>(bytes[13 + i]);
                }
                control = (flags & 32) ? static_cast<control_type>(sequence) : control_type::none;
			}
			void load(const packet_ack_type& bytes) { load(bytes.data()); }
			packet_ack_type to_ack() const {
				packet_ack_type bytes{};
				bytes[0] = static_cast<char>(2 | (control != control_type::none ? 32 : 0) | (use_ack << 7) | (is_ack_packet << 6));
				const auto sequence = control != control_type::none ? static_cast<std::uint32_t>(control) : static_cast<std::uint32_t>(counter);
				for (int i = 1; i < 5; ++i)
					bytes[i] = static_cast<char>((sequence >> ((4 - i) * 8)) & 255);
				for (int i = 0; i < 8; ++i) {
                    bytes[5 + i] = static_cast<char>(source_session >> ((7 - i) * 8));
                    bytes[13 + i] = static_cast<char>(destination_session >> ((7 - i) * 8));
                }
                return bytes;
			}
		};
	}
	
	template <typename packet_type>
	concept variable_packet_type = requires { packet_type::packet_id; packet_type::size; packet_type::type_size; };

	template <typename packet_type>
	concept fixed_packet_type = requires { packet_type::packet_id; } && !variable_packet_type<packet_type>;
	
	struct raw_packet {
		packet_size_type size;
		packet_id_type id;
		char* body;
	};

	constexpr int header_size = sizeof(packet_size_type) + sizeof(packet_id_type);

	// ReSharper disable once IdentifierTypo
	inline bool validate_packet(char* pkt, const size_t len) {
		if (pkt == nullptr || len < transport_header_size || len > max_packet_size) return false;
		if ((static_cast<std::uint8_t>(pkt[0]) & 31) != 2) return false;
		udp::ack_header ack;
		ack.load(pkt);
		if ((static_cast<std::uint8_t>(pkt[0]) & 32) != 0)
            return !ack.use_ack && !ack.is_ack_packet && len == transport_header_size
                && ack.counter >= 1 && ack.counter <= 3 && ack.source_session != 0
                && (ack.control == udp::ack_header::control_type::hello
                    ? ack.destination_session == 0 : ack.destination_session != 0);
        if (ack.is_ack_packet) return ack.use_ack && len == transport_header_size;
		if (len < static_cast<size_t>(header_size + transport_header_size)) return false;
		packet_size_type size;
		packet_id_type id;
		std::memcpy(&size, pkt + transport_header_size, sizeof(size));
		std::memcpy(&id, pkt + transport_header_size + sizeof(size), sizeof(id));
		if(id < 0 || id >= max_packet_types) return false;
		if(size < header_size || size > max_packet_size - transport_header_size) return false;
		if (len != static_cast<size_t>(size) + transport_header_size) return false;
		return true;
	}

	template <fixed_packet_type value_type>
	static raw_packet pack(value_type& packet_data) {
		return raw_packet{
			.size = sizeof(value_type) + header_size,
			.id = static_cast<packet_id_type>(value_type::packet_id),
			.body = reinterpret_cast<char*>(&packet_data)
		};
	}

	template <variable_packet_type value_type>
	static raw_packet pack(value_type& packet_data) {
		return raw_packet{
			.size = static_cast<packet_size_type>(packet_data.size * packet_data.type_size + header_size + sizeof(packet_size_type) + sizeof(packet_session_id_type)),
			.id = static_cast<packet_id_type>(value_type::packet_id),
			.body = reinterpret_cast<char*>(&packet_data)
		};
	}

	static raw_packet unpack(char* byte_code) {
		return raw_packet{
			.size = *reinterpret_cast<packet_size_type*>(byte_code),
			.id = *reinterpret_cast<packet_id_type*>(byte_code + sizeof(packet_size_type)),
			.body = byte_code + header_size
		};
	}
}
