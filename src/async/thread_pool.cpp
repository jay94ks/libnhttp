#include "nhttp/async/thread_pool.hpp"

namespace nhttp::async {

	thread_pool::thread_pool(std::size_t worker_count) {
		if (worker_count == 0)
			worker_count = 1;

		workers_.reserve(worker_count);

		for (std::size_t i = 0; i < worker_count; ++i)
			workers_.emplace_back([this] { worker_loop(); });
	}

	thread_pool::~thread_pool() {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stopping_ = true;
		}

		cv_.notify_all();

		for (std::thread& t : workers_) {
			if (t.joinable())
				t.join();
		}
	}

	void thread_pool::enqueue(std::function<void()> job) {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			jobs_.push(std::move(job));
		}

		cv_.notify_one();
	}

	void thread_pool::worker_loop() {
		for (;;) {
			std::function<void()> job;

			{
				std::unique_lock<std::mutex> lock(mutex_);
				cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });

				if (jobs_.empty()) {
					if (stopping_)
						return;

					continue;
				}

				job = std::move(jobs_.front());
				jobs_.pop();
			}

			job();
		}
	}

}
