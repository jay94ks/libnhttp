#pragma once

#include "../server/request.hpp"
#include "../async/io_context.hpp"
#include "../async/task.hpp"

#include <memory>

namespace nhttp::server {
	class listener; // see plugin::on_init/on_deinit below -- forward-declared to
	                 // keep this header light; only a reference is ever needed here.
}

namespace nhttp::plugin {

	/**
	 * class plugin.
	 * a piece of cross-cutting functionality (e.g. a database connection pool)
	 * that needs both a place to hook the *server's own* lifecycle and a
	 * before/after hook scoped to just the specific routes that use it:
	 *
	 *   - on_init/on_deinit run once each, tied to the server (server::listener)
	 *     this plugin is attached to via plugin_manager::attach() -- see
	 *     plugin_manager.hpp for exactly when. Both also receive a live,
	 *     currently-running async::io_context (plugin_manager's own short-
	 *     lived bootstrap context, not one of listener's workers) so a plugin
	 *     can do real async I/O here (e.g. async::async_socket::connect(...)
	 *     to a database) -- constructing async I/O primitives needs a context
	 *     that's actually being driven by a run() loop somewhere, which
	 *     nothing else available at this point (before listener::run() has
	 *     even started) can provide.
	 *   - on_begin/on_end run once per request, but only for routes explicitly
	 *     wrapped with plugin_scope (see plugin_scope.hpp) -- NOT for every
	 *     request through the server, unlike server::extension. on_begin may
	 *     stash per-request state into `req.tags` (e.g. a pooled connection)
	 *     for the wrapped handler to retrieve; on_end is always called to
	 *     match, even if the handler (or anything nested inside the scope)
	 *     threw -- see plugin_scope::handle().
	 *
	 * all four are non-pure with empty bodies: a plugin overrides only what it
	 * actually needs (e.g. one with no server-wide setup skips on_init/
	 * on_deinit entirely).
	 */
	class plugin {
	public:
		virtual ~plugin() = default;

	public:
		virtual async::task<void> on_init(server::listener&, async::io_context&) { co_return; }
		virtual async::task<void> on_deinit(server::listener&, async::io_context&) { co_return; }

		virtual async::task<void> on_begin(server::request&) { co_return; }
		virtual async::task<void> on_end(server::request&) { co_return; }
	};

	using plugin_ptr = std::shared_ptr<plugin>;

}
