#pragma once

#include <cstdint>
#include <string>

namespace nhttp::platform {

	/**
	 * class file_mapping.
	 * a read-only, *windowed* memory mapping into a file — POSIX mmap(2) /
	 * Windows CreateFileMapping+MapViewOfFile — see PLAN.md's P5. Opening a
	 * file only opens a native handle to it (cheap, and gives total_size());
	 * no memory is actually mapped until ensure_window() is first called,
	 * and never more of the file than one bounded-size window at a time —
	 * mapping a multi-gigabyte file in its entirety just to serve a small
	 * byte-Range request (or even a single sequential read of a huge file)
	 * would be wasteful at best and, on a 32-bit target, could exhaust the
	 * address space outright. ensure_window() slides this single window to
	 * cover whatever range is actually requested, remapping only when the
	 * request falls outside what's currently mapped — a purely sequential
	 * reader (io::file_stream's normal case) ends up remapping only once
	 * every `window_capacity` bytes, and a file smaller than
	 * `window_capacity` (the overwhelming majority of real static files)
	 * ends up mapped in a single window covering the whole thing, same as
	 * before this class had windowing at all.
	 *
	 * Owns an independent native handle to the file (a fresh open, never
	 * shared with any std::FILE* a caller like io::file_stream also holds
	 * on the same path) and unmaps/closes everything on destruction.
	 */
	class file_mapping {
	public:
		// the largest single window this class will ever map at once. Chosen
		// generously above any real static file this codebase's benchmarks
		// or tests use (so those still end up mapped in one window, same
		// behavior as an unwindowed full-file mapping), while still bounding
		// how much address space/page-table setup one file ever costs.
		static constexpr std::int64_t window_capacity = 4 * 1024 * 1024; // 4 MiB

		file_mapping() noexcept = default;
		~file_mapping();

		file_mapping(const file_mapping&) = delete;
		file_mapping& operator=(const file_mapping&) = delete;

		file_mapping(file_mapping&& other) noexcept;
		file_mapping& operator=(file_mapping&& other) noexcept;

	public:
		/* opens a read-only handle to `path` — no mapping yet. invalid() on
		 * any failure (including a zero-length file: there's nothing
		 * mappable, and callers should already have their own fast path for
		 * "nothing to read" that doesn't need one). */
		static file_mapping open(const std::string& path) noexcept;

		bool valid() const noexcept { return total_size_ > 0; }
		std::int64_t total_size() const noexcept { return total_size_; }

		/* ensures the current window covers [offset, offset + length) —
		 * remapping (still bounded to window_capacity, never the whole
		 * file) only if it doesn't already — and returns a pointer to byte
		 * `offset` of the file, valid for at least `length` bytes. nullptr
		 * if `offset + length` exceeds total_size(), `length` is negative,
		 * or the underlying platform mapping call fails. The returned
		 * pointer is only valid until the next ensure_window() call or this
		 * object's destruction — callers must finish using it (e.g. the
		 * memcpy it exists for) before calling this again. */
		const void* ensure_window(std::int64_t offset, std::int64_t length) noexcept;

	private:
		void reset() noexcept;
		bool remap(std::int64_t aligned_start, std::int64_t size) noexcept;

		void* window_data_ = nullptr; // base of the currently mapped window (aligned_start-relative), or nullptr.
		std::int64_t window_start_ = 0;
		std::int64_t window_size_ = 0;

		std::int64_t total_size_ = 0;

		// POSIX: a plain fd — `int` is already OS-agnostic, no header needed
		// to name it. Windows: two HANDLEs (the open file and the mapping
		// object — CreateFileMapping's object covers the whole file cheaply;
		// MapViewOfFile is what's actually windowed), stored as `void*` so
		// <windows.h> never leaks into this public header (HANDLE is itself
		// just a `void*` typedef).
#if defined(_WIN32)
		void* file_handle_ = nullptr;
		void* mapping_handle_ = nullptr;
#else
		int fd_ = -1;
#endif
	};

}
