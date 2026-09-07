#pragma once

#include <cstdint>
#include <sys/epoll.h>

namespace nhttp::platform {

	/**
	 * class epoll_handle.
	 * thin RAII wrapper over a Linux epoll instance. level-triggered by default
	 * (no EPOLLET) — simpler to reason about for a first correct implementation;
	 * see CLAUDE.md's architecture-decisions log before switching to edge-triggered.
	 */
	class epoll_handle {
	public:
		epoll_handle();
		~epoll_handle();

		epoll_handle(epoll_handle&& other) noexcept;
		epoll_handle& operator=(epoll_handle&& other) noexcept;

		epoll_handle(const epoll_handle&) = delete;
		epoll_handle& operator=(const epoll_handle&) = delete;

	public:
		bool valid() const noexcept { return fd_ >= 0; }

		bool add(int fd, std::uint32_t events, void* user_data) const noexcept;
		bool modify(int fd, std::uint32_t events, void* user_data) const noexcept;
		bool remove(int fd) const noexcept;

		/* blocks up to timeout_ms (-1 = forever, 0 = poll). returns the number of ready events. */
		int wait(epoll_event* out_events, int max_events, int timeout_ms) const noexcept;

	private:
		int fd_ = -1;
	};

}
