#include "nhttp/plugin/plugin_scope.hpp"

#include <exception>
#include <optional>

namespace nhttp::plugin {

	async::task<server::response> plugin_scope::handle(server::request& req, router::next_fn next) const {
		co_await plugin_->on_begin(req);

		// C++20 forbids co_await inside a catch handler ([expr.await]), so the
		// handler's exception (if any) is only ever *captured* inside a catch
		// block here -- on_end always runs afterward, outside any catch, where
		// co_await is legal again.
		std::exception_ptr eptr;
		std::optional<server::response> result;

		try {
			result = co_await next(req);
		}
		catch (...) {
			eptr = std::current_exception();
		}

		try {
			co_await plugin_->on_end(req);
		}
		catch (...) {
			// a SECOND exception from on_end while the handler already failed
			// is swallowed rather than left to replace/mask the real failure
			// (matching the "destructors don't throw during unwind"
			// convention) -- but if the handler itself succeeded, on_end's
			// failure IS the only error to report, so it propagates.
			if (!eptr)
				eptr = std::current_exception();
		}

		if (eptr)
			std::rethrow_exception(eptr);

		co_return std::move(*result);
	}

}
