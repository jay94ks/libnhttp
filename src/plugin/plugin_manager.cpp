#include "nhttp/plugin/plugin_manager.hpp"
#include "nhttp/async/io_context.hpp"
#include "nhttp/async/sync_wait.hpp"

#include <thread>

namespace nhttp::plugin {

	namespace {

		/* drives `body` (a coroutine that touches `ctx`) to completion,
		 * blocking the calling thread -- reusing io_context::schedule() (hop
		 * onto ctx's own thread first) + async::sync_wait(), the same
		 * cross-thread coroutine hand-off already used by thread_pool::run()
		 * and listener's no-SO_REUSEPORT fallback. A short-lived io_context is
		 * spun up here specifically so plugin lifecycle hooks can do real
		 * async I/O (e.g. connecting to a database) without needing anything
		 * from listener's own (private) worker pool, which isn't running yet
		 * when init_all() needs to run. */
		template<typename F>
		void run_on_fresh_context(F body) {
			async::io_context ctx;
			std::thread runner([&ctx] { ctx.run(); });

			async::sync_wait([&ctx, body = std::move(body)]() mutable -> async::task<void> {
				co_await ctx.schedule();
				co_await body(ctx);
			}());

			ctx.stop();
			runner.join();
		}

	}

	void plugin_manager::init_all(server::listener& srv) {
		if (plugins_.empty())
			return;

		run_on_fresh_context([this, &srv](async::io_context& ctx) -> async::task<void> {
			for (const plugin_ptr& p : plugins_)
				co_await p->on_init(srv, ctx);
		});
	}

	void plugin_manager::deinit_all(server::listener& srv) {
		if (plugins_.empty())
			return;

		run_on_fresh_context([this, &srv](async::io_context& ctx) -> async::task<void> {
			for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it)
				co_await (*it)->on_deinit(srv, ctx);
		});
	}

}
