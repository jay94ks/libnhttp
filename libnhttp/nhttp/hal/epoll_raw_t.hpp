#pragma once
#include "../types.hpp"
#include "../assert.hpp"
#include "socket_raw_t.hpp"
#include "../depends/wepoll/wepoll.h"

namespace nhttp {
namespace hal {

	/**
	 * class epoll_raw_t.
	 * wrapper for `epoll`. (in windows, uses wepoll)
	 */
	class NHTTP_API epoll_raw_t {
		using efd_t = decltype(epoll_create(0));

	private:
		efd_t					fd;
		std::atomic<int32_t>	amount;

	public:
		epoll_raw_t(int32_t size);
		~epoll_raw_t();

		/* add socket fd on epoll. */
		bool add(socket_fd_t _fd, uint32_t flags, void* data);

		/* modify socket fd relation on epoll. */
		bool modify(socket_fd_t _fd, uint32_t flags, void* data);

		/* remove socket fd from epoll. */
		bool remove(socket_fd_t _fd);

		/* wait epoll events. */
		int32_t wait(epoll_event* events, int32_t size, int32_t timeout);
	};

}
}