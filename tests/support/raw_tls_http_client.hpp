#pragma once

// a minimal, self-contained blocking HTTPS client used only to drive a real
// nhttp::server::listener's TLS endpoint end-to-end in integration tests —
// deliberately independent of nhttp's own tls:: code, so a bug shared between
// the server and this client wouldn't hide a failure. header-only test-support
// code, not part of the library. uses OpenSSL directly in simple blocking mode
// (no WANT_READ/WANT_WRITE handling needed, since the underlying socket is
// left in its default blocking mode) — much simpler than the async server-side
// tls_stream, which is exactly the point of having an independent client.

#include "nhttp/platform/socket.hpp"
#include "nhttp/protocol/http_header.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cstdlib>
#include <string>

namespace nhttp_test {

	class raw_tls_http_client {
	public:
		raw_tls_http_client(nhttp::platform::ip_version version, std::uint16_t port) {
			sock_ = nhttp::platform::socket_handle::create(version, nhttp::platform::transport::tcp);
			const nhttp::platform::ip_address addr = version == nhttp::platform::ip_version::v4
				? nhttp::platform::ip_address::loopback_v4()
				: nhttp::platform::ip_address::loopback_v6();

			if (sock_.connect_raw(nhttp::platform::endpoint(addr, port)) != nhttp::platform::connect_result::connected)
				return;

			ctx_ = SSL_CTX_new(TLS_client_method());

			if (!ctx_)
				return;

			SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr); // test cert is self-signed.

			ssl_ = SSL_new(ctx_);
			SSL_set_fd(ssl_, sock_.native_handle());
			ok_ = SSL_connect(ssl_) == 1;
		}

		~raw_tls_http_client() {
			if (ssl_) {
				SSL_shutdown(ssl_);
				SSL_free(ssl_);
			}

			if (ctx_)
				SSL_CTX_free(ctx_);

			sock_.close();
		}

		bool connected() const noexcept { return ok_; }

		struct response {
			int status = 0;
			std::string headers;
			std::string body;

			std::string header(const std::string& name) const { return raw_tls_http_client::find_header(headers, name); }
		};

		response send(const std::string& request_bytes) {
			std::size_t sent = 0;

			while (sent < request_bytes.size()) {
				const int n = SSL_write(ssl_, request_bytes.data() + sent, static_cast<int>(request_bytes.size() - sent));
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

			const std::string cl = find_header(out.headers, "Content-Length");
			const std::size_t content_length = cl.empty() ? 0 : static_cast<std::size_t>(std::atol(cl.c_str()));

			while (buffered_.size() < content_length) {
				if (!fill())
					break;
			}

			out.body = buffered_.substr(0, content_length);
			buffered_.erase(0, std::min(content_length, buffered_.size()));

			return out;
		}

	private:
		bool fill() {
			char chunk[4096];
			const int n = SSL_read(ssl_, chunk, sizeof(chunk));

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

		nhttp::platform::socket_handle sock_;
		SSL_CTX* ctx_ = nullptr;
		SSL* ssl_ = nullptr;
		std::string buffered_;
		bool ok_ = false;
	};

}
