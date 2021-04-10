#pragma once
#include "os/winapi.hpp"
#include "os/posix.hpp"
#include "spinlock_t.hpp"
#include "../utils/instrusive.hpp"

namespace nhttp {
namespace hal {

	/**
	 * class event_t.
	 * wrapper for os-specific event object.
	 */
	class NHTTP_API event_t {
	private:
#if NHTTP_OS_POSIX
		struct data_t;
		std::shared_ptr<data_t> eve_p;
#endif
#if NHTTP_OS_WINDOWS
		handle_zb_t eve_w;
#endif

	public:
		event_t(bool init_state, bool manual_reset = false);

	public:
		void signal();
		void unsignal();

		bool wait();
		bool try_wait() { return timed_wait(0); }
		bool timed_wait(int32_t timeout);
	};

	/**
	 * class event_lite_t.
	 * lite version of event_t.
	 * this will create event_t on demand.
	 */
	class NHTTP_API event_lite_t {
	private:
		hal::spinlock_t spinlock;
		utils::instrusive<event_t, true> event;
		std::atomic<bool> condition;
		std::atomic<int64_t> waiters;
		bool manual_reset;

	public:
		event_lite_t(bool init_state, bool manual_reset = false) 
			: condition(init_state), waiters(0), manual_reset(manual_reset)
		{
		}

	public:
		void signal();
		void unsignal();

		bool wait();
		bool try_wait();
		bool timed_wait(int32_t timeout);
	};
}
}