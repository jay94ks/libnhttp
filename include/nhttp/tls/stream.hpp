#pragma once

#include "context.hpp"
#include "../io/stream.hpp"

// forward-declare rather than pulling <openssl/ssl.h> into every translation
// unit that merely mentions a tls_stream.
typedef struct ssl_st SSL;
typedef struct bio_st BIO;

namespace nhttp::tls {

	/**
	 * class tls_stream.
	 * a TLS-terminating decorator over any io::stream (transport-agnostic on
	 * purpose, per CONCEPTS.md/CLAUDE.md's seam — this works over a TCP
	 * socket_stream today and would work unchanged over a future QUIC-backed
	 * stream). drives OpenSSL through a pair of memory BIOs instead of handing
	 * OpenSSL the raw socket, so every SSL_read/SSL_write/handshake step that
	 * would otherwise block is instead an explicit "pull more ciphertext from
	 * the inner stream" or "push ciphertext to the inner stream" step driven by
	 * our own coroutines — the reactor thread never blocks on network I/O
	 * inside OpenSSL.
	 */
	class tls_stream final : public io::stream {
	public:
		tls_stream(std::shared_ptr<io::stream> inner, std::shared_ptr<tls_context> ctx);
		~tls_stream() override;

		tls_stream(const tls_stream&) = delete;
		tls_stream(tls_stream&&) = delete;

	public:
		/* performs the server-side handshake. must be awaited (and must
		 * succeed) before read()/write() are called. */
		async::task<bool> accept();

	public:
		std::int64_t get_length() const override { return -1; }
		bool can_seek() const noexcept override { return false; }

		async::task<std::int64_t> seek(std::int64_t offset, io::seek_origin origin) override;
		async::task<std::size_t> read(void* buf, std::size_t n) override;
		async::task<std::size_t> write(const void* buf, std::size_t n) override;
		async::task<void> flush() override;
		async::task<void> close() override;

	private:
		async::task<void> flush_wbio();
		async::task<bool> feed_rbio_from_network();

		std::shared_ptr<io::stream> inner_;
		std::shared_ptr<tls_context> ctx_;
		SSL* ssl_ = nullptr;
		BIO* rbio_ = nullptr; // ownership transferred to `ssl_` via SSL_set_bio
		BIO* wbio_ = nullptr; // ownership transferred to `ssl_` via SSL_set_bio
	};

}
