#include "barrior_t.hpp"

#if NHTTP_OS_WINDOWS
#include <Windows.h>
#endif

namespace nhttp {
namespace hal {

#if NHTTP_OS_WINDOWS
	struct barrior_t::handle_w_t {
		CRITICAL_SECTION cs = { 0, };

		handle_w_t() { InitializeCriticalSection(&cs); }
		~handle_w_t() { DeleteCriticalSection(&cs); }
	};
#endif
#if NHTTP_OS_POSIX
	struct barrior_t::handle_p_t {
		pthread_mutex_t mutex;

		handle_p_t() {
			int state;
			pthread_mutexattr_t attr;

			pthread_mutexattr_init(&attr);
			pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

			do { state = pthread_mutex_init(&mutex, &attr); } while (state == -1 && (errno == ENOMEM || errno == EAGAIN));
			NHTTP_CRITICAL(!state, "fatal error: failed to create mutex!");

			pthread_mutexattr_destroy(&attr);
		}

		~handle_p_t() { pthread_mutex_destroy(&mutex); }
	};
#endif

	barrior_t::barrior_t() {
#if NHTTP_OS_WINDOWS
		handle_w = std::make_shared<handle_w_t>();
#endif
#if NHTTP_OS_POSIX
		handle_p = std::make_shared<handle_p_t>();
#endif
	}

	void barrior_t::lock() {
#if NHTTP_OS_WINDOWS
		EnterCriticalSection(&(handle_w->cs));
#endif
#if NHTTP_OS_POSIX
		pthread_mutex_lock(&(handle_p->mutex));
#endif
	}

	void barrior_t::unlock() {
#if NHTTP_OS_WINDOWS
		LeaveCriticalSection(&(handle_w->cs));
#endif
#if NHTTP_OS_POSIX
		pthread_mutex_unlock(&(handle_p->mutex));
#endif
	}

}
}