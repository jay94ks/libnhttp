#pragma once

// a minimal, self-contained blocking HTTP/1.1 client used only to drive a real
// nhttp::server::listener end-to-end in integration tests — deliberately
// independent of nhttp's own protocol/ code, so a bug shared between the
// server and this parser wouldn't hide a failure. header-only since it's
// test-support code, not part of the library.

#include "nhttp/platform/socket.hpp"
#include "nhttp/protocol/http_header.hpp"

#include <cstdlib>
#include <string>

namespace nhttp_test {

	class raw_http_client {
	public:
		raw_http_client(nhttp::platform::ip_version version, std::uint16_t port) {
			sock_ = nhttp::platform::socket_handle::create(version, nhttp::platform::transport::tcp);
			const nhttp::platform::ip_address addr = version == nhttp::platform::ip_version::v4
				? nhttp::platform::ip_address::loopback_v4()
				: nhttp::platform::ip_address::loopback_v6();
			ok_ = sock_.connect(nhttp::platform::endpoint(addr, port)) == nhttp::platform::connect_result::connected;
		}

		~raw_http_client() { sock_.close(); }

		bool connected() const noexcept { return ok_; }

		struct response {
			int status = 0;
			std::string headers;
			std::string body;

			std::string header(const std::string& name) const { return raw_http_client::find_header(headers, name); }
		};

		response send(const std::string& request_bytes) {
			std::size_t sent = 0;

			while (sent < request_bytes.size()) {
				const std::int64_t n = sock_.write(request_bytes.data() + sent, request_bytes.size() - sent);
				if (n <= 0) break;
				sent += static_cast<std::size_t>(n);
			}

			for (;;) {
				if (buffered_.find("\r\n\r\n") != std::string::npos)
					break;

				if (!fill())
					break;
			}

			const std::size_t header_end = buffered_.find("\r\n\r\n");
			response out;
			out.headers = buffered_.substr(0, header_end);
			buffered_.erase(0, header_end + 4);

			const std::size_t sp1 = out.headers.find(' ');
			const std::size_t sp2 = out.headers.find(' ', sp1 + 1);
			out.status = std::atoi(out.headers.substr(sp1 + 1, sp2 - sp1 - 1).c_str());

			if (find_header(out.headers, "Transfer-Encoding") == "chunked") {
				out.body = read_dechunked();
			}
			else {
				const std::string cl = find_header(out.headers, "Content-Length");
				const std::size_t content_length = cl.empty() ? 0 : static_cast<std::size_t>(std::atol(cl.c_str()));

				while (buffered_.size() < content_length) {
					if (!fill())
						break;
				}

				out.body = buffered_.substr(0, content_length);
				buffered_.erase(0, std::min(content_length, buffered_.size()));
			}

			return out;
		}

	private:
		bool fill() {
			char chunk[4096];
			const std::int64_t n = sock_.read(chunk, sizeof(chunk));

			if (n <= 0)
				return false;

			buffered_.append(chunk, static_cast<std::size_t>(n));
			return true;
		}

		static std::string find_header(const std::string& headers, const std::string& name) {
			std::size_t pos = 0;

			while (pos < headers.size()) {
				const std::size_t line_end = headers.find("\r\n", pos);
				const std::string line = headers.substr(pos, line_end == std::string::npos ? std::string::npos : line_end - pos);
				const std::size_t colon = line.find(':');

				if (colon != std::string::npos && nhttp::protocol::header_name_equals(line.substr(0, colon), name)) {
					std::string value = line.substr(colon + 1);

					while (!value.empty() && value.front() == ' ')
						value.erase(value.begin());

					return value;
				}

				if (line_end == std::string::npos)
					break;

				pos = line_end + 2;
			}

			return std::string();
		}

		std::string read_dechunked() {
			std::string out;

			for (;;) {
				std::size_t nl = buffered_.find("\r\n");

				while (nl == std::string::npos) {
					if (!fill())
						return out;

					nl = buffered_.find("\r\n");
				}

				const std::string size_line = buffered_.substr(0, nl);
				const std::size_t size = static_cast<std::size_t>(std::strtoul(size_line.c_str(), nullptr, 16));
				buffered_.erase(0, nl + 2);

				if (size == 0) {
					buffered_.erase(0, std::min<std::size_t>(2, buffered_.size()));
					return out;
				}

				while (buffered_.size() < size + 2) {
					if (!fill())
						return out;
				}

				out.append(buffered_, 0, size);
				buffered_.erase(0, size + 2);
			}
		}

		nhttp::platform::socket_handle sock_;
		std::string buffered_;
		bool ok_ = false;
	};

}
