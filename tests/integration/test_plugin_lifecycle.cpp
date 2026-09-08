#include <catch2/catch_test_macros.hpp>

#include "nhttp/server/listener.hpp"
#include "nhttp/plugin/plugin_manager.hpp"
#include "../support/raw_http_client.hpp"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace nhttp::server;
using namespace nhttp::platform;
using namespace nhttp::async;
using nhttp::plugin::plugin;
using nhttp::plugin::plugin_manager;
using nhttp_test::raw_http_client;

namespace {

	class lifecycle_plugin final : public plugin {
	public:
		lifecycle_plugin(std::vector<std::string>& log, std::mutex& mtx) : log_(log), mtx_(mtx) { }

		task<void> on_init(listener&, io_context& ctx) override {
			// a real async op through the bootstrap context (not just a
			// synchronous body) -- proves plugin_manager's short-lived
			// io_context genuinely drives async work, not just plain calls.
			co_await ctx.sleep_for(std::chrono::milliseconds(1));
			std::lock_guard<std::mutex> lock(mtx_);
			log_.push_back("init");
		}

		task<void> on_deinit(listener&, io_context& ctx) override {
			co_await ctx.sleep_for(std::chrono::milliseconds(1));
			std::lock_guard<std::mutex> lock(mtx_);
			log_.push_back("deinit");
		}

	private:
		std::vector<std::string>& log_;
		std::mutex& mtx_;
	};

}

TEST_CASE("plugin_manager runs on_init before requests are served and on_deinit after the server stops", "[plugin][plugin_manager]") {
	std::vector<std::string> log;
	std::mutex mtx;

	params p;
	p.io_worker_count = 1;
	p.blocking_pool_size = 1;
	listener srv(p);

	srv.set_handler([&](request&) -> task<response> {
		{
			std::lock_guard<std::mutex> lock(mtx);
			log.push_back("request");
		}
		co_return make_response("ok");
	});

	REQUIRE(srv.listen(endpoint(ip_address::any_v4(), 0)));

	plugin_manager plugins;
	plugins.attach(std::make_shared<lifecycle_plugin>(log, mtx));

	plugins.init_all(srv);

	std::thread run_thread([&] { srv.run(); });

	raw_http_client client(ip_version::v4, srv.local_endpoint()->port());
	auto resp = client.send("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(resp.status == 200);

	srv.stop();
	run_thread.join();

	plugins.deinit_all(srv);

	REQUIRE(log == std::vector<std::string>{"init", "request", "deinit"});
}
