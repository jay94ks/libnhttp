#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace nhttp::async::detail {

	/**
	 * class mpmc_queue.
	 * Dmitry Vyukov's bounded multi-producer/multi-consumer lock-free queue
	 * (http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue)
	 * — see PLAN.md's P4 and CLAUDE.md's Phase 16 log for the two designs
	 * tried around this before landing on the current one: `thread_pool`
	 * uses this purely for the push/pop data path (lock-free), pairing it
	 * with a plain mutex + condition_variable used *only* for event
	 * propagation (waking an idle worker) — never for the queue operations
	 * themselves. Every slot carries its own sequence number, so producers
	 * and consumers never contend on one shared index the way a naive ring
	 * buffer would — the only atomics touched per push/pop are that slot's
	 * sequence number and a CAS on the (cacheline-separated) enqueue/dequeue
	 * position counters.
	 *
	 * `capacity` must be a power of two (the caller's responsibility — this
	 * class doesn't round up, to keep the hot path free of a branch/division
	 * per operation). Capacity 1 is a genuine degenerate case for this
	 * specific algorithm, not just an inefficient one: with a single slot,
	 * the sequence number a push leaves behind (pos + 1 = 1, for the first
	 * push at pos 0) is indistinguishable from a *different* slot's fresh,
	 * never-used initial sequence number in the capacity >= 2 case — so a
	 * second push can wrongly believe the still-unconsumed slot is
	 * available again. Found by this file's own unit tests exercising
	 * capacity 1 directly; every real caller in this codebase uses a much
	 * larger capacity, where the algorithm's standard correctness argument
	 * (each slot cycles through `capacity` distinguishable generations)
	 * holds as documented upstream.
	 */
	template<typename T>
	class mpmc_queue {
	public:
		explicit mpmc_queue(std::size_t capacity)
			: buffer_(std::make_unique<cell[]>(capacity)), mask_(capacity - 1)
		{
			for (std::size_t i = 0; i < capacity; ++i)
				buffer_[i].sequence.store(i, std::memory_order_relaxed);
		}

		mpmc_queue(const mpmc_queue&) = delete;
		mpmc_queue& operator=(const mpmc_queue&) = delete;

	public:
		/* true if `value` was pushed (moved from); false only if the queue is
		 * completely full (every slot currently holds an unconsumed item),
		 * in which case `value` is left untouched so the caller can retry
		 * with the same object — this takes T&&, not T, specifically so nothing
		 * gets moved-from until the success path actually needs it; a
		 * by-value parameter would consume `value` at the call site on every
		 * retry attempt regardless of whether this call actually succeeded,
		 * silently losing it on a full queue (thread_pool's own retry loop
		 * needs this). The caller decides how to react to `false` (thread_pool
		 * retries with a yield; see its doc comment for why that's still safe
		 * for a reactor-thread caller). */
		bool try_push(T&& value) noexcept {
			std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
			cell* c;

			for (;;) {
				c = &buffer_[pos & mask_];
				const std::size_t seq = c->sequence.load(std::memory_order_acquire);
				const std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

				if (diff == 0) {
					if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
						break;
				}
				else if (diff < 0) {
					return false; // full
				}
				else {
					pos = enqueue_pos_.load(std::memory_order_relaxed);
				}
			}

			c->value = std::move(value);
			c->sequence.store(pos + 1, std::memory_order_release);
			return true;
		}

		/* true if an item was popped into `out`; false only if the queue is
		 * completely empty. */
		bool try_pop(T& out) noexcept {
			std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
			cell* c;

			for (;;) {
				c = &buffer_[pos & mask_];
				const std::size_t seq = c->sequence.load(std::memory_order_acquire);
				const std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

				if (diff == 0) {
					if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
						break;
				}
				else if (diff < 0) {
					return false; // empty
				}
				else {
					pos = dequeue_pos_.load(std::memory_order_relaxed);
				}
			}

			out = std::move(c->value);
			c->sequence.store(pos + mask_ + 1, std::memory_order_release);
			return true;
		}

	private:
		struct cell {
			std::atomic<std::size_t> sequence{ 0 };
			T value{};
		};

		std::unique_ptr<cell[]> buffer_;
		std::size_t mask_;

		alignas(64) std::atomic<std::size_t> enqueue_pos_{ 0 };
		alignas(64) std::atomic<std::size_t> dequeue_pos_{ 0 };
	};

}
