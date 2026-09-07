#pragma once

#include <cstdint>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <vector>

namespace nhttp::platform {

	enum class ip_version { v4, v6 };

	/**
	 * class ip_address.
	 * an IPv4 or IPv6 address (never both at once).
	 */
	class ip_address {
	public:
		ip_address() noexcept;

		static ip_address from_v4(const in_addr& addr) noexcept;
		static ip_address from_v6(const in6_addr& addr) noexcept;

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

		/* preconditions: is_v4() / is_v6() respectively. */
		const in_addr& as_v4() const noexcept { return v4_; }
		const in6_addr& as_v6() const noexcept { return v6_; }

		std::string to_string() const;

	private:
		ip_version version_;
		in_addr v4_;
		in6_addr v6_;
	};

	/**
	 * class endpoint.
	 * an ip_address + port pair, convertible to/from a native sockaddr.
	 */
	class endpoint {
	public:
		endpoint(ip_address address, std::uint16_t port) noexcept;

		const ip_address& address() const noexcept { return address_; }
		std::uint16_t port() const noexcept { return port_; }

		std::string to_string() const;

		/* fills `out` and returns the sockaddr length to pass to bind()/connect(). */
		socklen_t to_sockaddr(sockaddr_storage& out) const noexcept;

		static std::optional<endpoint> from_sockaddr(const sockaddr* addr, socklen_t len) noexcept;

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
