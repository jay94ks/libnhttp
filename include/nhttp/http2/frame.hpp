#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nhttp::http2 {

	/* RFC 9113 §11.2 frame type registry — only what this implementation
	 * actually generates or must recognize. PUSH_PROMISE is deliberately
	 * absent: this server never sends one, and receiving one (client-to-
	 * server) is always a connection error (§6.6), handled generically by
	 * connection_h2 without needing a dedicated payload type here. */
	enum class frame_type : std::uint8_t {
		data = 0x0,
		headers = 0x1,
		priority = 0x2,
		rst_stream = 0x3,
		settings = 0x4,
		push_promise = 0x5,
		ping = 0x6,
		goaway = 0x7,
		window_update = 0x8,
		continuation = 0x9,
	};

	namespace frame_flags {
		inline constexpr std::uint8_t end_stream = 0x1;
		inline constexpr std::uint8_t ack = 0x1; // SETTINGS/PING share the bit with END_STREAM's value
		inline constexpr std::uint8_t end_headers = 0x4;
		inline constexpr std::uint8_t padded = 0x8;
		inline constexpr std::uint8_t priority = 0x20;
	}

	/* the 9-byte frame header every frame starts with (RFC 9113 §4.1). */
	struct frame_header {
		std::uint32_t length = 0; // payload length, 24 bits
		frame_type type = frame_type::data;
		std::uint8_t flags = 0;
		std::uint32_t stream_id = 0; // 31 bits (top bit reserved, always read/written as 0)

		static constexpr std::size_t wire_size = 9;

		void write(std::string& out) const;

		/* false only if `data` is shorter than wire_size — always call with
		 * exactly wire_size bytes available. */
		static bool parse(const std::uint8_t* data, frame_header& out) noexcept;
	};

	/* one SETTINGS parameter (RFC 9113 §6.5.2). */
	struct settings_param {
		std::uint16_t id = 0;
		std::uint32_t value = 0;
	};

	namespace settings_id {
		inline constexpr std::uint16_t header_table_size = 0x1;
		inline constexpr std::uint16_t enable_push = 0x2;
		inline constexpr std::uint16_t max_concurrent_streams = 0x3;
		inline constexpr std::uint16_t initial_window_size = 0x4;
		inline constexpr std::uint16_t max_frame_size = 0x5;
		inline constexpr std::uint16_t max_header_list_size = 0x6;
	}

	/* RFC 9113 §5.1.1 — even-numbered error codes a GOAWAY/RST_STREAM carries. */
	namespace error_code {
		inline constexpr std::uint32_t no_error = 0x0;
		inline constexpr std::uint32_t protocol_error = 0x1;
		inline constexpr std::uint32_t internal_error = 0x2;
		inline constexpr std::uint32_t flow_control_error = 0x3;
		inline constexpr std::uint32_t settings_timeout = 0x4;
		inline constexpr std::uint32_t stream_closed = 0x5;
		inline constexpr std::uint32_t frame_size_error = 0x6;
		inline constexpr std::uint32_t refused_stream = 0x7;
		inline constexpr std::uint32_t cancel = 0x8;
		inline constexpr std::uint32_t compression_error = 0x9;
		inline constexpr std::uint32_t connect_error = 0xa;
		inline constexpr std::uint32_t enhance_your_calm = 0xb;
		inline constexpr std::uint32_t inadequate_security = 0xc;
		inline constexpr std::uint32_t http_1_1_required = 0xd;
	}

	/* the client connection preface (RFC 9113 §3.4) — sent by every HTTP/2
	 * client before the first frame, over both TLS (post-ALPN) and cleartext
	 * (prior-knowledge) connections. used by listener to detect a
	 * prior-knowledge h2 connection and by connection_h2 to consume it. */
	inline const std::string client_preface = std::string("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");

	/* --- payload helpers: encode/decode the parts connection_h2 needs --- */

	void write_settings_payload(std::string& out, const std::vector<settings_param>& params);

	/* false on a malformed payload (length not a multiple of 6 — RFC 9113 §6.5). */
	bool parse_settings_payload(const std::uint8_t* data, std::size_t len, std::vector<settings_param>& out);

	void write_window_update_payload(std::string& out, std::uint32_t increment);
	bool parse_window_update_payload(const std::uint8_t* data, std::size_t len, std::uint32_t& increment);

	void write_rst_stream_payload(std::string& out, std::uint32_t error);
	bool parse_rst_stream_payload(const std::uint8_t* data, std::size_t len, std::uint32_t& error);

	void write_goaway_payload(std::string& out, std::uint32_t last_stream_id, std::uint32_t error, std::string_view debug_data);

	void write_ping_payload(std::string& out, const std::uint8_t (&opaque)[8]);

	/* strips DATA/HEADERS padding (RFC 9113 §6.1/§6.2) if PADDED is set;
	 * `payload` is the frame payload *after* the 9-byte header. false if the
	 * declared pad length doesn't fit. `header_block_start` is only relevant
	 * for HEADERS (past any Pad Length/priority fields the caller has
	 * already skipped); for DATA pass 0. */
	bool strip_padding(const std::uint8_t* payload, std::size_t len, bool padded, std::size_t& content_len, std::size_t& content_offset);

}
