#include <catch2/catch_test_macros.hpp>

#include "nhttp/io/file_stream.hpp"
#include "nhttp/async/io_context.hpp"
#include "nhttp/async/thread_pool.hpp"
#include "nhttp/platform/file_mapping.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

using namespace nhttp::async;
using namespace nhttp::io;
using namespace nhttp::platform;

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

	std::string make_pattern(std::size_t size) {
		std::string data(size, '\0');

		for (std::size_t i = 0; i < size; ++i)
			data[i] = static_cast<char>(static_cast<unsigned char>(i % 256));

		return data;
	}

	/* a file bigger than platform::file_mapping::window_capacity (4 MiB) so
	 * reading it end to end (and seeking around inside it) must slide the
	 * mapped window at least twice — see PLAN.md's P5. */
	detached_task run_large_file_windowed_read(io_context& ctx, thread_pool& pool, std::string path,
		std::string expected, bool& sequential_ok, bool& seek_back_ok, bool& seek_forward_ok, std::atomic<bool>& done)
	{
		{
			auto out = co_await file_stream::open(ctx, pool, path, "wb");
			co_await out->write(expected.data(), expected.size());
			co_await out->close();
		}

		auto in = co_await file_stream::open(ctx, pool, path, "rb");

		std::string read_back;
		read_back.reserve(expected.size());

		char chunk[65536];

		for (;;) {
			const std::size_t n = co_await in->read(chunk, sizeof(chunk));

			if (n == 0)
				break;

			read_back.append(chunk, n);
		}

		sequential_ok = (read_back == expected);

		// seek back into an earlier window (window 0) after having read
		// all the way to the end (currently positioned in the last window).
		co_await in->seek(10, seek_origin::begin);
		char small[32] = { 0 };
		const std::size_t back_n = co_await in->read(small, sizeof(small));
		seek_back_ok = (back_n == sizeof(small)) && (std::string(small, back_n) == expected.substr(10, sizeof(small)));

		// seek forward across two window boundaries at once (window 0 -> window 2).
		const std::int64_t far_offset = file_mapping::window_capacity * 2 + 123;
		co_await in->seek(far_offset, seek_origin::begin);
		char far[32] = { 0 };
		const std::size_t far_n = co_await in->read(far, sizeof(far));
		seek_forward_ok = (far_n == sizeof(far))
			&& (std::string(far, far_n) == expected.substr(static_cast<std::size_t>(far_offset), sizeof(far)));

		co_await in->close();
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

TEST_CASE("file_stream slides its mapped window correctly across a file bigger than one window", "[io][file_stream]") {
	const std::string path = (std::filesystem::temp_directory_path() / "nhttp_test_file_stream_large.bin").string();
	std::remove(path.c_str());

	io_context ctx;
	thread_pool pool(2);

	// 3 windows' worth plus change, so sequential reading and the seeks
	// below each cross at least one window boundary.
	const std::string expected = make_pattern(static_cast<std::size_t>(file_mapping::window_capacity * 3 + 4096));

	bool sequential_ok = false;
	bool seek_back_ok = false;
	bool seek_forward_ok = false;
	std::atomic<bool> done{ false };

	run_large_file_windowed_read(ctx, pool, path, expected, sequential_ok, seek_back_ok, seek_forward_ok, done);

	std::thread reactor_thread([&ctx] { ctx.run(); });
	spin_until(done);

	ctx.stop();
	reactor_thread.join();

	std::remove(path.c_str());

	REQUIRE(sequential_ok);
	REQUIRE(seek_back_ok);
	REQUIRE(seek_forward_ok);
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
