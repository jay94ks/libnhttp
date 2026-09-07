#pragma once

// internal implementation detail — never installed, never included from
// include/nhttp/**. keeps sockaddr/SOCKET out of every public header.

#include "nhttp/platform/address.hpp"

#include <cstring>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>

namespace nhttp::platform::win32_detail {

	inline int endpoint_to_sockaddr(const endpoint& ep, sockaddr_storage& out) noexcept {
		std::memset(&out, 0, sizeof(out));

		if (ep.address().is_v4()) {
			sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&out);
			sin->sin_family = AF_INET;
			std::memcpy(&sin->sin_addr, ep.address().v4_bytes().data(), sizeof(sin->sin_addr));
			sin->sin_port = htons(ep.port());
			return static_cast<int>(sizeof(sockaddr_in));
		}

		sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(&out);
		sin6->sin6_family = AF_INET6;
		std::memcpy(&sin6->sin6_addr, ep.address().v6_bytes().data(), sizeof(sin6->sin6_addr));
		sin6->sin6_port = htons(ep.port());
		return static_cast<int>(sizeof(sockaddr_in6));
	}

	inline std::optional<endpoint> endpoint_from_sockaddr(const sockaddr* addr, int len) noexcept {
		if (addr->sa_family == AF_INET && len >= static_cast<int>(sizeof(sockaddr_in))) {
			const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(addr);
			ip_address::v4_bytes_t bytes{};
			std::memcpy(bytes.data(), &sin->sin_addr, bytes.size());
			return endpoint(ip_address::from_v4_bytes(bytes), ntohs(sin->sin_port));
		}

		if (addr->sa_family == AF_INET6 && len >= static_cast<int>(sizeof(sockaddr_in6))) {
			const sockaddr_in6* sin6 = reinterpret_cast<const sockaddr_in6*>(addr);
			ip_address::v6_bytes_t bytes{};
			std::memcpy(bytes.data(), &sin6->sin6_addr, bytes.size());
			return endpoint(ip_address::from_v6_bytes(bytes), ntohs(sin6->sin6_port));
		}

		return std::nullopt;
	}

}
