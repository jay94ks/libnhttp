#include "nhttp/protocol/http_multipart.hpp"
#include "nhttp/protocol/http_mime_type.hpp"

#include <algorithm>
#include <cstring>

namespace nhttp::protocol {

	// NOTE: deliberately NOT in an anonymous namespace — multipart_reader's
	// `friend class multipart_part_stream;` names this exact class in the
	// nhttp::protocol namespace; an anonymous-namespace class of the same name
	// would be a distinct, unrelated type and the friendship wouldn't apply.
	class multipart_part_stream final : public io::stream {
	public:
		explicit multipart_part_stream(multipart_reader& reader) noexcept : reader_(reader) { }

		std::int64_t get_length() const override { return -1; }
		bool can_seek() const noexcept override { return false; }

		async::task<std::int64_t> seek(std::int64_t, io::seek_origin) override { co_return -1; }
		async::task<std::size_t> read(void* buf, std::size_t n) override { return reader_.read_part_body(buf, n); }
		async::task<std::size_t> write(const void*, std::size_t) override { co_return 0; }
		async::task<void> flush() override { co_return; }
		async::task<void> close() override { co_return; }

	private:
		multipart_reader& reader_;
	};

	std::optional<std::string> multipart_part::name() const {
		const std::string* disposition = headers.get(header_names::CONTENT_DISPOSITION);

		if (!disposition)
			return std::nullopt;

		const auto params = parse_header_parameters(*disposition);
		const auto it = params.find("name");

		if (it == params.end())
			return std::nullopt;

		return it->second;
	}

	std::optional<std::string> multipart_part::filename() const {
		const std::string* disposition = headers.get(header_names::CONTENT_DISPOSITION);

		if (!disposition)
			return std::nullopt;

		const auto params = parse_header_parameters(*disposition);
		const auto it = params.find("filename");

		if (it == params.end())
			return std::nullopt;

		return it->second;
	}

	multipart_reader::multipart_reader(std::shared_ptr<io::stream> source, std::string boundary)
		: source_(std::move(source)), boundary_marker_("\r\n--" + boundary)
	{
		// a synthetic leading CRLF lets the very first delimiter (which has no
		// real preceding CRLF in the body) be found by the same "\r\n--boundary"
		// search used for every subsequent part — see CLAUDE.md/CONCEPTS.md notes
		// on why this parser is written this way.
		buffer_ = "\r\n";
	}

	async::task<bool> multipart_reader::fill_more() {
		if (eof_)
			co_return false;

		char chunk[4096];
		const std::size_t n = co_await source_->read(chunk, sizeof(chunk));

		if (n == 0) {
			eof_ = true;
			co_return false;
		}

		buffer_.append(chunk, n);
		co_return true;
	}

	async::task<bool> multipart_reader::ensure_delimiter_or_eof() {
		for (;;) {
			const std::size_t pos = buffer_.find(boundary_marker_);

			if (pos != std::string::npos) {
				delimiter_pos_ = pos;
				co_return true;
			}

			if (eof_) {
				delimiter_pos_ = std::string::npos;
				co_return false;
			}

			co_await fill_more();
		}
	}

	async::task<bool> multipart_reader::read_header_line(std::string& out) {
		for (;;) {
			const std::size_t nl = buffer_.find('\n');

			if (nl != std::string::npos) {
				std::size_t len = nl;

				if (len > 0 && buffer_[len - 1] == '\r')
					--len;

				out.assign(buffer_, 0, len);
				buffer_.erase(0, nl + 1);
				co_return true;
			}

			if (eof_)
				co_return false;

			co_await fill_more();
		}
	}

	async::task<std::size_t> multipart_reader::read_part_body(void* buf, std::size_t n) {
		if (!part_active_)
			co_return 0;

		for (;;) {
			const std::size_t pos = buffer_.find(boundary_marker_);

			if (pos != std::string::npos) {
				const std::size_t to_copy = std::min(n, pos);

				if (to_copy > 0) {
					std::memcpy(buf, buffer_.data(), to_copy);
					buffer_.erase(0, to_copy);
					co_return to_copy;
				}

				part_active_ = false;
				co_return 0;
			}

			if (buffer_.size() > boundary_marker_.size()) {
				const std::size_t safe = buffer_.size() - (boundary_marker_.size() - 1);
				const std::size_t to_copy = std::min(n, safe);

				if (to_copy > 0) {
					std::memcpy(buf, buffer_.data(), to_copy);
					buffer_.erase(0, to_copy);
					co_return to_copy;
				}
			}

			if (eof_) {
				const std::size_t to_copy = std::min(n, buffer_.size());

				if (to_copy > 0) {
					std::memcpy(buf, buffer_.data(), to_copy);
					buffer_.erase(0, to_copy);
					co_return to_copy;
				}

				part_active_ = false;
				co_return 0;
			}

			co_await fill_more();
		}
	}

	async::task<std::optional<multipart_part>> multipart_reader::next_part() {
		if (finished_)
			co_return std::nullopt;

		if (part_active_) {
			char scratch[512];

			while (part_active_) {
				if (co_await read_part_body(scratch, sizeof(scratch)) == 0)
					break;
			}
		}

		if (!co_await ensure_delimiter_or_eof()) {
			finished_ = true;
			co_return std::nullopt;
		}

		buffer_.erase(0, delimiter_pos_ + boundary_marker_.size());

		while (buffer_.size() < 2 && !eof_)
			co_await fill_more();

		if (buffer_.size() < 2 || buffer_.compare(0, 2, "--") == 0) {
			finished_ = true;
			co_return std::nullopt;
		}

		// expect "\r\n" after the boundary marker; consume it leniently.
		buffer_.erase(0, 2);

		multipart_part part;

		for (;;) {
			std::string line;

			if (!co_await read_header_line(line)) {
				finished_ = true;
				co_return std::nullopt;
			}

			if (line.empty())
				break;

			line += '\n';

			http_header h;

			if (http_header::try_parse(line.data(), line.size(), h) > 0)
				part.headers.add(std::move(h.name), std::move(h.value));
		}

		part_active_ = true;
		part.body = std::make_shared<multipart_part_stream>(*this);

		co_return part;
	}

}
