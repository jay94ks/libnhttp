#include "nhttp/async/io_context.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <unistd.h>

namespace nhttp::async {

	io_context::io_context() {
		wake_fd_ = ::eventfd(0, EFD_NONBLOCK);
		if (wake_fd_ < 0)
			throw std::runtime_error("eventfd failed");

		epoll_.add(wake_fd_, EPOLLIN, nullptr);
	}

	io_context::~io_context() {
		if (wake_fd_ >= 0)
			::close(wake_fd_);
	}

	void io_context::run() {
		static constexpr int max_events = 256;
		epoll_event events[max_events];

		while (!stopping_.load(std::memory_order_acquire)) {
			const int timeout_ms = compute_timeout_ms();
			const int n = epoll_.wait(events, max_events, timeout_ms);

			if (n > 0)
				process_ready_epoll_events(events, n);

			process_timers();
			drain_posted();
		}
	}

	void io_context::stop() noexcept {
		stopping_.store(true, std::memory_order_release);

		const std::uint64_t one = 1;
		[[maybe_unused]] const ssize_t ignored = ::write(wake_fd_, &one, sizeof(one));
	}

	void io_context::post(std::coroutine_handle<> h) {
		{
			std::lock_guard<std::mutex> lock(posted_mutex_);
			posted_.push_back(h);
		}

		const std::uint64_t one = 1;
		[[maybe_unused]] const ssize_t ignored = ::write(wake_fd_, &one, sizeof(one));
	}

	void io_context::watch_readable(io_registration& reg, std::coroutine_handle<> h) {
		reg.read_waiter = h;
		update_interest(reg);
	}

	void io_context::watch_writable(io_registration& reg, std::coroutine_handle<> h) {
		reg.write_waiter = h;
		update_interest(reg);
	}

	void io_context::forget(io_registration& reg) noexcept {
		if (reg.registered_with_epoll)
			epoll_.remove(reg.fd);

		reg.registered_with_epoll = false;
		reg.read_waiter = nullptr;
		reg.write_waiter = nullptr;
		reg.interest = 0;
	}

	void io_context::update_interest(io_registration& reg) const {
		std::uint32_t desired = 0;

		if (reg.read_waiter)
			desired |= static_cast<std::uint32_t>(EPOLLIN);

		if (reg.write_waiter)
			desired |= static_cast<std::uint32_t>(EPOLLOUT);

		if (!reg.registered_with_epoll) {
			if (desired != 0) {
				epoll_.add(reg.fd, desired, &reg);
				reg.registered_with_epoll = true;
				reg.interest = desired;
			}

			return;
		}

		if (desired == reg.interest)
			return;

		if (desired == 0) {
			epoll_.remove(reg.fd);
			reg.registered_with_epoll = false;
		}
		else {
			epoll_.modify(reg.fd, desired, &reg);
		}

		reg.interest = desired;
	}

	void io_context::add_timer(std::chrono::steady_clock::time_point deadline, std::coroutine_handle<> h) {
		timers_.push_back(timer_entry{ deadline, h });
		std::push_heap(timers_.begin(), timers_.end(), &io_context::timer_later);
	}

	void io_context::process_ready_epoll_events(epoll_event* events, int count) {
		for (int i = 0; i < count; ++i) {
			if (events[i].data.ptr == nullptr) {
				std::uint64_t value = 0;
				[[maybe_unused]] const ssize_t ignored = ::read(wake_fd_, &value, sizeof(value));
				continue;
			}

			auto* reg = static_cast<io_registration*>(events[i].data.ptr);
			const std::uint32_t flags = events[i].events;

			const bool readable = (flags & (static_cast<std::uint32_t>(EPOLLIN) | static_cast<std::uint32_t>(EPOLLHUP) | static_cast<std::uint32_t>(EPOLLERR))) != 0;
			const bool writable = (flags & (static_cast<std::uint32_t>(EPOLLOUT) | static_cast<std::uint32_t>(EPOLLHUP) | static_cast<std::uint32_t>(EPOLLERR))) != 0;

			std::coroutine_handle<> to_resume_read;
			std::coroutine_handle<> to_resume_write;

			if (readable && reg->read_waiter) {
				to_resume_read = reg->read_waiter;
				reg->read_waiter = nullptr;
			}

			if (writable && reg->write_waiter) {
				to_resume_write = reg->write_waiter;
				reg->write_waiter = nullptr;
			}

			update_interest(*reg);

			if (to_resume_read)
				to_resume_read.resume();

			if (to_resume_write)
				to_resume_write.resume();
		}
	}

	void io_context::process_timers() {
		const auto now = std::chrono::steady_clock::now();

		while (!timers_.empty() && timers_.front().deadline <= now) {
			const std::coroutine_handle<> h = timers_.front().handle;
			std::pop_heap(timers_.begin(), timers_.end(), &io_context::timer_later);
			timers_.pop_back();
			h.resume();
		}
	}

	void io_context::drain_posted() {
		std::vector<std::coroutine_handle<>> local;

		{
			std::lock_guard<std::mutex> lock(posted_mutex_);
			local.swap(posted_);
		}

		for (const std::coroutine_handle<>& h : local)
			h.resume();
	}

	int io_context::compute_timeout_ms() const {
		if (timers_.empty())
			return -1;

		const auto now = std::chrono::steady_clock::now();
		const auto diff = timers_.front().deadline - now;
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();

		if (ms < 0)
			ms = 0;

		if (ms > std::numeric_limits<int>::max())
			ms = std::numeric_limits<int>::max();

		return static_cast<int>(ms);
	}

	bool io_context::timer_later(const timer_entry& a, const timer_entry& b) noexcept {
		return a.deadline > b.deadline;
	}

}
