#include "event_t.hpp"

#if NHTTP_OS_WINDOWS
#include <Windows.h>
#endif

namespace nhttp {
namespace hal {

#if NHTTP_OS_POSIX
	struct event_t::data_t {
		utils::instrusive<pthread_mutex_t, true> mutex;
		utils::instrusive<pthread_cond_t, true> cond;
		bool condition, auto_reset;

		inline pthread_mutex_t* as_mutex_ptr() {
			return mutex ? &(*mutex) : nullptr;
		}

		inline pthread_cond_t* as_cond_ptr() {
			return cond ? &(*cond) : nullptr;
		}

		~data_t() {
			if (cond) { pthread_cond_destroy(as_cond_ptr()); }
			if (mutex) { pthread_mutex_destroy(as_mutex_ptr()); }
			cond.unset(); mutex.unset();
		}
	};
#endif

	event_t::event_t(bool init_state, bool manual_reset) {
#if NHTTP_OS_WINDOWS
		eve_w = handle_zb_t::make(CreateEventA(nullptr, manual_reset, init_state, nullptr));
#endif
#if NHTTP_OS_POSIX
		int state;

		eve_p = std::shared_ptr<data_t>(new data_t());
		eve_p->auto_reset = !manual_reset;
		eve_p->condition = false;

		eve_p->cond = pthread_cond_t();
		eve_p->mutex = pthread_mutex_t();

		do { state = pthread_mutex_init(eve_p->as_mutex_ptr(), nullptr); } while (state == -1 && (errno == ENOMEM || errno == EAGAIN));
		NHTTP_CRITICAL(!state, "fatal error: failed to create mutex!");

		do { state = pthread_cond_init(eve_p->as_cond_ptr(), nullptr); } while (state == -1 && (errno == ENOMEM || errno == EAGAIN));
		NHTTP_CRITICAL(!state, "fatal error: failed to create cond-var!");
#endif
	}

	void event_t::signal() {
#if NHTTP_OS_WINDOWS
		SetEvent(*eve_w);
#endif
#if NHTTP_OS_POSIX
		pthread_mutex_lock(eve_p->as_mutex_ptr());
		eve_p->condition = true;

		pthread_cond_signal(eve_p->as_cond_ptr());
		pthread_mutex_unlock(eve_p->as_mutex_ptr());
#endif
	}

	void event_t::unsignal() {
#if NHTTP_OS_WINDOWS
		ResetEvent(*eve_w);
#endif
#if NHTTP_OS_POSIX
		pthread_mutex_lock(eve_p->as_mutex_ptr());
		eve_p->condition = false;

		pthread_cond_signal(eve_p->as_cond_ptr());
		pthread_mutex_unlock(eve_p->as_mutex_ptr());
#endif
	}

	bool event_t::wait() {
#if NHTTP_OS_WINDOWS
		return !WaitForSingleObject(*eve_w, uint32_t(-1));
#endif
#if NHTTP_OS_POSIX
		pthread_mutex_lock(eve_p->as_mutex_ptr());

		while (!eve_p->condition) {
			pthread_cond_wait(eve_p->as_cond_ptr(), eve_p->as_mutex_ptr());

			if (eve_p->condition && eve_p->auto_reset) {
				eve_p->condition = false;
				break;
			}
		}

		pthread_mutex_unlock(eve_p->as_mutex_ptr());
		return true;
#endif
	}

	bool event_t::timed_wait(int32_t timeout) {
		if (timeout < 0)
			return wait();

#if NHTTP_OS_WINDOWS
		return !WaitForSingleObject(*eve_w, uint32_t(timeout));
#endif
#if NHTTP_OS_POSIX
		timespec ts;
		int state = 0;

		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += (time_t)(timeout / 1000);
		ts.tv_nsec += (long)(timeout % 1000) * 1000 * 1000;

		anti_timebomb(ts);
		pthread_mutex_lock(eve_p->as_mutex_ptr());

		while (!eve_p->condition) {
			state = pthread_cond_timedwait(eve_p->as_cond_ptr(), eve_p->as_mutex_ptr(), &ts);

			if (eve_p->condition && eve_p->auto_reset) {
				eve_p->condition = false;
				break;
			}

			if (state == -1 && errno == ETIMEDOUT)
				break;
		}

		pthread_mutex_unlock(eve_p->as_mutex_ptr());
		return !state;
#endif
	}

	void event_lite_t::signal() {
		spinlock.lock();
		condition.store(true);

		if (event)
			(*event).signal();
		spinlock.unlock();
	}

	void event_lite_t::unsignal() {
		spinlock.lock();
		condition.store(false);

		if (event)
			(*event).unsignal();
		spinlock.unlock();
	}

	bool event_lite_t::wait() {
		spinlock.lock();
		if (condition) {
			if (!manual_reset)
				condition = false;

			spinlock.unlock();
			return true;
		}

		while (true) {
			if (!event)
				event = event_t(false, true);

			++waiters;
			spinlock.unlock();

			if ((*event).wait()) {
				spinlock.lock();

				if (condition) {
					if (!manual_reset)
						condition.store(false);

					if (!--waiters)
						event.unset();

					spinlock.unlock();
					return true;
				}

				spinlock.unlock();
			}

			spinlock.lock();
			--waiters;
		}

		spinlock.unlock();
		return false;
	}

	bool event_lite_t::try_wait() {
		spinlock.lock();

		if (condition) {
			if (!manual_reset)
				condition.store(false);

			spinlock.unlock();
			return true;
		}

		spinlock.unlock();
		return false;
	}

	bool event_lite_t::timed_wait(int32_t timeout) {
		if (timeout < 0)
			return wait();

		else if (timeout == 0)
			return try_wait();

		double msec = (clock() * 1.0 / CLOCKS_PER_SEC) * 1000;

		spinlock.lock();
		if (condition) {
			if (!manual_reset)
				condition.store(false);

			spinlock.unlock();
			return true;
		}

		while (timeout > 0) {
			if (!event)
				event = event_t(false, true);

			++waiters;
			spinlock.unlock();

			if ((*event).timed_wait(timeout)) {
				spinlock.lock();

				if (condition) {
					if (!manual_reset)
						condition.store(false);

					if (!--waiters)
						event.unset();

					spinlock.unlock();
					return true;
				}

				spinlock.unlock();

				/* subtracts delta milliseconds from timeout. */
				double now = (clock() * 1.0 / CLOCKS_PER_SEC) * 1000;
				double delta = now - msec;

				if (delta < 1) 
					delta = 1;

				msec = now;
				timeout -= int32_t(delta);
			}

			spinlock.lock();
			--waiters;
		}

		spinlock.unlock();
		return false;
	}

}
}