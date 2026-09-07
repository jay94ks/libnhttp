#include "nhttp/http2/hpack.hpp"
#include "hpack_tables.hpp"

#include <vector>

namespace nhttp::http2 {

	namespace {

		/* a binary trie built once from detail::huffman_table, used to decode
		 * bit-by-bit — simple and clearly correct rather than the fastest
		 * possible approach, appropriate for a first implementation. */
		class huffman_tree {
		public:
			huffman_tree() {
				nodes_.push_back(node{});

				for (int sym = 0; sym < 256; ++sym)
					insert(sym, detail::huffman_table[static_cast<std::size_t>(sym)].code, detail::huffman_table[static_cast<std::size_t>(sym)].bits);
			}

			/* false on an invalid code, or padding that isn't a valid prefix
			 * of the EOS code (RFC 7541 §5.2's decoding-error requirement). */
			bool decode(const std::uint8_t* data, std::size_t len, std::string& out) const {
				int cur = 0;
				int pending_bits = 0;
				bool pending_all_ones = true;

				for (std::size_t i = 0; i < len; ++i) {
					const std::uint8_t byte = data[i];

					for (int b = 7; b >= 0; --b) {
						const int bit = (byte >> b) & 1;
						const int next = nodes_[static_cast<std::size_t>(cur)].children[bit];

						if (next < 0)
							return false;

						cur = next;
						++pending_bits;

						if (bit == 0)
							pending_all_ones = false;

						if (nodes_[static_cast<std::size_t>(cur)].symbol >= 0) {
							out += static_cast<char>(nodes_[static_cast<std::size_t>(cur)].symbol);
							cur = 0;
							pending_bits = 0;
							pending_all_ones = true;
						}
					}
				}

				if (pending_bits >= 8)
					return false; // too much trailing padding to be legitimate.

				return pending_bits == 0 || pending_all_ones;
			}

		private:
			struct node {
				int symbol = -1;
				int children[2] = { -1, -1 };
			};

			void insert(int symbol, std::uint32_t code, int bits) {
				int cur = 0;

				for (int b = bits - 1; b >= 0; --b) {
					const int bit = (code >> b) & 1;

					if (nodes_[static_cast<std::size_t>(cur)].children[bit] < 0) {
						nodes_.push_back(node{});
						nodes_[static_cast<std::size_t>(cur)].children[bit] = static_cast<int>(nodes_.size()) - 1;
					}

					cur = nodes_[static_cast<std::size_t>(cur)].children[bit];
				}

				nodes_[static_cast<std::size_t>(cur)].symbol = symbol;
			}

			std::vector<node> nodes_;
		};

		const huffman_tree& huffman() {
			static const huffman_tree tree;
			return tree;
		}

	}

	// ------------------------------------------------------------- decoder

	struct hpack_decoder::bit_reader {
		const std::uint8_t* data;
		std::size_t len;
		std::size_t pos = 0;

		bool at_end() const noexcept { return pos >= len; }
		std::uint8_t peek() const noexcept { return data[pos]; }
		std::uint8_t take() noexcept { return data[pos++]; }
	};

	bool hpack_decoder::read_integer(bit_reader& r, int prefix_bits, std::uint64_t& out) {
		// caller has already consumed the first byte (it shares bits with the
		// instruction-type marker); re-read it via the byte just before pos.
		const std::uint8_t first = r.data[r.pos - 1];
		const std::uint8_t mask = static_cast<std::uint8_t>((1u << prefix_bits) - 1);
		std::uint64_t value = first & mask;

		if (value < mask) {
			out = value;
			return true;
		}

		std::uint64_t m = 0;

		for (;;) {
			if (r.at_end())
				return false;

			const std::uint8_t b = r.take();
			value += static_cast<std::uint64_t>(b & 0x7f) << m;
			m += 7;

			if (!(b & 0x80))
				break;

			if (m > 63)
				return false; // absurd/overflowing integer — reject rather than wrap.
		}

		out = value;
		return true;
	}

	bool hpack_decoder::read_string(bit_reader& r, std::string& out) {
		if (r.at_end())
			return false;

		const std::uint8_t first = r.take();
		const bool is_huffman = (first & 0x80) != 0;
		std::uint64_t length = 0;

		if (!read_integer(r, 7, length))
			return false;

		if (length > r.len - r.pos)
			return false;

		const auto ulen = static_cast<std::size_t>(length);

		if (is_huffman) {
			out.clear();

			if (!huffman().decode(r.data + r.pos, ulen, out))
				return false;
		}
		else {
			out.assign(reinterpret_cast<const char*>(r.data + r.pos), ulen);
		}

		r.pos += ulen;
		return true;
	}

	const std::pair<std::string, std::string>* hpack_decoder::lookup_indexed(std::uint64_t index) const {
		if (index == 0)
			return nullptr;

		if (index <= detail::static_table.size()) {
			// detail::static_table holds string_views into static storage;
			// materialize on demand into a thread-local so callers can treat
			// this uniformly with a dynamic-table pointer.
			static thread_local std::pair<std::string, std::string> scratch;
			const auto& entry = detail::static_table[static_cast<std::size_t>(index - 1)];
			scratch = { std::string(entry.first), std::string(entry.second) };
			return &scratch;
		}

		const std::size_t dyn_index = static_cast<std::size_t>(index) - detail::static_table.size() - 1;

		if (dyn_index >= dynamic_table_.size())
			return nullptr;

		return &dynamic_table_[dyn_index];
	}

