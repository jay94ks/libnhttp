#include "nhttp/async/io_context.hpp"

#include <algorithm>
#include <limits>

namespace nhttp::async {

	io_context::io_context() : reactor_(platform::make_reactor()) {
	}

	io_context::~io_context() {
	}

	void io_context::run() {
		static constexpr int max_events = 256;
		platform::ready_event events[max_events];

		while (!stopping_.load(std::memory_order_acquire)) {
			const int timeout_ms = compute_timeout_ms();
			const int n = reactor_->wait(events, max_events, timeout_ms);

			if (n > 0)
				process_ready_events(events, n);

			process_timers();
			drain_posted();
		}
	}

	void io_context::stop() noexcept {
		stopping_.store(true, std::memory_order_release);
		reactor_->wake();
	}

	void io_context::post(std::coroutine_handle<> h) {
		{
			std::lock_guard<std::mutex> lock(posted_mutex_);
			posted_.push_back(h);
		}

		reactor_->wake();
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
		if (reg.registered_with_reactor)
			reactor_->remove(reg.fd);

		reg.registered_with_reactor = false;
		reg.read_waiter = nullptr;
		reg.write_waiter = nullptr;
		reg.interest_read = false;
		reg.interest_write = false;
	}

	void io_context::update_interest(io_registration& reg) const {
		const bool want_read = static_cast<bool>(reg.read_waiter);
		const bool want_write = static_cast<bool>(reg.write_waiter);

		if (!reg.registered_with_reactor) {
			if (want_read || want_write) {
				reactor_->add(reg.fd, want_read, want_write, &reg);
				reg.registered_with_reactor = true;
				reg.interest_read = want_read;
				reg.interest_write = want_write;
			}

			return;
		}

		if (want_read == reg.interest_read && want_write == reg.interest_write)
			return;

		if (!want_read && !want_write) {
			reactor_->remove(reg.fd);
			reg.registered_with_reactor = false;
		}
		else {
			reactor_->modify(reg.fd, want_read, want_write, &reg);
		}

		reg.interest_read = want_read;
		reg.interest_write = want_write;
	}

	void io_context::add_timer(std::chrono::steady_clock::time_point deadline, std::coroutine_handle<> h) {
		timers_.push_back(timer_entry{ deadline, h });
		std::push_heap(timers_.begin(), timers_.end(), &io_context::timer_later);
	}

	void io_context::process_ready_events(const platform::ready_event* events, int count) {
		for (int i = 0; i < count; ++i) {
			auto* reg = static_cast<io_registration*>(events[i].user_data);

			if (!reg)
				continue;

			std::coroutine_handle<> to_resume_read;
			std::coroutine_handle<> to_resume_write;

			if (events[i].readable && reg->read_waiter) {
				to_resume_read = reg->read_waiter;
				reg->read_waiter = nullptr;
			}

			if (events[i].writable && reg->write_waiter) {
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
