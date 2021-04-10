#pragma once
#include <exception>

namespace nhttp {

	/**
	 * class exception. 
	 * Base class of all exceptions of `seq` namespace.
	 */
	class exception : public std::exception {
	public:
		exception() noexcept { }
#ifdef _MSC_VER
		exception(const char* message) noexcept : std::exception(message) { }
#else
		exception(const char* message) noexcept : _msg(message) { }
		virtual char const* what() const noexcept { return _msg.c_str(); }
	private:
		std::string _msg;
	public:
#endif
		exception(const exception& o) noexcept : std::exception(o) { }
		virtual ~exception() { }
	};

	/**
	 * class uninitialized.
	 * Represents the implementation tried to access uninitialized something.
	 */
	class critical : public exception {
	public:
		critical() noexcept : exception("tried to access on uninitialized!") { }
		critical(const char* message) noexcept : exception(message) { }
		critical(const critical& o) noexcept : exception(o) { }
		virtual ~critical() { }
	};

	/**
	 * class uninitialized.
	 * Represents the implementation tried to access uninitialized something.
	 */
	class uninitialized : public exception {
	public:
		uninitialized() noexcept : exception("tried to access on uninitialized!") { }
		uninitialized(const char* message) noexcept : exception(message) { }
		uninitialized(const uninitialized& o) noexcept : exception(o) { }
		virtual ~uninitialized() { }
	};

	/**
	 * class out_of_range.
	 * Represents the implementation tried to access out of range.
	 */
	class out_of_range : public exception {
	public:
		out_of_range() noexcept : exception("tried to access on impossible range of data!") { }
		out_of_range(const char* message) noexcept : exception(message) { }
		out_of_range(const out_of_range& o) noexcept : exception(o) { }
		virtual ~out_of_range() { }
	};

	/**
	 * NHTTP_ASSERT( condition, message ... )
	 * non-categorized failures.
	 */
#define NHTTP_ASSERT(cond, ...) \
	NHTTP_DEBUG(if (!(cond)) throw ::nhttp::exception(__VA_ARGS__))

	/**
	 * NHTTP_CRITICAL( condition, message ... )
	 * against critical failures.
	 * @note this will not be removed even in debug binaries.
	 */
#define NHTTP_CRITICAL(cond, ...) \
	if (!(cond)) throw ::nhttp::critical(__VA_ARGS__)

	/**
	 * NHTTP_INIT_ASSERT( condition, message ... )
	 * against initialization failure.
	 */
#define NHTTP_INIT_ASSERT(cond, ...) \
	NHTTP_DEBUG(if (!(cond)) throw ::nhttp::uninitialized(__VA_ARGS__))
	
	/**
	 * NHTTP_RANGE_ASSERT( condition, message ... )
	 * against out of ranges.
	 */
#define NHTTP_RANGE_ASSERT(cond, ...) \
	NHTTP_DEBUG(if (!(cond)) throw ::nhttp::out_of_range(__VA_ARGS__))

#ifdef _WIN32 
#define NHTTP_ENSURE(cond, ...) \
	NHTTP_DEBUG(if (!(cond)) __debugbreak())
#else
#define NHTTP_ENSURE(cond, ...)
#endif
}