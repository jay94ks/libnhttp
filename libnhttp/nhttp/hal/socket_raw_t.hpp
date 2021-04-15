#pragma once
#include "../types.hpp"
#include "../assert.hpp"
#include "../depends/wepoll/wepoll.h"

namespace nhttp {
namespace hal {

#if NHTTP_OS_WINDOWS
	using socket_fd_t = SOCKET;
	constexpr socket_fd_t INVALID_SOCKET_FD = socket_fd_t(~0);
#else
	using socket_fd_t = int;
	constexpr socket_fd_t INVALID_SOCKET_FD = -1;
#endif

	struct ipv4_addr;
	struct ipv6_addr;

	NHTTP_API std::string to_string(const ipv4_addr& in);
	NHTTP_API std::string to_string(const ipv6_addr& in);

	struct ipv4_addr {
		union {
			uint32_t u32;
			uint8_t  u8_4[4];
		} addr;

		uint16_t port;
	};

	struct ipv6_addr {
		union {
			uint64_t u64_2[2];
			uint8_t  u8_16[128];
		} addr;

		uint16_t port;
		uint32_t flow;
		uint32_t scope;
	};

	struct tcp { };
	struct udp { };

	/* resolve ip address. */
	NHTTP_API bool resolve(ipv4_addr& out, const char* in_string);
	NHTTP_API bool resolve(ipv6_addr& out, const char* in_string);

	/* resolve all available ip addresses. */
	NHTTP_API bool resolve_all(std::vector<ipv4_addr>& out, const char* in_string);
	NHTTP_API bool resolve_all(std::vector<ipv6_addr>& out, const char* in_string);

#if NHTTP_OS_WINDOWS
#	define SHUT_RD 0
#	define SHUT_WR 1
#	define SHUT_RDWR 2
	typedef int socklen_t;
#endif

	class NHTTP_API socket_raw_t {
	private:
		socket_fd_t fd;
		mutable int32_t err;

	public:
		socket_raw_t(nullptr_t = nullptr)
			: fd(INVALID_SOCKET_FD), err(0) { }

		socket_raw_t(socket_fd_t fd, int32_t err = 0) 
			: fd(fd), err(err) { }

	private:
		static socket_fd_t create(bool v4, bool tcp);
		static int32_t get_last_error();

	public:
		inline operator bool() const { return is_valid(); }
		inline bool operator !() const { return !is_valid(); }

		inline bool operator ==(const socket_raw_t& o) const { return fd == o.fd; }
		inline bool operator !=(const socket_raw_t& o) const { return fd != o.fd; }

	public:
		template<typename address_type = ipv4_addr, typename protocol = tcp>
		inline static socket_raw_t create() {
			bool requires_v4 = std::is_same<ipv4_addr, address_type>::value;
			bool requires_tcp = std::is_same<tcp, protocol>::value;
			socket_raw_t sock = create(requires_v4, requires_tcp);

			if (sock.fd == INVALID_SOCKET_FD)
				sock.err = get_last_error();

			return sock;
		}

	public:
		inline bool is_valid() const { return fd != INVALID_SOCKET_FD; }
		inline int32_t error() const { return err; }
		std::string error_str() const;
		inline socket_fd_t get_fd() const { return fd; }

	public:
		socket_raw_t clone();

	private:
		bool check_return(int32_t ret_val) const;
		bool bind(void* data, size_t size);
		bool connect(void* data, size_t size);

	public:
		template<typename address_type>
		inline bool bind(const address_type& addr) {
			return bind((void*)&addr, sizeof(address_type));
		}

		template<typename address_type>
		inline bool connect(const address_type& addr) {
			return connect((void*)&addr, sizeof(address_type));
		}

		bool listen(int32_t backlog);
		socket_raw_t accept();

	private:
		bool get_local_address(void* out, size_t size);
		bool get_remote_address(void* out, size_t size);

	public:
		template<typename address_type>
		bool get_local_address(address_type& out_addr) {
			return get_local_address(&out_addr, sizeof(address_type));
		}

		template<typename address_type>
		bool get_remote_address(address_type& out_addr) {
			return get_remote_address(&out_addr, sizeof(address_type));
		}

	private:
		bool get_option(int level, int optname, void* optval, socklen_t* optlen) const;
		bool set_option(int level, int optname, const void* optval, socklen_t optlen) const;

	public:
		bool set_non_blocking(bool on);
		bool set_naggle_enabled(bool on);
		bool set_reuse_address(bool allow);
		bool set_read_timeout(int32_t millisec);
		bool set_write_timeout(int32_t millisec);

		/* to disable: call with 0, 0.*/
		bool set_keepalive(uint32_t idle, uint32_t interval, uint32_t count = 10);

		/* set linger, millisec < 0: disable, millisec >= 0 enable. */
		bool set_linger(int32_t millisec);

	public:
		ssize_t read(void* buf, size_t n);
		//ssize_t read_n(void* buf, size_t n);

		ssize_t write(const void* buf, size_t n);
		//ssize_t write_n(const void* buf, size_t n);

	public:
		bool shutdown(int how = SHUT_RDWR);
		bool close();
	};

}
}