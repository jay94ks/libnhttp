#include "nhttp/http2/frame.hpp"

namespace nhttp::http2 {

	namespace {

		void write_u24(std::string& out, std::uint32_t value) {
			out += static_cast<char>(static_cast<std::uint8_t>((value >> 16) & 0xff));
			out += static_cast<char>(static_cast<std::uint8_t>((value >> 8) & 0xff));
			out += static_cast<char>(static_cast<std::uint8_t>(value & 0xff));
		}

		void write_u32(std::string& out, std::uint32_t value) {
			out += static_cast<char>(static_cast<std::uint8_t>((value >> 24) & 0xff));
			out += static_cast<char>(static_cast<std::uint8_t>((value >> 16) & 0xff));
			out += static_cast<char>(static_cast<std::uint8_t>((value >> 8) & 0xff));
			out += static_cast<char>(static_cast<std::uint8_t>(value & 0xff));
		}

		void write_u16(std::string& out, std::uint16_t value) {
			out += static_cast<char>(static_cast<std::uint8_t>((value >> 8) & 0xff));
			out += static_cast<char>(static_cast<std::uint8_t>(value & 0xff));
		}

		std::uint32_t read_u24(const std::uint8_t* p) noexcept {
			return (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
		}

		std::uint32_t read_u32(const std::uint8_t* p) noexcept {
			return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
				(static_cast<std::uint32_t>(p[2]) << 8) | p[3];
		}

		std::uint16_t read_u16(const std::uint8_t* p) noexcept {
			return static_cast<std::uint16_t>((static_cast<std::uint32_t>(p[0]) << 8) | p[1]);
		}

	}

	void frame_header::write(std::string& out) const {
		write_u24(out, length & 0x00ffffffu);
		out += static_cast<char>(static_cast<std::uint8_t>(type));
		out += static_cast<char>(flags);
		write_u32(out, stream_id & 0x7fffffffu);
	}

	bool frame_header::parse(const std::uint8_t* data, frame_header& out) noexcept {
		out.length = read_u24(data);
		out.type = static_cast<frame_type>(data[3]);
		out.flags = data[4];
		out.stream_id = read_u32(data + 5) & 0x7fffffffu;
		return true;
	}

	void write_settings_payload(std::string& out, const std::vector<settings_param>& params) {
		for (const settings_param& p : params) {
			write_u16(out, p.id);
			write_u32(out, p.value);
		}
	}

	bool parse_settings_payload(const std::uint8_t* data, std::size_t len, std::vector<settings_param>& out) {
		if (len % 6 != 0)
			return false;

		for (std::size_t i = 0; i < len; i += 6) {
			settings_param p;
			p.id = read_u16(data + i);
			p.value = read_u32(data + i + 2);
			out.push_back(p);
		}

		return true;
	}

	void write_window_update_payload(std::string& out, std::uint32_t increment) {
		write_u32(out, increment & 0x7fffffffu);
	}

	bool parse_window_update_payload(const std::uint8_t* data, std::size_t len, std::uint32_t& increment) {
		if (len != 4)
			return false;

		increment = read_u32(data) & 0x7fffffffu;
		return true;
	}

	void write_rst_stream_payload(std::string& out, std::uint32_t error) {
		write_u32(out, error);
	}

	bool parse_rst_stream_payload(const std::uint8_t* data, std::size_t len, std::uint32_t& error) {
		if (len != 4)
			return false;

		error = read_u32(data);
		return true;
	}

	void write_goaway_payload(std::string& out, std::uint32_t last_stream_id, std::uint32_t error, std::string_view debug_data) {
		write_u32(out, last_stream_id & 0x7fffffffu);
		write_u32(out, error);
		out += debug_data;
	}

	void write_ping_payload(std::string& out, const std::uint8_t (&opaque)[8]) {
		for (const std::uint8_t b : opaque)
			out += static_cast<char>(b);
	}

	bool strip_padding(const std::uint8_t* payload, std::size_t len, bool padded, std::size_t& content_len, std::size_t& content_offset) {
		if (!padded) {
			content_offset = 0;
			content_len = len;
			return true;
		}

		if (len < 1)
			return false;

		const std::size_t pad_length = payload[0];

		if (pad_length + 1 > len)
			return false;

		content_offset = 1;
		content_len = len - 1 - pad_length;
		return true;
	}

}
