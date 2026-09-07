#pragma once

#include <memory>
#include <string>

// forward-declare rather than pulling <openssl/ssl.h> into every translation
// unit that merely mentions a tls_context (e.g. listener.hpp).
typedef struct ssl_ctx_st SSL_CTX;

namespace nhttp::tls {

	/**
	 * class tls_context.
	 * owns an OpenSSL SSL_CTX configured as a TLS server (certificate chain +
	 * private key loaded from PEM files, TLS >= 1.2). one instance is shared
	 * across every connection accepted on a given TLS-enabled listener endpoint
	 * (see server::listener::listen_tls) — it's read-only after construction,
	 * so sharing it across worker threads is safe.
	 */
	class tls_context {
	public:
		~tls_context();

		tls_context(const tls_context&) = delete;
		tls_context(tls_context&&) = delete;

	public:
		/* nullptr on failure (bad paths, key/cert mismatch, etc). */
		static std::shared_ptr<tls_context> create_server(const std::string& cert_chain_file, const std::string& private_key_file);

		/**
		 * a client-mode context (used by reverse_proxy for an HTTPS upstream).
		 * `verify_peer` defaults to on (the system default CA store, via
		 * SSL_CTX_set_default_verify_paths) — pass false only for a trusted
		 * internal/self-signed upstream, never for anything reachable outside
		 * the operator's own control.
		 */
		static std::shared_ptr<tls_context> create_client(bool verify_peer = true);

		SSL_CTX* native() const noexcept { return ctx_; }

	private:
		explicit tls_context(SSL_CTX* ctx) noexcept : ctx_(ctx) { }

		SSL_CTX* ctx_;
	};

}
