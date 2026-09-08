#pragma once

#include "plugin.hpp"

#include <vector>

namespace nhttp::server {
	class listener; // see plugin.hpp's own forward declaration for why.
}

namespace nhttp::plugin {

	/**
	 * class plugin_manager.
	 * owns a server::listener's set of attached plugins and drives their
	 * server-lifecycle hooks (plugin::on_init/on_deinit) -- NOT the per-request
	 * scope hooks (on_begin/on_end), which are wired directly onto whichever
	 * routes need them via plugin_scope (see plugin_scope.hpp) and never touch
	 * this class.
	 *
	 * deliberately does not live inside `listener` itself: plugin_scope above
	 * already depends on router::middleware, and router already depends on
	 * server -- a dependency from server/listener.hpp back into plugin would
	 * create a cycle. Call init_all()/deinit_all() from user code around
	 * listener::run(), which already blocks until stop() is called:
	 *
	 *   listener srv(p);
	 *   plugin::plugin_manager plugins;
	 *   plugins.attach(mysql_plugin);
	 *
	 *   plugins.init_all(srv);
	 *   srv.run();              // blocks until stop()
	 *   plugins.deinit_all(srv);
	 */
	class plugin_manager {
	public:
		void attach(plugin_ptr p) { plugins_.push_back(std::move(p)); }

		/* runs on_init() for every attached plugin, in attach() order, on a
		 * short-lived internal io_context (so a plugin can do real async I/O,
		 * e.g. connecting to a database) -- blocks the calling thread until
		 * every one completes. */
		void init_all(server::listener& srv);

		/* runs on_deinit() for every attached plugin, in REVERSE attach()
		 * order, same as init_all() otherwise. */
		void deinit_all(server::listener& srv);

	private:
		std::vector<plugin_ptr> plugins_;
	};

}
