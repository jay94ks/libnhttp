#include "nhttp/platform/address.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>

namespace nhttp::platform {

	ip_address::ip_address() noexcept
		: version_(ip_version::v4), v4_{}, v6_{}
	{
	}

	ip_address ip_address::from_v4(const in_addr& addr) noexcept {
		ip_address out;
		out.version_ = ip_version::v4;
		out.v4_ = addr;
		return out;
	}

	ip_address ip_address::from_v6(const in6_addr& addr) noexcept {
		ip_address out;
		out.version_ = ip_version::v6;
		out.v6_ = addr;
		return out;
	}

	std::optional<ip_address> ip_address::parse(std::string_view text) noexcept {
		std::string owned(text);

		in_addr v4{};
		if (::inet_pton(AF_INET, owned.c_str(), &v4) == 1)
			return from_v4(v4);

		in6_addr v6{};
		if (::inet_pton(AF_INET6, owned.c_str(), &v6) == 1)
			return from_v6(v6);

		return std::nullopt;
	}

	ip_address ip_address::any_v4() noexcept {
		in_addr addr{};
		addr.s_addr = htonl(INADDR_ANY);
		return from_v4(addr);
	}

	ip_address ip_address::any_v6() noexcept {
		return from_v6(in6addr_any);
	}

	ip_address ip_address::loopback_v4() noexcept {
		in_addr addr{};
		addr.s_addr = htonl(INADDR_LOOPBACK);
		return from_v4(addr);
	}

	ip_address ip_address::loopback_v6() noexcept {
		return from_v6(in6addr_loopback);
	}

	std::string ip_address::to_string() const {
		char buf[INET6_ADDRSTRLEN] = { 0 };

		if (is_v4())
			::inet_ntop(AF_INET, &v4_, buf, sizeof(buf));
		else
			::inet_ntop(AF_INET6, &v6_, buf, sizeof(buf));

		return std::string(buf);
	}

	endpoint::endpoint(ip_address address, std::uint16_t port) noexcept
		: address_(address), port_(port)
	{
	}

	std::string endpoint::to_string() const {
		if (address_.is_v6())
			return "[" + address_.to_string() + "]:" + std::to_string(port_);

		return address_.to_string() + ":" + std::to_string(port_);
	}

	socklen_t endpoint::to_sockaddr(sockaddr_storage& out) const noexcept {
		std::memset(&out, 0, sizeof(out));

		if (address_.is_v4()) {
			sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&out);
			sin->sin_family = AF_INET;
			sin->sin_addr = address_.as_v4();
			sin->sin_port = htons(port_);
			return sizeof(sockaddr_in);
		}

		sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(&out);
		sin6->sin6_family = AF_INET6;
		sin6->sin6_addr = address_.as_v6();
		sin6->sin6_port = htons(port_);
		return sizeof(sockaddr_in6);
	}

	std::optional<endpoint> endpoint::from_sockaddr(const sockaddr* addr, socklen_t len) noexcept {
		if (addr->sa_family == AF_INET && len >= static_cast<socklen_t>(sizeof(sockaddr_in))) {
			const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(addr);
			return endpoint(ip_address::from_v4(sin->sin_addr), ntohs(sin->sin_port));
		}

		if (addr->sa_family == AF_INET6 && len >= static_cast<socklen_t>(sizeof(sockaddr_in6))) {
			const sockaddr_in6* sin6 = reinterpret_cast<const sockaddr_in6*>(addr);
			return endpoint(ip_address::from_v6(sin6->sin6_addr), ntohs(sin6->sin6_port));
		}

		return std::nullopt;
	}

	std::vector<endpoint> resolve(std::string_view host, std::uint16_t port) {
		std::vector<endpoint> out;
		std::string owned(host);

		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		addrinfo* result = nullptr;
		if (::getaddrinfo(owned.c_str(), nullptr, &hints, &result) != 0 || !result)
			return out;

		for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
			if (auto ep = endpoint::from_sockaddr(it->ai_addr, static_cast<socklen_t>(it->ai_addrlen))) {
				out.emplace_back(ep->address(), port);
			}
		}

		::freeaddrinfo(result);
		return out;
	}

}
