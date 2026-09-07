#include "nhttp/tls/context.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace nhttp::tls {

	namespace {

		struct openssl_init {
			openssl_init() noexcept {
				OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
			}
		};

		void ensure_openssl_initialized() noexcept {
			static const openssl_init init;
			(void) init;
		}

	}

	tls_context::~tls_context() {
		SSL_CTX_free(ctx_);
	}

	std::shared_ptr<tls_context> tls_context::create_server(const std::string& cert_chain_file, const std::string& private_key_file) {
		ensure_openssl_initialized();

		SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());

		if (!ctx)
			return nullptr;

		SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
		SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);

		if (SSL_CTX_use_certificate_chain_file(ctx, cert_chain_file.c_str()) <= 0 ||
			SSL_CTX_use_PrivateKey_file(ctx, private_key_file.c_str(), SSL_FILETYPE_PEM) <= 0 ||
			SSL_CTX_check_private_key(ctx) <= 0)
		{
			SSL_CTX_free(ctx);
			return nullptr;
		}

		return std::shared_ptr<tls_context>(new tls_context(ctx));
	}

	std::shared_ptr<tls_context> tls_context::create_client(bool verify_peer) {
		ensure_openssl_initialized();

		SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());

		if (!ctx)
			return nullptr;

		SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
		SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);

		if (verify_peer) {
			SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);

			if (SSL_CTX_set_default_verify_paths(ctx) <= 0) {
				SSL_CTX_free(ctx);
				return nullptr;
			}
		}
		else {
			SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
		}

		return std::shared_ptr<tls_context>(new tls_context(ctx));
	}

}
