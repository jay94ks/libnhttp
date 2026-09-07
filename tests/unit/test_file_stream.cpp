#include <catch2/catch_test_macros.hpp>

#include "nhttp/io/file_stream.hpp"
#include "nhttp/async/io_context.hpp"
#include "nhttp/async/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

using namespace nhttp::async;
using namespace nhttp::io;

namespace {

	void spin_until(std::atomic<bool>& flag) {
		while (!flag.load(std::memory_order_acquire))
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	detached_task run_file_roundtrip(io_context& ctx, thread_pool& pool, std::string path,
		std::string& read_back, std::int64_t& length, std::string& seeked_tail, std::atomic<bool>& done)
	{
		{
			auto out = co_await file_stream::open(ctx, pool, path, "wb");
			const std::string content = "hello file stream";
			co_await out->write(content.data(), content.size());
			co_await out->close();
		}

		{
			auto in = co_await file_stream::open(ctx, pool, path, "rb");
			length = in->get_length();
			co_await in->read_all(read_back);

			co_await in->seek(6, seek_origin::begin);
			char buf[64] = { 0 };
			const std::size_t n = co_await in->read(buf, sizeof(buf));
			seeked_tail.assign(buf, n);

			co_await in->close();
		}

		done.store(true, std::memory_order_release);
	}

	detached_task run_open_missing(io_context& ctx, thread_pool& pool, std::unique_ptr<file_stream>& result, std::atomic<bool>& done) {
		const std::string missing_path = (std::filesystem::temp_directory_path() / "nhttp_test_definitely_missing.txt").string();
		result = co_await file_stream::open(ctx, pool, missing_path, "rb");
		done.store(true, std::memory_order_release);
	}

}

TEST_CASE("file_stream writes, reads, and seeks a real file via thread_pool offload", "[io][file_stream]") {
	const std::string path = (std::filesystem::temp_directory_path() / "nhttp_test_file_stream.txt").string();
	std::remove(path.c_str());

	io_context ctx;
	thread_pool pool(2);

	std::string read_back;
	std::int64_t length = -1;
	std::string seeked_tail;
	std::atomic<bool> done{ false };

	run_file_roundtrip(ctx, pool, path, read_back, length, seeked_tail, done);

	std::thread reactor_thread([&ctx] { ctx.run(); });
	spin_until(done);

	ctx.stop();
	reactor_thread.join();

	std::remove(path.c_str());

	REQUIRE(length == static_cast<std::int64_t>(std::string("hello file stream").size()));
	REQUIRE(read_back == "hello file stream");
	REQUIRE(seeked_tail == "file stream");
}

TEST_CASE("file_stream::open returns nullptr for a nonexistent file", "[io][file_stream]") {
	io_context ctx;
	thread_pool pool(1);

	std::unique_ptr<file_stream> result;
	std::atomic<bool> done{ false };

	run_open_missing(ctx, pool, result, done);

	std::thread reactor_thread([&ctx] { ctx.run(); });
	spin_until(done);

	ctx.stop();
	reactor_thread.join();

	REQUIRE(result == nullptr);
}
