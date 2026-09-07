#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nhttp::platform {

	enum class ip_version { v4, v6 };

	/**
	 * class ip_address.
	 * an IPv4 or IPv6 address (never both at once). stores raw address bytes in
	 * network byte order (matching in_addr/in6_addr's own layout, so platform
	 * .cpp files can convert with a plain memcpy) instead of naming any OS
	 * socket type here, so this header never needs <netinet/in.h>/<winsock2.h>.
	 */
	class ip_address {
	public:
		using v4_bytes_t = std::array<std::uint8_t, 4>;
		using v6_bytes_t = std::array<std::uint8_t, 16>;

	public:
		ip_address() noexcept;

		static ip_address from_v4_bytes(const v4_bytes_t& bytes) noexcept;
		static ip_address from_v6_bytes(const v6_bytes_t& bytes) noexcept;

		/* parses a dotted-decimal IPv4 or a colon-form IPv6 literal. no DNS lookups. */
		static std::optional<ip_address> parse(std::string_view text) noexcept;

		static ip_address any_v4() noexcept;
		static ip_address any_v6() noexcept;
		static ip_address loopback_v4() noexcept;
		static ip_address loopback_v6() noexcept;

	public:
		ip_version version() const noexcept { return version_; }
		bool is_v4() const noexcept { return version_ == ip_version::v4; }
		bool is_v6() const noexcept { return version_ == ip_version::v6; }

		/* preconditions: is_v4() / is_v6() respectively. network byte order. */
		const v4_bytes_t& v4_bytes() const noexcept { return v4_; }
		const v6_bytes_t& v6_bytes() const noexcept { return v6_; }

		std::string to_string() const;

	private:
		ip_version version_;
		v4_bytes_t v4_;
		v6_bytes_t v6_;
	};

	/**
	 * class endpoint.
	 * an ip_address + port pair. conversion to/from a native sockaddr is a
	 * platform implementation detail (see src/platform/posix|win32) — kept out
	 * of this public header on purpose, so it never names an OS socket type.
	 */
	class endpoint {
	public:
		endpoint(ip_address address, std::uint16_t port) noexcept;

		const ip_address& address() const noexcept { return address_; }
		std::uint16_t port() const noexcept { return port_; }

		std::string to_string() const;

	private:
		ip_address address_;
		std::uint16_t port_;
	};

	/**
	 * resolve() performs a blocking DNS lookup (getaddrinfo) for `host` and returns
	 * every A/AAAA result as an endpoint with the given port. blocking: only call
	 * this from startup/configuration code, or via the async thread_pool.
	 */
	std::vector<endpoint> resolve(std::string_view host, std::uint16_t port);

}
