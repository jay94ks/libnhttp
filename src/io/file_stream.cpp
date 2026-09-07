#include "nhttp/io/file_stream.hpp"

#include <sys/types.h>

namespace nhttp::io {

	file_stream::file_stream(async::io_context& ctx, async::thread_pool& pool, std::FILE* file, std::int64_t size) noexcept
		: ctx_(&ctx), pool_(&pool), file_(file), size_(size)
	{
	}

	file_stream::~file_stream() {
		if (file_)
			std::fclose(file_);
	}

	async::task<std::unique_ptr<file_stream>> file_stream::open(
		async::io_context& ctx, async::thread_pool& pool, std::string path, std::string mode)
	{
		std::FILE* file = co_await pool.run(ctx, [path, mode] {
			return std::fopen(path.c_str(), mode.c_str());
		});

		if (!file)
			co_return nullptr;

		const std::int64_t size = co_await pool.run(ctx, [file]() -> std::int64_t {
			const off_t current = ::ftello(file);
			::fseeko(file, 0, SEEK_END);
			const off_t end = ::ftello(file);
			::fseeko(file, current, SEEK_SET);
			return static_cast<std::int64_t>(end);
		});

		co_return std::make_unique<file_stream>(ctx, pool, file, size);
	}

	async::task<std::int64_t> file_stream::seek(std::int64_t offset, seek_origin origin) {
		std::FILE* f = file_;
		int whence = SEEK_SET;

		switch (origin) {
		case seek_origin::begin: whence = SEEK_SET; break;
		case seek_origin::current: whence = SEEK_CUR; break;
		case seek_origin::end: whence = SEEK_END; break;
		}

		const std::int64_t result = co_await pool_->run(*ctx_, [f, offset, whence]() -> std::int64_t {
			if (::fseeko(f, static_cast<off_t>(offset), whence) != 0)
				return -1;

			return static_cast<std::int64_t>(::ftello(f));
		});

		co_return result;
	}

	async::task<std::size_t> file_stream::read(void* buf, std::size_t n) {
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

}
