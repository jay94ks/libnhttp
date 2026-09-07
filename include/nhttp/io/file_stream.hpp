#pragma once

#include "stream.hpp"
#include "../async/io_context.hpp"
#include "../async/thread_pool.hpp"

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
	 */
	class file_stream final : public stream {
	public:
		file_stream(async::io_context& ctx, async::thread_pool& pool, std::FILE* file, std::int64_t size) noexcept;
		~file_stream() override;

		file_stream(const file_stream&) = delete;
		file_stream(file_stream&&) = delete;

	public:
		/* opens `path` in `mode` (fopen semantics); returns nullptr on failure. */
		static async::task<std::unique_ptr<file_stream>> open(
			async::io_context& ctx, async::thread_pool& pool, std::string path, std::string mode);

	public:
		std::int64_t get_length() const override { return size_; }
		bool can_seek() const noexcept override { return true; }

		async::task<std::int64_t> seek(std::int64_t offset, seek_origin origin) override;
		async::task<std::size_t> read(void* buf, std::size_t n) override;
		async::task<std::size_t> write(const void* buf, std::size_t n) override;
		async::task<void> flush() override;
		async::task<void> close() override;

	private:
		async::io_context* ctx_;
		async::thread_pool* pool_;
		std::FILE* file_;
		std::int64_t size_;
	};

}
