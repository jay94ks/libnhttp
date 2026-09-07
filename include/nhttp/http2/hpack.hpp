#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace nhttp::http2 {

	/* one decoded/to-be-encoded header field. HPACK's header list includes
	 * HTTP/2 pseudo-headers (":method", ":path", ...) alongside regular
	 * headers, so this is deliberately a flat name/value pair rather than
	 * protocol::http_headers — connection_h2 maps pseudo-headers onto
	 * http_resource and everything else onto http_headers afterward (see
	 * CONCEPTS.md's wire-decoded-header-model seam: nothing below that
	 * mapping point ever sees HPACK/wire details). */
	struct header_field {
		std::string name;
		std::string value;
	};

	using header_list = std::vector<header_field>;

	/**
	 * class hpack_decoder.
	 * one instance per HTTP/2 connection (its dynamic table is connection-
	 * scoped and stateful across header blocks — RFC 7541 §2.3.2). decode()
	 * expects one fully-reassembled header block (concatenated across any
	 * CONTINUATION frames) at a time.
	 */
	class hpack_decoder {
	public:
		explicit hpack_decoder(std::size_t max_dynamic_table_size = 4096) noexcept
			: max_dynamic_table_size_(max_dynamic_table_size)
		{
		}

		/* false on any malformed/invalid input — per RFC 7541 §4.3 and §6.3.2,
		 * a HPACK decoding error is not recoverable and must be treated as a
		 * connection-level (not stream-level) error by the caller. */
		bool decode(const std::uint8_t* data, std::size_t len, header_list& out);

		/* the peer's SETTINGS_HEADER_TABLE_SIZE, applied via a dynamic table
		 * size update instruction in the next header block per RFC 7541 §4.2. */
		void set_max_dynamic_table_size(std::size_t max_size) noexcept { max_dynamic_table_size_ = max_size; }

	private:
		struct bit_reader;

		bool read_indexed(bit_reader& r, header_list& out);
		bool read_literal(bit_reader& r, header_list& out, int name_index_prefix_bits, bool add_to_table);
		bool read_size_update(bit_reader& r);
		bool read_string(bit_reader& r, std::string& out);
		bool read_integer(bit_reader& r, int prefix_bits, std::uint64_t& out);

		const std::pair<std::string, std::string>* lookup_indexed(std::uint64_t index) const;
		void add_to_dynamic_table(std::string name, std::string value);
		void evict_to_fit();

		std::size_t max_dynamic_table_size_;
		std::size_t dynamic_table_size_ = 0; // RFC 7541 §4.1: sum of (name.size()+value.size()+32) per entry
		std::deque<std::pair<std::string, std::string>> dynamic_table_; // front = most recently added (lowest index)
	};

	/**
	 * class hpack_encoder.
	 * one instance per HTTP/2 connection, mirroring the peer's decoder's
	 * dynamic table state. always emits literal-with-incremental-indexing or
	 * literal-without-indexing (never re-uses static/dynamic table indices
	 * for values, only for names it recognizes) — simpler and still fully
	 * RFC-compliant (a decoder must accept any valid representation choice),
	 * just not maximally compact. Huffman-encodes every string literal
	 * (always at least as small as the literal form, per RFC 7541 §5.2).
	 */
	class hpack_encoder {
	public:
		explicit hpack_encoder(std::size_t max_dynamic_table_size = 4096) noexcept
			: max_dynamic_table_size_(max_dynamic_table_size)
		{
		}

		void encode(const header_list& headers, std::string& out);

	private:
		void write_integer(std::string& out, std::uint64_t value, int prefix_bits, std::uint8_t prefix_pattern);
		void write_string(std::string& out, const std::string& value);

		std::size_t max_dynamic_table_size_;
	};

}
