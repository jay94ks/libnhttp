#include "nhttp/tls/stream.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace nhttp::tls {

	tls_stream::tls_stream(std::shared_ptr<io::stream> inner, std::shared_ptr<tls_context> ctx)
		: inner_(std::move(inner)), ctx_(std::move(ctx))
	{
		ssl_ = SSL_new(ctx_->native());
		rbio_ = BIO_new(BIO_s_mem());
		wbio_ = BIO_new(BIO_s_mem());

		// an empty memory BIO normally reports EOF; we want it to report
		// "would block" instead, since "no ciphertext available yet" just
		// means we haven't fed it more from the network yet.
		BIO_set_mem_eof_return(rbio_, -1);
		BIO_set_mem_eof_return(wbio_, -1);

		SSL_set_bio(ssl_, rbio_, wbio_); // ssl_ now owns both BIOs.
	}

	tls_stream::~tls_stream() {
		if (ssl_)
			SSL_free(ssl_); // also frees rbio_/wbio_.
	}

	async::task<void> tls_stream::flush_wbio() {
		char buf[4096];

		for (;;) {
			const int n = BIO_read(wbio_, buf, sizeof(buf));

			if (n <= 0)
				break;

			std::size_t written = 0;
			const std::size_t total = static_cast<std::size_t>(n);

			while (written < total)
				written += co_await inner_->write(buf + written, total - written);
		}
	}

	async::task<bool> tls_stream::feed_rbio_from_network() {
		char buf[4096];
		const std::size_t n = co_await inner_->read(buf, sizeof(buf));

		if (n == 0)
			co_return false;

		BIO_write(rbio_, buf, static_cast<int>(n));
		co_return true;
	}

	async::task<bool> tls_stream::accept() {
		SSL_set_accept_state(ssl_);

		for (;;) {
			const int rc = SSL_accept(ssl_);

			if (rc == 1) {
				co_await flush_wbio();
				co_return true;
			}

			const int err = SSL_get_error(ssl_, rc);
			co_await flush_wbio();

			if (err == SSL_ERROR_WANT_READ) {
				if (!co_await feed_rbio_from_network())
					co_return false;

				continue;
			}

			if (err == SSL_ERROR_WANT_WRITE)
				continue;

			co_return false;
		}
	}

	async::task<bool> tls_stream::connect(const std::string& sni_hostname) {
		SSL_set_connect_state(ssl_);

		// SSL_set_tlsext_host_name(ssl_, sni_hostname.c_str()) is a macro that
		// expands to an old-style C cast, which -Wold-style-cast flags at this
		// call site — call the underlying SSL_ctrl directly with a proper C++
		// cast instead of disabling the warning.
		SSL_ctrl(ssl_, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name,
			static_cast<void*>(const_cast<char*>(sni_hostname.c_str())));

		// enables hostname verification against the certificate's SAN/CN when
		// the context was created with verify_peer=true (SSL_VERIFY_PEER) —
		// without this, SSL_VERIFY_PEER only checks the chain, not the name.
		SSL_set1_host(ssl_, sni_hostname.c_str());

		for (;;) {
			const int rc = SSL_connect(ssl_);

			if (rc == 1) {
				co_await flush_wbio();
				co_return true;
			}

			const int err = SSL_get_error(ssl_, rc);
			co_await flush_wbio();

			if (err == SSL_ERROR_WANT_READ) {
				if (!co_await feed_rbio_from_network())
					co_return false;

				continue;
			}

			if (err == SSL_ERROR_WANT_WRITE)
				continue;

			co_return false;
		}
	}

	async::task<std::int64_t> tls_stream::seek(std::int64_t, io::seek_origin) {
		co_return -1;
	}

	async::task<std::size_t> tls_stream::read(void* buf, std::size_t n) {
		for (;;) {
			const int rc = SSL_read(ssl_, buf, static_cast<int>(n));

			if (rc > 0) {
				co_await flush_wbio();
				co_return static_cast<std::size_t>(rc);
			}

			const int err = SSL_get_error(ssl_, rc);
			co_await flush_wbio();

			if (err == SSL_ERROR_WANT_READ) {
				if (!co_await feed_rbio_from_network())
					co_return 0;

				continue;
			}

			if (err == SSL_ERROR_WANT_WRITE)
				continue;

			// SSL_ERROR_ZERO_RETURN (clean TLS close_notify) and any other
			// error both surface to the caller as end-of-stream.
			co_return 0;
		}
	}

	async::task<std::size_t> tls_stream::write(const void* buf, std::size_t n) {
		for (;;) {
			const int rc = SSL_write(ssl_, buf, static_cast<int>(n));

			if (rc > 0) {
				co_await flush_wbio();
				co_return static_cast<std::size_t>(rc);
			}

			const int err = SSL_get_error(ssl_, rc);
			co_await flush_wbio();

			if (err == SSL_ERROR_WANT_READ) {
				if (!co_await feed_rbio_from_network())
					co_return 0;

				continue;
			}

			if (err == SSL_ERROR_WANT_WRITE)
				continue;

			co_return 0;
		}
	}

	async::task<void> tls_stream::flush() {
		co_return;
	}

	async::task<void> tls_stream::close() {
		if (ssl_) {
			SSL_shutdown(ssl_); // best-effort; don't loop waiting for the peer's close_notify.
			co_await flush_wbio();
		}

		co_await inner_->close();
	}

}
