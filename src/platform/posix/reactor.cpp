#include "nhttp/platform/reactor.hpp"

#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

namespace nhttp::platform {

	namespace {

		/**
		 * class epoll_reactor.
		 * the POSIX reactor::wait() is level-triggered epoll underneath, but
		 * io_context already treats every readiness notification as one-shot
		 * (it clears the waiter and drops interest before resuming — see
		 * io_context::process_ready_events/update_interest), so this only ever
		 * needs to report "ready right now", matching platform::reactor's
		 * contract exactly. wake() is its own eventfd, owned here rather than
		 * by io_context, so io_context.cpp never needs to know it's epoll.
		 */
		class epoll_reactor final : public reactor {
		public:
			epoll_reactor() {
				fd_ = ::epoll_create1(0);

				if (fd_ < 0)
					throw std::runtime_error("epoll_create1 failed");

				wake_fd_ = ::eventfd(0, EFD_NONBLOCK);

				if (wake_fd_ < 0) {
					::close(fd_);
					throw std::runtime_error("eventfd failed");
				}

				epoll_event ev{};
				ev.events = EPOLLIN;
				ev.data.ptr = nullptr; // sentinel: the wake fd, never surfaced as a ready_event.
				::epoll_ctl(fd_, EPOLL_CTL_ADD, wake_fd_, &ev);
			}

			~epoll_reactor() override {
				if (wake_fd_ >= 0)
					::close(wake_fd_);

				if (fd_ >= 0)
					::close(fd_);
			}

			bool add(native_socket_t fd, bool want_read, bool want_write, void* user_data) noexcept override {
				epoll_event ev{};
				ev.events = to_epoll_events(want_read, want_write);
				ev.data.ptr = user_data;

				return ::epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
			}

			bool modify(native_socket_t fd, bool want_read, bool want_write, void* user_data) noexcept override {
				epoll_event ev{};
				ev.events = to_epoll_events(want_read, want_write);
				ev.data.ptr = user_data;

				return ::epoll_ctl(fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
			}

			void remove(native_socket_t fd) noexcept override {
				::epoll_ctl(fd_, EPOLL_CTL_DEL, fd, nullptr);
			}

			int wait(ready_event* out_events, int max_events, int timeout_ms) noexcept override {
				if (scratch_.size() < static_cast<std::size_t>(max_events))
					scratch_.resize(static_cast<std::size_t>(max_events));

				const int n = ::epoll_wait(fd_, scratch_.data(), max_events, timeout_ms);
				int produced = 0;

				for (int i = 0; i < n; ++i) {
					if (scratch_[static_cast<std::size_t>(i)].data.ptr == nullptr) {
						std::uint64_t value = 0;
						[[maybe_unused]] const ssize_t ignored = ::read(wake_fd_, &value, sizeof(value));
						continue;
					}

					const std::uint32_t flags = scratch_[static_cast<std::size_t>(i)].events;
					ready_event& out = out_events[produced++];

					out.user_data = scratch_[static_cast<std::size_t>(i)].data.ptr;
					out.readable = (flags & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0;
					out.writable = (flags & (EPOLLOUT | EPOLLHUP | EPOLLERR)) != 0;
				}

				return produced;
			}

			void wake() noexcept override {
				const std::uint64_t one = 1;
				[[maybe_unused]] const ssize_t ignored = ::write(wake_fd_, &one, sizeof(one));
			}

		private:
			static std::uint32_t to_epoll_events(bool want_read, bool want_write) noexcept {
				std::uint32_t events = 0;

				if (want_read)
					events |= static_cast<std::uint32_t>(EPOLLIN);

				if (want_write)
					events |= static_cast<std::uint32_t>(EPOLLOUT);

				return events;
			}

			int fd_ = -1;
			int wake_fd_ = -1;
			std::vector<epoll_event> scratch_;
		};

	}

	std::unique_ptr<reactor> make_reactor() {
		return std::make_unique<epoll_reactor>();
	}

}
