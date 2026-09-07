#include "nhttp/server/extensions/static_content.hpp"
#include "nhttp/protocol/http_date.hpp"
#include "nhttp/protocol/http_method.hpp"
#include "nhttp/io/range_stream.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace nhttp::server {

	std::string make_etag(std::time_t mtime, std::int64_t size) {
		char buf[48];
		std::snprintf(buf, sizeof(buf), "\"nh-%llx-%llx\"",
			static_cast<unsigned long long>(mtime), static_cast<unsigned long long>(size));
		return std::string(buf);
	}

	std::optional<std::string> qualify_relative_path(std::string_view path) {
		std::vector<std::string_view> stack;
		std::size_t pos = 0;

		while (pos <= path.size()) {
			const std::size_t slash = path.find('/', pos);
			const std::string_view seg = path.substr(pos, slash == std::string_view::npos ? std::string_view::npos : slash - pos);

			if (seg == "..") {
				if (stack.empty())
					return std::nullopt;

				stack.pop_back();
			}
			else if (!seg.empty() && seg != ".") {
				stack.push_back(seg);
			}

			if (slash == std::string_view::npos)
				break;

			pos = slash + 1;
		}

		std::string out;

		for (const std::string_view& s : stack) {
			if (!out.empty())
				out += '/';

			out += s;
		}

		return out;
	}

	namespace {

		bool etag_list_contains(const std::string& list, const std::string& etag) noexcept {
			std::size_t pos = 0;

			while (pos < list.size()) {
				const std::size_t comma = list.find(',', pos);
				std::string_view tok(list.data() + pos, (comma == std::string::npos ? list.size() : comma) - pos);

				while (!tok.empty() && tok.front() == ' ')
					tok.remove_prefix(1);

				while (!tok.empty() && tok.back() == ' ')
					tok.remove_suffix(1);

				if (tok == etag)
					return true;

				if (comma == std::string::npos)
					break;

				pos = comma + 1;
			}

			return false;
		}

		struct range_request {
			bool present = false;
			bool satisfiable = true;
			std::int64_t begin = 0;
			std::int64_t end = 0; // inclusive
		};

		bool parse_digits(std::string_view digits, std::int64_t& out) noexcept {
			out = 0;

			for (const char c : digits) {
				if (c < '0' || c > '9')
					return false;

				out = out * 10 + (c - '0');
			}

			return true;
		}

		range_request parse_range_header(const std::string* range_value, std::int64_t content_size) noexcept {
			range_request out;

			if (!range_value || content_size <= 0)
				return out;

			std::string_view v(*range_value);

			if (v.substr(0, 6) != "bytes=")
				return out;

			v.remove_prefix(6);

			const std::size_t comma = v.find(',');

			if (comma != std::string_view::npos)
				v = v.substr(0, comma);

			const std::size_t dash = v.find('-');

			if (dash == std::string_view::npos)
				return out;

			out.present = true;

			const std::string_view start_s = v.substr(0, dash);
			const std::string_view end_s = v.substr(dash + 1);

			if (start_s.empty()) {
				std::int64_t suffix_len = 0;

				if (end_s.empty() || !parse_digits(end_s, suffix_len) || suffix_len <= 0) {
					out.satisfiable = false;
					return out;
				}

				out.begin = std::max<std::int64_t>(0, content_size - suffix_len);
				out.end = content_size - 1;
			}
			else {
				std::int64_t begin = 0;

				if (!parse_digits(start_s, begin)) {
					out.satisfiable = false;
					return out;
				}

				std::int64_t end = content_size - 1;

				if (!end_s.empty() && !parse_digits(end_s, end)) {
					out.satisfiable = false;
					return out;
				}

				out.begin = begin;
				out.end = std::min(end, content_size - 1);
			}

			if (out.begin < 0 || out.begin > out.end || out.begin >= content_size)
				out.satisfiable = false;

			return out;
		}

	}

	async::task<response> serve_stream_conditionally(
		const request& req, std::shared_ptr<io::stream> content,
		std::int64_t size, std::time_t mtime, std::string_view mime, const std::string& etag)
	{
		const std::string last_modified = protocol::format_http_date(mtime);
		const bool is_head = req.method() == protocol::http_method::HEAD();

		auto finalize = [&](response r) {
			r.headers.set(std::string(protocol::header_names::ACCEPT_RANGES), "bytes");
			r.headers.set(std::string(protocol::header_names::ETAG), etag);
			r.headers.set(std::string(protocol::header_names::LAST_MODIFIED), last_modified);
			r.headers.set(std::string(protocol::header_names::CACHE_CONTROL),
				req.headers.isset(protocol::header_names::AUTHORIZATION) ? "private, must-revalidate" : "public, must-revalidate");

			if (is_head)
				r.body = nullptr;

			return r;
		};

		if (const std::string* if_match = req.headers.get(protocol::header_names::IF_MATCH)) {
			if (*if_match != "*" && !etag_list_contains(*if_match, etag))
				co_return finalize(make_response(412));
		}

		bool not_modified = false;

		if (const std::string* inm = req.headers.get(protocol::header_names::IF_NONE_MATCH)) {
			if (*inm == "*" || etag_list_contains(*inm, etag)) {
				if (req.method() == protocol::http_method::GET() || is_head)
					not_modified = true;
				else
					co_return finalize(make_response(412));
			}
		}
		else if (const std::string* ims = req.headers.get(protocol::header_names::IF_MODIFIED_SINCE)) {
			const std::time_t since = protocol::parse_http_date(*ims);

			if (since != static_cast<std::time_t>(-1) && mtime <= since)
				not_modified = true;
		}

		if (not_modified)
			co_return finalize(make_response(304));

		const std::string* range_header = req.headers.get(protocol::header_names::RANGE);
		bool use_range = false;

		if (range_header) {
			bool if_range_ok = true;

			if (const std::string* ifr = req.headers.get(protocol::header_names::IF_RANGE)) {
				if (!ifr->empty() && ifr->front() == '"') {
					if_range_ok = (*ifr == etag);
				}
				else {
					const std::time_t d = protocol::parse_http_date(*ifr);
					if_range_ok = (d != static_cast<std::time_t>(-1) && d >= mtime);
				}
			}

			use_range = if_range_ok;
		}

		if (use_range) {
			const range_request rr = parse_range_header(range_header, size);

			if (!rr.satisfiable) {
				response r = make_response(416);
				r.headers.set(std::string(protocol::header_names::CONTENT_RANGE), "bytes */" + std::to_string(size));
				co_return finalize(std::move(r));
			}

			auto ranged = std::make_shared<io::range_stream>(content, rr.begin, rr.end + 1);
			response r = make_response(std::move(ranged), mime, rr.end - rr.begin + 1);
			r.status = protocol::http_status(206);
			r.headers.set(std::string(protocol::header_names::CONTENT_RANGE),
				"bytes " + std::to_string(rr.begin) + "-" + std::to_string(rr.end) + "/" + std::to_string(size));

			co_return finalize(std::move(r));
		}

		co_return finalize(make_response(std::move(content), mime, size));
	}

}
