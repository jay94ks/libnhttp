#pragma once

#include "io_context.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace nhttp::async {

	/**
	 * class io_context_pool.
	 * owns N independent io_context instances, each run on its own OS thread.
	 * a listener spreads connections across the pool by giving each context its
	 * own SO_REUSEPORT-bound listening socket (see server::listener) so the
	 * kernel load-balances accepts — this class only owns the contexts/threads.
	 */
	class io_context_pool {
	public:
		explicit io_context_pool(std::size_t worker_count);
		~io_context_pool();

		io_context_pool(const io_context_pool&) = delete;
		io_context_pool(io_context_pool&&) = delete;

	public:
		void start();
		void stop();

		std::size_t size() const noexcept { return contexts_.size(); }
		io_context& context(std::size_t index) noexcept { return *contexts_[index]; }

		/* round-robin accessor for work with no other placement preference. */
		io_context& next() noexcept;

	private:
		std::vector<std::unique_ptr<io_context>> contexts_;
		std::vector<std::thread> threads_;
		std::atomic<std::size_t> next_index_{ 0 };
		bool running_ = false;
	};

}
