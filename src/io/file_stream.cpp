#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS // fopen() is fine here; no need for fopen_s's different error convention.
#endif

#include "nhttp/io/file_stream.hpp"

#include <algorithm>
#include <cstring>

#if !defined(_WIN32)
#include <sys/types.h>
#endif

namespace nhttp::io {

	namespace {

		/* fseeko/ftello (POSIX, 64-bit-safe) vs _fseeki64/_ftelli64 (MSVC's
		 * equivalent — off_t itself is only 32-bit there, so this avoids it
		 * entirely rather than trying to make it work). */
		int portable_fseek64(std::FILE* f, std::int64_t offset, int whence) noexcept {
#if defined(_WIN32)
			return ::_fseeki64(f, offset, whence);
#else
			return ::fseeko(f, static_cast<off_t>(offset), whence);
#endif
		}

		std::int64_t portable_ftell64(std::FILE* f) noexcept {
#if defined(_WIN32)
			return ::_ftelli64(f);
#else
			return static_cast<std::int64_t>(::ftello(f));
#endif
		}

	}

	file_stream::file_stream(async::io_context& ctx, async::thread_pool& pool, std::FILE* file, std::int64_t size,
		std::string path, bool read_only) noexcept
		: ctx_(&ctx), pool_(&pool), file_(file), size_(size), path_(std::move(path)), read_only_(read_only)
	{
	}

	file_stream::~file_stream() {
		if (file_)
			std::fclose(file_);
	}

	namespace {

		struct opened_file {
			std::FILE* file;
			std::int64_t size;
		};

	}

	async::task<std::unique_ptr<file_stream>> file_stream::open(
		async::io_context& ctx, async::thread_pool& pool, std::string path, std::string mode,
		std::int64_t known_size)
	{
		const opened_file result = co_await pool.run(ctx, [path, mode, known_size]() -> opened_file {
			std::FILE* file = std::fopen(path.c_str(), mode.c_str());

			if (!file)
				return opened_file{ nullptr, 0 };

			if (known_size >= 0)
				return opened_file{ file, known_size };

			const std::int64_t current = portable_ftell64(file);
			portable_fseek64(file, 0, SEEK_END);
			const std::int64_t end = portable_ftell64(file);
			portable_fseek64(file, current, SEEK_SET);
			return opened_file{ file, end };
		});

		if (!result.file)
			co_return nullptr;

		co_return std::make_unique<file_stream>(ctx, pool, result.file, result.size, path, mode == "rb");
	}

	async::task<std::int64_t> file_stream::seek(std::int64_t offset, seek_origin origin) {
		if (mapping_.valid()) {
			// no syscall at all: mapped mode tracks its own read position
			// instead of the FILE*'s (see this class's doc comment for why
			// that's safe — nothing else depends on the FILE*'s own cursor
			// while mapped: native_fd()'s sendfile(2) fast path always
			// passes its own explicit offset, never relying on the fd's
			// current position). Only reachable once read() has already
			// attempted (and succeeded at) mapping at least once — a seek()
			// before that still goes through the real fseek() below, which
			// is exactly what read()'s own lazy mapping attempt later reads
			// back via portable_ftell64() to pick up from the right place.
			std::int64_t base = 0;

			switch (origin) {
			case seek_origin::begin: base = 0; break;
			case seek_origin::current: base = mapped_pos_; break;
			case seek_origin::end: base = size_; break;
			}

			const std::int64_t target = base + offset;

			if (target < 0 || target > size_)
				co_return -1;

			mapped_pos_ = target;
			co_return mapped_pos_;
		}

		std::FILE* f = file_;
		int whence = SEEK_SET;

		switch (origin) {
		case seek_origin::begin: whence = SEEK_SET; break;
		case seek_origin::current: whence = SEEK_CUR; break;
		case seek_origin::end: whence = SEEK_END; break;
		}

		const std::int64_t result = co_await pool_->run(*ctx_, [f, offset, whence]() -> std::int64_t {
			if (portable_fseek64(f, offset, whence) != 0)
				return -1;

			return portable_ftell64(f);
		});

		co_return result;
	}

	namespace {

