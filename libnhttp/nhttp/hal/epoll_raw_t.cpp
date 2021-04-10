#include "epoll_raw_t.hpp"
#include <thread>

namespace nhttp {
namespace hal {

	epoll_raw_t::epoll_raw_t(int32_t size)
		: fd(epoll_create(size))
	{
#if NHTTP_OS_WINDOWS
		NHTTP_CRITICAL(fd,
			"wepoll: epoll_create(size) failure!"
		);
#else
		int32_t retries = 0;

		while (fd < 0) {
			NHTTP_CRITICAL(
				errno == EMFILE || errno == ENFILE,
				"epoll: epoll_create(size): fd limit exhaused!"
			);

			NHTTP_CRITICAL(
				retries > 3,
				"epoll: epoll_create(size): kernel object couldn't be created!"
			);

			if (errno == ENOMEM) {
				std::this_thread::sleep_for(
					std::chrono::milliseconds(100)
				);

				fd = epoll_create(size);
				++retries;
			}
		}
#endif
	}

	epoll_raw_t::~epoll_raw_t() {
#if NHTTP_OS_WINDOWS
		epoll_close(fd);
#else
		close(fd);
#endif
	}

	bool epoll_raw_t::add(socket_fd_t _fd, uint32_t flags, void* data) {
		epoll_event event;

		event.data.ptr = data;
		event.events = flags;

		if (!epoll_ctl(fd, EPOLL_CTL_ADD, _fd, &event)) {
			++amount;
			return true;
		}

		return false;
	}

	bool epoll_raw_t::modify(socket_fd_t _fd, uint32_t flags, void* data) {
		epoll_event event;

		event.data.ptr = data;
		event.events = flags;

		return !epoll_ctl(fd, EPOLL_CTL_MOD, _fd, &event);
	}

	bool epoll_raw_t::remove(socket_fd_t _fd) {
		epoll_event event = { 0, };

		if (!epoll_ctl(fd, EPOLL_CTL_DEL, _fd, &event)) {
			--amount;
			return true;
		}

		return false;
	}

	int32_t epoll_raw_t::wait(epoll_event* events, int32_t size, int32_t timeout) {
		return epoll_wait(fd, events, size, timeout);
	}

}
}