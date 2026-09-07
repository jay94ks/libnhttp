#pragma once

#include "vpath.hpp"
#include "../../platform/address.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace nhttp::server {

	/**
	 * one upstream server a reverse_proxy can forward to. `address` must
	 * already be a resolved endpoint (IP + port) — DNS resolution, if needed,
	 * is the caller's job (e.g. via platform::resolve() at setup time), kept
	 * out of the request path on purpose.
	 */
	struct upstream {
		explicit upstream(platform::endpoint ep) noexcept : address(std::move(ep)) { }

		platform::endpoint address;
		bool use_tls = false;

		/* SNI hostname + certificate-verification name when use_tls is set;
		 * defaults to address.address().to_string() (a bare IP) if left
		 * empty, which will fail verification against a real certificate —
		 * set this explicitly for any upstream with a real hostname. */
		std::string tls_sni_hostname;

		/* only meaningful when use_tls is set. defaults on (verify against
		 * the system CA store) — turn off only for a trusted internal/
		 * self-signed upstream, never for anything outside the operator's
		 * own control. */
		bool verify_tls_cert = true;
	};

	/**
	 * class reverse_proxy.
	 * mounts one or more upstream servers under a URL path prefix (the same
	 * vpath URL-prefix scoping router is built on — see CONCEPTS.md §4-5),
	 * relaying HTTP/1.1 requests/responses verbatim to whichever upstream a
	 * plain round-robin picks, including a WebSocket upgrade (passed through
	 * as a raw byte splice once the upstream answers 101).
	 *
	 * known simplifications, in the same spirit as Phase 4's logged ones: no
	 * upstream connection pooling (a fresh connection per proxied request or
	 * upgrade), and no active health checking (a down upstream just fails
	 * that one request) — both reasonable follow-ups, neither required for
	 * correct behavior today.
	 */
	class reverse_proxy final : public vpath {
	public:
		reverse_proxy(std::string mount_path, std::vector<upstream> upstreams, std::uint32_t prio = 0x80000000u);

	protected:
		async::task<std::optional<response>> on_handle(request& req) override;

	private:
		const upstream& pick_upstream() noexcept;

		std::vector<upstream> upstreams_;
		std::atomic<std::size_t> next_upstream_{ 0 };
	};

	inline std::shared_ptr<reverse_proxy> reverse_proxy_for(std::string mount_path, std::vector<upstream> upstreams) {
		return std::make_shared<reverse_proxy>(std::move(mount_path), std::move(upstreams));
	}

}
