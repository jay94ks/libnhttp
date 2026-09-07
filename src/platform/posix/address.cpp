#include "sockaddr_convert.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>

namespace nhttp::platform {

	ip_address::ip_address() noexcept
		: version_(ip_version::v4), v4_{}, v6_{}
	{
	}

	ip_address ip_address::from_v4_bytes(const v4_bytes_t& bytes) noexcept {
		ip_address out;
		out.version_ = ip_version::v4;
		out.v4_ = bytes;
		return out;
	}

	ip_address ip_address::from_v6_bytes(const v6_bytes_t& bytes) noexcept {
		ip_address out;
		out.version_ = ip_version::v6;
		out.v6_ = bytes;
		return out;
	}

	std::optional<ip_address> ip_address::parse(std::string_view text) noexcept {
		std::string owned(text);

		in_addr v4{};
		if (::inet_pton(AF_INET, owned.c_str(), &v4) == 1) {
			v4_bytes_t bytes{};
			std::memcpy(bytes.data(), &v4, bytes.size());
			return from_v4_bytes(bytes);
		}

		in6_addr v6{};
		if (::inet_pton(AF_INET6, owned.c_str(), &v6) == 1) {
			v6_bytes_t bytes{};
			std::memcpy(bytes.data(), &v6, bytes.size());
			return from_v6_bytes(bytes);
		}

		return std::nullopt;
	}

	ip_address ip_address::any_v4() noexcept {
		in_addr addr{};
		addr.s_addr = htonl(INADDR_ANY);
		v4_bytes_t bytes{};
		std::memcpy(bytes.data(), &addr, bytes.size());
		return from_v4_bytes(bytes);
	}

	ip_address ip_address::any_v6() noexcept {
		v6_bytes_t bytes{};
		std::memcpy(bytes.data(), &in6addr_any, bytes.size());
		return from_v6_bytes(bytes);
	}

	ip_address ip_address::loopback_v4() noexcept {
		in_addr addr{};
		addr.s_addr = htonl(INADDR_LOOPBACK);
		v4_bytes_t bytes{};
		std::memcpy(bytes.data(), &addr, bytes.size());
		return from_v4_bytes(bytes);
	}

	ip_address ip_address::loopback_v6() noexcept {
		v6_bytes_t bytes{};
		std::memcpy(bytes.data(), &in6addr_loopback, bytes.size());
		return from_v6_bytes(bytes);
	}

	std::string ip_address::to_string() const {
		char buf[INET6_ADDRSTRLEN] = { 0 };

		if (is_v4())
			::inet_ntop(AF_INET, v4_.data(), buf, sizeof(buf));
		else
			::inet_ntop(AF_INET6, v6_.data(), buf, sizeof(buf));

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
			if (auto ep = posix_detail::endpoint_from_sockaddr(it->ai_addr, static_cast<socklen_t>(it->ai_addrlen))) {
				out.emplace_back(ep->address(), port);
			}
		}

		::freeaddrinfo(result);
		return out;
	}

}
