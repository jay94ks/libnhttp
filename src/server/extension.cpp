#include "nhttp/server/extension.hpp"

#include <algorithm>

namespace nhttp::server {

	void extension_registry::add(extension_ptr ext) {
		const auto it = std::upper_bound(extensions_.begin(), extensions_.end(), ext,
			[](const extension_ptr& a, const extension_ptr& b) { return a->priority() < b->priority(); });

		extensions_.insert(it, std::move(ext));
	}

	async::task<std::optional<response>> extension_registry::dispatch(request& req) {
		for (const extension_ptr& ext : extensions_) {
			if (co_await ext->wants(req))
				co_return co_await ext->handle(req);
		}

		co_return std::nullopt;
	}

}