	void hpack_decoder::add_to_dynamic_table(std::string name, std::string value) {
		const std::size_t entry_size = name.size() + value.size() + 32;

		dynamic_table_.emplace_front(std::move(name), std::move(value));
		dynamic_table_size_ += entry_size;
		evict_to_fit();
	}

	void hpack_decoder::evict_to_fit() {
		while (dynamic_table_size_ > max_dynamic_table_size_ && !dynamic_table_.empty()) {
			const auto& back = dynamic_table_.back();
			dynamic_table_size_ -= back.first.size() + back.second.size() + 32;
			dynamic_table_.pop_back();
		}
	}

	bool hpack_decoder::read_size_update(bit_reader& r) {
		std::uint64_t new_size = 0;

		if (!read_integer(r, 5, new_size))
			return false;

		if (new_size > max_dynamic_table_size_)
			return false; // may not exceed the bound set via set_max_dynamic_table_size.

		max_dynamic_table_size_ = static_cast<std::size_t>(new_size);
		evict_to_fit();
		return true;
	}

	bool hpack_decoder::decode(const std::uint8_t* data, std::size_t len, header_list& out) {
		bit_reader r{ data, len, 0 };

		while (!r.at_end()) {
			const std::uint8_t first = r.peek();

			if (first & 0x80) {
				r.take();
				std::uint64_t index = 0;

				if (!read_integer(r, 7, index) || index == 0)
					return false;

				const auto* entry = lookup_indexed(index);

				if (!entry)
					return false;

				out.push_back({ entry->first, entry->second });
			}
			else if ((first & 0xc0) == 0x40) {
				r.take();
				std::uint64_t name_index = 0;

				if (!read_integer(r, 6, name_index))
					return false;

				std::string name, value;

				if (name_index == 0) {
					if (!read_string(r, name))
						return false;
				}
				else {
					const auto* entry = lookup_indexed(name_index);

					if (!entry)
						return false;

					name = entry->first;
				}

				if (!read_string(r, value))
					return false;

				out.push_back({ name, value });
				add_to_dynamic_table(std::move(name), std::move(value));
			}
			else if ((first & 0xe0) == 0x20) {
				r.take();

				if (!read_size_update(r))
					return false;
			}
			else {
				// 0000xxxx (without indexing) or 0001xxxx (never indexed) —
				// identical parse; the never-indexed distinction only matters
				// to a re-encoding proxy, which this decoder isn't.
				r.take();
				std::uint64_t name_index = 0;

				if (!read_integer(r, 4, name_index))
					return false;

				std::string name, value;

				if (name_index == 0) {
					if (!read_string(r, name))
						return false;
				}
				else {
					const auto* entry = lookup_indexed(name_index);

					if (!entry)
						return false;

					name = entry->first;
				}

				if (!read_string(r, value))
					return false;

				out.push_back({ std::move(name), std::move(value) });
			}
		}

		return true;
	}

	// ------------------------------------------------------------- encoder

	void hpack_encoder::write_integer(std::string& out, std::uint64_t value, int prefix_bits, std::uint8_t prefix_pattern) {
		const std::uint8_t max_prefix = static_cast<std::uint8_t>((1u << prefix_bits) - 1);

		if (value < max_prefix) {
			out += static_cast<char>(static_cast<std::uint8_t>(prefix_pattern | static_cast<std::uint8_t>(value)));
			return;
		}

		out += static_cast<char>(static_cast<std::uint8_t>(prefix_pattern | max_prefix));
		value -= max_prefix;

		while (value >= 128) {
			out += static_cast<char>(static_cast<std::uint8_t>((value % 128) | 0x80));
			value /= 128;
		}

		out += static_cast<char>(static_cast<std::uint8_t>(value));
	}

	void hpack_encoder::write_string(std::string& out, const std::string& value) {
		std::string encoded;
		std::uint64_t bit_buffer = 0;
		int bit_count = 0;

		for (const char raw_c : value) {
			const auto c = static_cast<unsigned char>(raw_c);
			const auto& hc = detail::huffman_table[c];
			bit_buffer = (bit_buffer << hc.bits) | hc.code;
			bit_count += hc.bits;

			while (bit_count >= 8) {
				bit_count -= 8;
				encoded += static_cast<char>(static_cast<std::uint8_t>((bit_buffer >> bit_count) & 0xff));
			}

			// keep only the still-pending low bits — without this, bit_buffer
			// grows unboundedly across a long string and eventually overflows
			// past 64 bits, corrupting later bytes.
			bit_buffer &= (bit_count == 0) ? 0 : ((std::uint64_t{ 1 } << bit_count) - 1);
		}

		if (bit_count > 0) {
			const auto pad = static_cast<std::uint8_t>((bit_buffer << (8 - bit_count)) | (0xffu >> bit_count));
			encoded += static_cast<char>(pad);
		}

		write_integer(out, encoded.size(), 7, 0x80); // top bit set: Huffman-coded.
		out += encoded;
	}

	void hpack_encoder::encode(const header_list& headers, std::string& out) {
		for (const header_field& h : headers) {
			// literal without indexing, literal name — simple and always
			// correct; see the class doc comment for why this doesn't bother
			// matching static-table names or using dynamic-table indexing.
			out += static_cast<char>(0x00);
			write_string(out, h.name);
			write_string(out, h.value);
		}
	}

}
