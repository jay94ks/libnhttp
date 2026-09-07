#include "nhttp/platform/epoll.hpp"

#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace nhttp::platform {

	epoll_handle::epoll_handle() : fd_(::epoll_create1(0)) {
		if (fd_ < 0)
			throw std::runtime_error("epoll_create1 failed");
	}

	epoll_handle::~epoll_handle() {
		if (fd_ >= 0)
			::close(fd_);
	}

	epoll_handle::epoll_handle(epoll_handle&& other) noexcept
		: fd_(std::exchange(other.fd_, -1))
	{
	}

	epoll_handle& epoll_handle::operator=(epoll_handle&& other) noexcept {
		if (this != &other) {
			if (fd_ >= 0)
				::close(fd_);

			fd_ = std::exchange(other.fd_, -1);
		}

		return *this;
	}

	bool epoll_handle::add(int fd, std::uint32_t events, void* user_data) const noexcept {
		epoll_event ev{};
		ev.events = events;
		ev.data.ptr = user_data;

		return ::epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
	}

	bool epoll_handle::modify(int fd, std::uint32_t events, void* user_data) const noexcept {
		epoll_event ev{};
		ev.events = events;
		ev.data.ptr = user_data;

		return ::epoll_ctl(fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
	}

	bool epoll_handle::remove(int fd) const noexcept {
		return ::epoll_ctl(fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
	}

	int epoll_handle::wait(epoll_event* out_events, int max_events, int timeout_ms) const noexcept {
		return ::epoll_wait(fd_, out_events, max_events, timeout_ms);
	}

}
