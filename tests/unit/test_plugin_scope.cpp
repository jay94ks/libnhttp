#include <catch2/catch_test_macros.hpp>

#include "nhttp/plugin/plugin_scope.hpp"
#include "nhttp/async/sync_wait.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace nhttp::router;
using namespace nhttp::server;
using namespace nhttp::async;
using nhttp::plugin::plugin;
using nhttp::plugin::plugin_ptr;
using nhttp::plugin::plugin_scope;

namespace {

	/* stashed into req.tags by recording_plugin::on_begin -- stands in for
	 * something like a pooled DB connection a real plugin would hand the
	 * wrapped handler. */
	struct fake_resource_tag {
		std::string value;
	};

	class recording_plugin final : public plugin {
	public:
		explicit recording_plugin(std::string label) : label_(std::move(label)) { }

		task<void> on_begin(request& req) override {
			events->push_back(label_ + "-begin");
			req.tags.ensure<fake_resource_tag>().value = "resource-from-" + label_;
			co_return;
		}

		task<void> on_end(request&) override {
			events->push_back(label_ + "-end");
			co_return;
		}

		std::shared_ptr<std::vector<std::string>> events = std::make_shared<std::vector<std::string>>();

	private:
		std::string label_;
	};

}

TEST_CASE("plugin_scope calls on_begin before and on_end after a successful handler", "[plugin][plugin_scope]") {
	auto p = std::make_shared<recording_plugin>("mysql");

	middleware_stack stack;
	stack.prepend(std::make_shared<plugin_scope>(p));

	auto t = target_by([](request& req) {
		REQUIRE(req.tags.get<fake_resource_tag>() != nullptr);
		REQUIRE(req.tags.get<fake_resource_tag>()->value == "resource-from-mysql");
		return make_response("ok");
	});

	request req;
	response r = sync_wait(stack.handle(req, t));

	REQUIRE(*p->events == std::vector<std::string>{"mysql-begin", "mysql-end"});
}

TEST_CASE("plugin_scope still calls on_end when the wrapped handler throws, and the exception still propagates", "[plugin][plugin_scope]") {
	auto p = std::make_shared<recording_plugin>("mysql");

	middleware_stack stack;
	stack.prepend(std::make_shared<plugin_scope>(p));

	// a plain (non-coroutine) target: target_by's dispatch calls it directly
	// and wraps the result in a task itself, so an unconditional throw here
	// needs no unreachable "return" after it (unlike a coroutine body, which
	// would need a co_return statement even though it's never reached).
	auto t = target_by([](request&) -> response {
		throw std::runtime_error("handler boom");
	});

	request req;

	REQUIRE_THROWS_AS(sync_wait(stack.handle(req, t)), std::runtime_error);
	REQUIRE(*p->events == std::vector<std::string>{"mysql-begin", "mysql-end"});
}

TEST_CASE("two plugin_scopes on one route nest with the last-prepended one outermost", "[plugin][plugin_scope]") {
	auto shared_events = std::make_shared<std::vector<std::string>>();

	auto a = std::make_shared<recording_plugin>("a");
	auto b = std::make_shared<recording_plugin>("b");
	a->events = shared_events;
	b->events = shared_events;

	middleware_stack stack;
	stack.prepend(std::make_shared<plugin_scope>(a));
	stack.prepend(std::make_shared<plugin_scope>(b)); // prepended second -> outermost

	auto t = target_by([](request&) { return make_response("ok"); });

	request req;
	sync_wait(stack.handle(req, t));

	REQUIRE(*shared_events == std::vector<std::string>{"b-begin", "a-begin", "a-end", "b-end"});
}
