#pragma once

#include "plugin.hpp"
#include "../router/middleware.hpp"

namespace nhttp::plugin {

	/**
	 * class plugin_scope.
	 * a router::middleware adapter wrapping a plugin's on_begin/on_end around
	 * whatever's next in the chain (another middleware, or the route's own
	 * target) -- this is the entire mechanism for scoping a plugin to specific
	 * routes: attach it the same way any other middleware is attached
	 * (facade::prepend()/append(), typically inside a group() so it applies to
	 * every route registered there -- see CONCEPTS.md §5 and
	 * examples/nhttpd/main.cpp's add_header_middleware for the identical
	 * pattern this reuses verbatim).
	 *
	 * on_end always runs to match on_begin, even if the wrapped handler (or
	 * anything nested inside it) threw -- so a plugin can rely on it to
	 * release whatever on_begin acquired (e.g. hand a pooled connection back)
	 * regardless of how the handler finished. If on_begin itself throws,
	 * on_end does NOT run (nothing was acquired to release), matching normal
	 * RAII/constructor-throws-so-destructor-never-runs semantics. If the
	 * handler failed AND on_end also throws while cleaning up, on_end's
	 * exception is swallowed so it doesn't replace/mask the handler's real
	 * failure; if the handler succeeded but on_end throws, that IS the only
	 * error to report, so it propagates normally.
	 */
	class plugin_scope final : public router::middleware {
	public:
		explicit plugin_scope(plugin_ptr p) noexcept : plugin_(std::move(p)) { }

	public:
		async::task<server::response> handle(server::request& req, router::next_fn next) const override;

	private:
		plugin_ptr plugin_;
	};

}
