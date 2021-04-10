#pragma once
#include "spinlock_t.hpp"

namespace nhttp {
namespace hal {

	/**
	 * class rwlock_t.
	 * read/write seperated lock.
	 */
	class NHTTP_API rwlock_t {
	private:
		spinlock_t spinlock;

		int32_t readers;
		int32_t writers;

	public:
		rwlock_t() : readers(0), writers(0) { }

	public:
		inline bool try_lock_read() {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			if (writers)
				return false;

			++readers;
			return true;
		}

		inline bool try_lock_write() {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			if (readers || writers)
				return false;

			++writers;
			return true;
		}

		inline void lock_read() {
			while(!try_lock_read())
				std::this_thread::yield();
		}

		inline void lock_write() {
			while(!try_lock_write())
				std::this_thread::yield();
		}

		inline void unlock_read() {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			--readers;
		}

		inline void unlock_write() {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			--writers;
		}
	};

}
}