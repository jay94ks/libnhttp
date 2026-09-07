#include "nhttp/async/io_context_pool.hpp"

namespace nhttp::async {

	io_context_pool::io_context_pool(std::size_t worker_count) {
		if (worker_count == 0)
			worker_count = 1;

		contexts_.reserve(worker_count);

		for (std::size_t i = 0; i < worker_count; ++i)
			contexts_.push_back(std::make_unique<io_context>());
	}

	io_context_pool::~io_context_pool() {
		stop();
	}

	void io_context_pool::start() {
		if (running_)
			return;

		running_ = true;
		threads_.reserve(contexts_.size());

		for (const std::unique_ptr<io_context>& ctx : contexts_) {
			io_context* raw = ctx.get();
			threads_.emplace_back([raw] { raw->run(); });
		}
	}

	void io_context_pool::stop() {
		if (!running_)
			return;

		for (const std::unique_ptr<io_context>& ctx : contexts_)
			ctx->stop();

		for (std::thread& t : threads_) {
			if (t.joinable())
				t.join();
		}

		threads_.clear();
		running_ = false;
	}

	io_context& io_context_pool::next() noexcept {
		const std::size_t i = next_index_.fetch_add(1, std::memory_order_relaxed) % contexts_.size();
		return *contexts_[i];
	}

}