		struct mapping_probe_result {
			platform::file_mapping mapping;
			std::int64_t pos;
		};

	}

	async::task<std::size_t> file_stream::read(void* buf, std::size_t n) {
		if (!mapping_attempted_) {
			mapping_attempted_ = true;

			// only worth attempting for a read-only open of a non-empty
			// file (see this class's doc comment for why this is lazy —
			// gated here, on the first actual read() call, rather than
			// eagerly in open()) — skip the thread-pool hop entirely rather
			// than doing one just to immediately no-op, for a write-mode
			// stream or a zero-length file (neither of which any real
			// caller in this codebase ever calls read() on anyway).
			if (read_only_ && size_ > 0) {
				std::FILE* f = file_;
				std::string path = path_;

				mapping_probe_result probed = co_await pool_->run(*ctx_, [f, path]() -> mapping_probe_result {
					platform::file_mapping mapping = platform::file_mapping::open(path);

					if (!mapping.valid())
						return mapping_probe_result{ {}, 0 };

					// captures wherever the FILE*'s own cursor already is —
					// any seek()s (or, in principle, reads) that happened
					// before mapping was attempted moved *that* cursor, per
					// seek()'s own doc comment below, so this is what keeps
					// mapped_pos_ correctly picking up from there instead of
					// silently resetting to 0.
					return mapping_probe_result{ std::move(mapping), portable_ftell64(f) };
				});

				mapping_ = std::move(probed.mapping);
				mapped_pos_ = probed.pos;
			}
		}

		if (mapping_.valid()) {
			const std::int64_t remaining = size_ - mapped_pos_;

			if (remaining <= 0)
				co_return 0;

			const std::size_t want = static_cast<std::size_t>(
				std::min<std::int64_t>(remaining, static_cast<std::int64_t>(n)));
			const std::int64_t pos = mapped_pos_;

			// still offloaded — a page fault on a cold page is genuine
			// blocking disk I/O, not a fast path (see this class's doc
			// comment), and ensure_window() itself may need to mmap/munmap a
			// new window — but now a plain memcpy from the OS page cache
			// instead of stdio's fread() and its internal buffering/locking.
			// mapping_'s window state is only ever touched from inside a
			// single in-flight read()/seek() at a time (this object is never
			// shared across concurrent operations), so no locking is needed
			// around it even though this runs on a pool thread.
			const std::size_t copied = co_await pool_->run(*ctx_, [this, buf, pos, want]() -> std::size_t {
				const void* src = mapping_.ensure_window(pos, static_cast<std::int64_t>(want));

				if (!src)
					return 0; // e.g. the file was truncated after opening — treat like EOF rather than reading garbage.

				std::memcpy(buf, src, want);
				return want;
			});

			mapped_pos_ += static_cast<std::int64_t>(copied);
			co_return copied;
		}

		std::FILE* f = file_;

		const std::size_t result = co_await pool_->run(*ctx_, [f, buf, n] {
			return std::fread(buf, 1, n, f);
		});

		co_return result;
	}

	async::task<std::size_t> file_stream::write(const void* buf, std::size_t n) {
		std::FILE* f = file_;

		const std::size_t result = co_await pool_->run(*ctx_, [f, buf, n] {
			return std::fwrite(buf, 1, n, f);
		});

		co_return result;
	}

	async::task<void> file_stream::flush() {
		std::FILE* f = file_;
		co_await pool_->run(*ctx_, [f] { std::fflush(f); });
	}

	async::task<void> file_stream::close() {
		std::FILE* f = file_;
		file_ = nullptr;

		co_await pool_->run(*ctx_, [f] { std::fclose(f); });
	}

	int file_stream::native_fd() const noexcept {
#if defined(_WIN32)
		// a CRT fd, not a raw Win32 HANDLE — matching this function's
		// existing POSIX contract (io::file_stream::native_fd() returns
		// "whatever this platform's native_fd()-consuming code expects", and
		// async_socket::send_file's Windows branch converts this to a real
		// HANDLE via _get_osfhandle() itself, right where TransmitFile needs
		// it).
		return file_ ? ::_fileno(file_) : -1;
#else
		return file_ ? ::fileno(file_) : -1;
#endif
	}

}
