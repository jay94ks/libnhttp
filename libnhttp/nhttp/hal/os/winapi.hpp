#pragma once
#include "../../types.hpp"
#include "../../assert.hpp"

#if defined(_WIN32) || defined(_WIN64)
#define NHTTP_OS_WINDOWS 1
#else
#define NHTTP_OS_WINDOWS 0
#endif

namespace nhttp {
namespace hal {

#if NHTTP_OS_WINDOWS
	NHTTP_API int x_win_close_handle(void* handle);

	/**
	 * class _handle_t. (alias: handle_zb_t, handle_t)
	 * container for windows handle.
	 * @note z is for determining the invalid hanlde value.
	 */
	template<bool z = false>
	class _handle_t {
	private:
		struct value_t {
			void* handle;

			value_t(void* v) : handle(v) { }
			~value_t() {
				if (z && handle) { x_win_close_handle(handle); } // <-- zero inv
				if (!z && handle != ((void*)uint64_t(-1))) { x_win_close_handle(handle); } // <-- non-zero inv
			}
		};

		std::shared_ptr<value_t> value;

	public:
		inline static _handle_t make(void* handle) {
			_handle_t wrapper;
			NHTTP_CRITICAL(
				/**
				 * 1. for zero inv handle, == nullptr: semaphore, event ...
				 * 2. for non-zero inv handle, == ((void*)size_t(-1)): file handle, mmaped...
				 */
				(z && handle) || (!z && handle != ((void*)size_t(-1))),
				"fatal error: failed to create handle!"
			);

			wrapper.value = std::make_shared<value_t>(handle);
			return wrapper;
		}

	public:
		_handle_t(nullptr_t = nullptr) : value(nullptr) { }
		_handle_t(const _handle_t& other) : value(other.value) { }
		_handle_t(_handle_t&& other) : value(std::move(other.value)) { }

		inline _handle_t& operator =(nullptr_t) { value = nullptr; }
		inline _handle_t& operator =(const _handle_t& other) { value = other.value; return *this; }
		inline _handle_t& operator =(_handle_t&& other) { value = std::move(other.value); return *this; }

		/* determines the handle is valid or not. */
		inline operator bool() const { return value; }
		inline bool operator !() const { !(*this); }

		/* get handle value. */
		inline void* operator *() const {
			NHTTP_ENSURE(value, "are you sure to use invalid-handle?");
			if (!value) return z ? nullptr : ((void*)uint64_t(-1));
			return value->handle;
		}
	};

	using handle_t = _handle_t<false>;
	using handle_zb_t = _handle_t<true>;
#endif

}
}