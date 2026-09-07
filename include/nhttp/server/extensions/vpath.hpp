#pragma once

#include "../extension.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace nhttp::server {

	/* per-request stack of "remaining path after the innermost matched vpath
	 * prefix" — pushed/popped by vpath::handle around dispatching its nested
	 * registry, so nested extensions see paths relative to their mount point. */
	struct vpath_tag {
		std::vector<std::string> subpaths;
	};

	/* the current remaining path for `req`: the innermost vpath's remainder,
	 * or the full request path if not nested under any vpath. */
	std::string_view subpath_of(request& req);

	/**
	 * class vpath.
	 * mounts a nested extension_registry under a URL path prefix, narrowing
	 * subpath_of() for everything inside — the same scoping primitive
	 * xfwk_router is built on top of (CONCEPTS.md §4-5), not a separate one.
	 */
	class vpath : public extension {
	public:
		explicit vpath(std::string base_path, std::uint32_t prio = 0x80000000u);

	public:
		void extends(extension_ptr ext);

		std::uint32_t priority() const noexcept override { return priority_; }
		async::task<bool> wants(request& req) override;
		async::task<response> handle(request& req) override;

	protected:
		/* hook for subclasses (the router) to handle if the nested registry declines. */
		virtual async::task<std::optional<response>> on_handle(request& req);

		const std::string& base_path() const noexcept { return base_path_; }

	private:
		std::string base_path_;
		std::uint32_t priority_;
		extension_registry registry_;
	};

	inline std::shared_ptr<vpath> vpath_for(std::string base_path) {
		return std::make_shared<vpath>(std::move(base_path));
	}

}
