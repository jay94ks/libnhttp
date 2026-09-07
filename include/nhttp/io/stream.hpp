#pragma once

#include "../async/task.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nhttp::io {

	enum class seek_origin { begin, current, end };

	/**
	 * class stream.
	 * an async byte stream. every I/O operation is a coroutine; implementations
	 * that must genuinely block a thread (filesystem I/O) offload internally to
	 * a thread_pool rather than blocking the caller's reactor thread. this is
	 * the one abstraction request/response bodies, static files, and byte-range
	 * views are all expressed through — see CONCEPTS.md's "a stream is a stream
	 * regardless of source" principle.
	 */
	class stream {
	public:
		virtual ~stream() = default;

	public:
		/* -1 if the length isn't knowable up front (e.g. a live connection). */
		virtual std::int64_t get_length() const = 0;
		virtual bool can_seek() const noexcept = 0;

		/* returns the new absolute position, or -1 if the seek is invalid/unsupported. */
		virtual async::task<std::int64_t> seek(std::int64_t offset, seek_origin origin) = 0;

		/* returns the number of bytes actually read; 0 means end of stream. */
		virtual async::task<std::size_t> read(void* buf, std::size_t n) = 0;

		/* returns the number of bytes actually written. */
		virtual async::task<std::size_t> write(const void* buf, std::size_t n) = 0;

		virtual async::task<void> flush() = 0;
		virtual async::task<void> close() = 0;

	public:
		/* reads until end of stream, appending everything to `out`. */
		async::task<void> read_all(std::string& out);
	};

}
