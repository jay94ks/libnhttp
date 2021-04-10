#pragma once
#include "../types.hpp"
#include "../hal/socket_raw_t.hpp"

namespace nhttp {
namespace _ {
	template<typename network>
	struct is_supported_network { static constexpr bool value = false; };
	template<> struct is_supported_network<hal::ipv4_addr> { static constexpr bool value = true; };
	template<> struct is_supported_network<hal::ipv6_addr> { static constexpr bool value = true; };
}

	/**
	 * struct endpoint<network>.
	 * represents endpoint.
	 */
	template<typename network>
	struct endpoint {
		static_assert(
			_::is_supported_network<network>::value,
			"endpoint requires supported network type."
		);

		using address_type = network;
		
		address_type addr;
		bool validity;

		/* constructors */
		endpoint() : addr({0, }), validity(false) { }
		endpoint(address_type addr) : addr(addr), validity(true) { }
		endpoint(const endpoint&) = default;
		endpoint(endpoint&&) = default;
		endpoint& operator =(const endpoint&) = default;
		endpoint& operator =(endpoint&&) = default;

		/* resolve real address from given string. */
		inline static endpoint resolve(const char* addr, int32_t port) {
			address_type raw_addr;
			
			if (hal::resolve(raw_addr, addr)) {
				raw_addr.port = port;
				return endpoint(raw_addr);
			}

			return endpoint();
		}

		/* resolve all available real addresses from given string. */
		inline static std::vector<endpoint> resolve_all(const char* addr) {
			std::vector<address_type> raw_addrs;
			std::vector<endpoint> end_points;

			hal::resolve_all(raw_addrs, addr);
			for (auto& raw_addr : raw_addrs)
				end_points.push_back(raw_addr);

			return end_points;
		}
	};

	
	using ipv4_addr = hal::ipv4_addr;
	using ipv6_addr = hal::ipv6_addr;
	
	using ipv4 = endpoint<ipv4_addr>;
	using ipv6 = endpoint<ipv6_addr>;
}