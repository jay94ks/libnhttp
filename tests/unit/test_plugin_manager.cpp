#include <catch2/catch_test_macros.hpp>

#include "nhttp/plugin/plugin_manager.hpp"
#include "nhttp/server/listener.hpp"

#include <chrono>
#include <string>
#include <vector>

using namespace nhttp::server;
using namespace nhttp::async;
using nhttp::plugin::plugin;
using nhttp::plugin::plugin_manager;

namespace {

	/* plugin_manager never dereferences the listener& it's handed -- it just
	 * forwards it to each plugin's hook -- so these tests use a listener
	 * that's never listen()'d or run(), exercising plugin_manager entirely in
	 * isolation from real sockets/HTTP traffic. */
	class ordered_plugin final : public plugin {
	public:
		ordered_plugin(std::string label, std::vector<std::string>& log) : label_(std::move(label)), log_(log) { }

		task<void> on_init(listener&, io_context&) override {
			log_.push_back(label_ + "-init");
			co_return;
		}

		task<void> on_deinit(listener&, io_context&) override {
			log_.push_back(label_ + "-deinit");
			co_return;
		}

	private:
		std::string label_;
		std::vector<std::string>& log_;
	};

	/* proves plugin_manager's bootstrap io_context is genuinely running (not
	 * just a synchronous call), the same way test_plugin_lifecycle.cpp's
	 * integration test does, but here in isolation -- no real listener/HTTP
	 * traffic involved. */
	class async_probe_plugin final : public plugin {
	public:
		async_probe_plugin(bool& init_flag, bool& deinit_flag) : init_flag_(init_flag), deinit_flag_(deinit_flag) { }

		task<void> on_init(listener&, io_context& ctx) override {
			co_await ctx.sleep_for(std::chrono::milliseconds(1));
			init_flag_ = true;
		}

		task<void> on_deinit(listener&, io_context& ctx) override {
			co_await ctx.sleep_for(std::chrono::milliseconds(1));
			deinit_flag_ = true;
		}

	private:
		bool& init_flag_;
		bool& deinit_flag_;
	};

}

TEST_CASE("plugin_manager runs on_init in attach order and on_deinit in reverse order", "[plugin][plugin_manager]") {
	std::vector<std::string> log;
	listener srv;

	plugin_manager plugins;
	plugins.attach(std::make_shared<ordered_plugin>("a", log));
	plugins.attach(std::make_shared<ordered_plugin>("b", log));
	plugins.attach(std::make_shared<ordered_plugin>("c", log));

	plugins.init_all(srv);
	REQUIRE(log == std::vector<std::string>{"a-init", "b-init", "c-init"});

	plugins.deinit_all(srv);
	REQUIRE(log == std::vector<std::string>{"a-init", "b-init", "c-init", "c-deinit", "b-deinit", "a-deinit"});
}

TEST_CASE("plugin_manager with no attached plugins does nothing on init_all/deinit_all", "[plugin][plugin_manager]") {
	listener srv;
	plugin_manager plugins;

	REQUIRE_NOTHROW(plugins.init_all(srv));
	REQUIRE_NOTHROW(plugins.deinit_all(srv));
}

TEST_CASE("plugin_manager drives on_init/on_deinit on a genuinely running io_context", "[plugin][plugin_manager]") {
	listener srv;
	plugin_manager plugins;

	bool init_ran = false;
	bool deinit_ran = false;
	plugins.attach(std::make_shared<async_probe_plugin>(init_ran, deinit_ran));

	plugins.init_all(srv);
	REQUIRE(init_ran);
	REQUIRE_FALSE(deinit_ran);

	plugins.deinit_all(srv);
	REQUIRE(deinit_ran);
}

TEST_CASE("plugin_manager runs multiple plugins' on_init/on_deinit on the same bootstrap context sequentially", "[plugin][plugin_manager]") {
	// a second, independent check that ordering holds even when every plugin
	// also does a real async op (not just synchronous log pushes) -- so a
	// stray interleaving bug in run_on_fresh_context's driving coroutine
	// would show up as an out-of-order log, not just a missing entry.
	class async_ordered_plugin final : public plugin {
	public:
		async_ordered_plugin(std::string label, std::vector<std::string>& log) : label_(std::move(label)), log_(log) { }

		task<void> on_init(listener&, io_context& ctx) override {
			co_await ctx.sleep_for(std::chrono::milliseconds(1));
			log_.push_back(label_ + "-init");
		}

		task<void> on_deinit(listener&, io_context& ctx) override {
			co_await ctx.sleep_for(std::chrono::milliseconds(1));
			log_.push_back(label_ + "-deinit");
		}

	private:
		std::string label_;
		std::vector<std::string>& log_;
	};

	std::vector<std::string> log;
	listener srv;

	plugin_manager plugins;
	plugins.attach(std::make_shared<async_ordered_plugin>("x", log));
	plugins.attach(std::make_shared<async_ordered_plugin>("y", log));

	plugins.init_all(srv);
	plugins.deinit_all(srv);

	REQUIRE(log == std::vector<std::string>{"x-init", "y-init", "y-deinit", "x-deinit"});
}
