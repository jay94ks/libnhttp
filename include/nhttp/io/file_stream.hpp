#pragma once

#include "stream.hpp"
#include "../async/io_context.hpp"
#include "../async/thread_pool.hpp"
#include "../platform/file_mapping.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace nhttp::io {

	/**
	 * class file_stream.
	 * a stream backed by a regular file. Linux epoll doesn't cover regular-file
	 * readiness, so every operation here offloads the actual blocking libc call
	 * to a thread_pool and resumes on the given io_context — see CLAUDE.md's
	 * concurrency invariant ("the reactor thread must never block").
	 *
	 * A read-only ("rb") open can additionally memory-map the file (see
	 * PLAN.md's P5 and `platform::file_mapping`); read()/seek() then work
	 * directly against that mapping — a plain memcpy / in-object position
	 * update — instead of stdio's fread()/fseek(), still offloaded through
	 * the same thread_pool (a page fault on a cold page is genuine blocking
	 * disk I/O, not a fast path, so this still must not run on the reactor
	 * thread). The underlying std::FILE* (and the fd behind it — see
	 * native_fd() below) stays open regardless, unaffected: the mapping is a
	 * purely additive read acceleration, never a replacement for the fd the
	 * sendfile(2) fast path (PLAN.md's P1) needs.
	 *
	 * The mapping is opened *lazily*, on the first actual read() call, not
	 * eagerly in open() — found the hard way (see CLAUDE.md's Phase 16 log):
	 * a whole-file GET never calls read() at all, since P1's sendfile fast
	 * path sends straight from native_fd() without going through this
	 * class's read() — so an eager open() would pay a second, wholly wasted
	 * open()+fstat() for every one of those requests, which is the common
	 * case this codebase actually benchmarks. Only a byte-Range/TLS/chunked
	 * response — the ones sendfile can't take — ever calls read(), and only
	 * those pay the mapping's setup cost, once, on their first read. A
	 * write-mode open, or a read-only open where mapping fails for any
	 * reason (a zero-length file, an unmappable filesystem, ...), simply
	 * falls back to the pre-existing fread()/fseek() path unchanged.
	 */
	class file_stream final : public stream {
	public:
		file_stream(async::io_context& ctx, async::thread_pool& pool, std::FILE* file, std::int64_t size,
			std::string path, bool read_only) noexcept;
		~file_stream() override;

		file_stream(const file_stream&) = delete;
		file_stream(file_stream&&) = delete;

	public:
		/* opens `path` in `mode` (fopen semantics); returns nullptr on failure.
		 * a single thread-pool hop does the fopen and (unless `known_size` is
		 * given) the initial ftell/fseek/ftell size probe together — see
		 * PLAN.md's P2, which found this as two separate round trips. Pass
		 * `known_size` when the caller already has it from its own stat (e.g.
		 * overlay's resolve()) to skip the probe's four syscalls entirely; the
		 * caller is trusting the file hasn't changed size between that stat and
		 * this open, which every caller here already implicitly assumed before
		 * (nothing re-validated size between resolving a path and reading it). */
		static async::task<std::unique_ptr<file_stream>> open(
			async::io_context& ctx, async::thread_pool& pool, std::string path, std::string mode,
			std::int64_t known_size = -1);

	public:
		std::int64_t get_length() const override { return size_; }
		bool can_seek() const noexcept override { return true; }

		async::task<std::int64_t> seek(std::int64_t offset, seek_origin origin) override;
		async::task<std::size_t> read(void* buf, std::size_t n) override;
		async::task<std::size_t> write(const void* buf, std::size_t n) override;
		async::task<void> flush() override;
		async::task<void> close() override;

	public:
		/* the raw POSIX file descriptor backing this stream, for the sendfile(2)
		 * fast path in http1_io.cpp (see PLAN.md's P1) — not meaningful/used on
		 * Windows, where that fast path is not yet implemented (TransmitFile
		 * needs the overlapped-I/O completion model this reactor deliberately
		 * doesn't use for plain reads/writes; see CLAUDE.md's Phase 12 design
		 * note). Returns -1 if unavailable. */
		int native_fd() const noexcept;

	private:
		async::io_context* ctx_;
		async::thread_pool* pool_;
		std::FILE* file_;
		std::int64_t size_;
		std::string path_;
		bool read_only_;
		bool mapping_attempted_ = false;
		platform::file_mapping mapping_;
		std::int64_t mapped_pos_ = 0; // only meaningful/used while mapping_.valid().
	};

}
