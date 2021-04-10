#pragma once
#include "os/winapi.hpp"
#include "os/posix.hpp"

namespace nhttp {
namespace hal {

	/**
	 * class barrior_t.
	 * wraps mutex object.
	 */
	class NHTTP_API barrior_t {
	private:
#if NHTTP_OS_WINDOWS
		struct handle_w_t;
		std::shared_ptr<handle_w_t> handle_w;
#endif
#if NHTTP_OS_POSIX
		struct handle_p_t;
		std::shared_ptr<handle_p_t> handle_p;
#endif

	public:
		barrior_t();

	public:
		void lock();
		void unlock();
	};
}
}