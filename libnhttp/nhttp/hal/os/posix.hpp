#pragma once
#include "../../types.hpp"

#if (!defined(_WIN32) && !defined(_WIN64))
#	define NHTTP_OS_POSIX 1
#	include <pthread.h>
#else
#	if defined(_MSC_VER) && defined(__INTELLISENSE__)
#		define NHTTP_OS_POSIX 1
#	else
#		define NHTTP_OS_POSIX 0
#	endif
#endif

namespace nhttp {
namespace hal {

#if defined(_MSC_VER) && defined(__INTELLISENSE__)
	/**
	 *	Visual Studio Intellisense helpers
	 */
	struct sem_t { };

	struct pthread_cond_t { };
	struct pthread_mutex_t { };
	struct pthread_condattr_t { };
	struct pthread_mutexattr_t { };
	struct pthread_key_t { };
	struct pthread_mutexattr_t { };

	struct timespec {
		time_t tv_sec; 
		int32_t tv_nsec;
	};

	int pthread_mutex_init(pthread_mutex_t*, const pthread_mutexattr_t*);
	int pthread_mutex_lock(pthread_mutex_t*);
	int pthread_mutex_trylock(pthread_mutex_t*);
	int pthread_mutex_unlock(pthread_mutex_t*);
	int pthread_mutex_destroy(pthread_mutex_t*);

	int pthread_mutexattr_init(pthread_mutexattr_t* attr);
	int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type);
	int pthread_mutexattr_destroy(pthread_mutexattr_t* attr);

	int pthread_cond_init(pthread_cond_t*, const pthread_condattr_t*);
	int pthread_cond_signal(pthread_cond_t* cond);
	int pthread_cond_timedwait(pthread_cond_t*, pthread_mutex_t*, const timespec*);
	int pthread_cond_wait(pthread_cond_t*, pthread_mutex_t*);
	int pthread_cond_destroy(pthread_cond_t*);

	int pthread_key_create(pthread_key_t*, void (*)(void*));
	int pthread_key_delete(pthread_key_t);
	void* pthread_getspecific(pthread_key_t);
	int pthread_setspecific(pthread_key_t, const void* value);

	constexpr int CLOCK_REALTIME = 0;
	constexpr int PTHREAD_MUTEX_RECURSIVE = 0;
	int clock_gettime(int, struct timespec*);
#endif
#if (!defined(_WIN32) && !defined(_WIN64)) || defined(__INTELLISENSE__)
	/**
	 * anti_timebomb( timespec ).
	 * qualify timespec against crash.
	 */
	inline void anti_timebomb(timespec& ts) {
		while (ts.tv_nsec >= 1000000000) { /* 1 second. */
			ts.tv_nsec -= 1000000000;
			++ts.tv_sec;
		}
	}

#endif

}
}