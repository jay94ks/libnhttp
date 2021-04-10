#pragma once
#include "../types.hpp"
#include "../assert.hpp"
#include "../utils/instrusive.hpp"
#include <thread>

namespace nhttp {
namespace hal {
	/**
	 * class spinlock_t.
	 * provides spinlock.
	 * usage: std::lock_guard<spinlock_t> _guard;
	 */
	class spinlock_t {
	private:
		std::atomic_flag guard = ATOMIC_FLAG_INIT;

	public:
		spinlock_t() { }
		spinlock_t(const spinlock_t&) = delete;
		spinlock_t(spinlock_t&&) = delete;
		~spinlock_t() { unlock(); }

	public:
		inline void lock() {
			while (guard.test_and_set(std::memory_order_acquire))
				std::this_thread::yield();
		}

		inline bool try_lock() {
			return !guard.test_and_set(std::memory_order_acquire);
		}

		inline void unlock() {
			guard.clear(std::memory_order_release);
		}
	};

	/**
	 * class spinlock_safe_t.
	 * provides spin-based critical section.
	 * usage: std::lock_guard<spinlock_safe_t> _guard;
	 */
	class spinlock_safe_t {
	private:
		spinlock_t unsafe;
		std::atomic_int32_t depth;
		utils::instrusive<std::thread::id, true> owner;

		NHTTP_DEBUG(
			std::atomic<bool> __dtor = false;
		);

	public:
		spinlock_safe_t() : depth(0) NHTTP_DEBUG(, __dtor(false)) { }
		spinlock_safe_t(const spinlock_safe_t&) = delete;
		spinlock_safe_t(spinlock_safe_t&&) = delete;

		NHTTP_DEBUG(
			~spinlock_safe_t() {
				__dtor = true;

				NHTTP_ENSURE(!depth,
					"somebody didn't unlock this."
				);

				unsafe.lock();
				while(depth);
			}
		);

	public:
		/* acquire spin-lock. */
		inline void lock() {
			NHTTP_DEBUG(
				time_t now = std::time(nullptr);
			);
			
			while (!try_lock())
				std::this_thread::yield();

			NHTTP_ENSURE(
				now == std::time(nullptr),
				"spinlock takes too much time!"
				"replace this to mutex or semaphore, SRW..."
			);
		}

		/* try to acquire spin-lock. */
		inline bool try_lock() {
			NHTTP_ASSERT(!__dtor, "this spinlock destructed!");

			std::lock_guard<spinlock_t> guard(unsafe);
			bool retval = false;
			
			if (!owner || *owner == std::this_thread::get_id()) {
				owner = std::this_thread::get_id();
				retval = true; ++depth;
			}

			return retval;
		}

		/* release spin-lock.*/
		inline void unlock() {
			NHTTP_ASSERT(!__dtor, "this spinlock destructed!");

			std::lock_guard<spinlock_t> guard(unsafe);
			
			if (owner && *owner == std::this_thread::get_id()) {
				if (!--depth)
					owner.unset();
			}
		}
	};

}
}