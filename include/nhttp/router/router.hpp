#pragma once

#include "route.hpp"
#include "../server/extensions/vpath.hpp"

#include <memory>

namespace nhttp::router {

	/* per-request captured route-match state, retrieved via route_of(req). */
	struct route_match_tag {
		route_state state;
	};

	/* the captures/depth from the route that handled `req` — empty if none did
	 * (e.g. the request fell through with no match). */
	const route_state& route_of(server::request& req);

	/**
	 * class router.
	 * a Laravel-style REST router: a trie of routes (route.hpp) mounted as a
	 * listener extension via the same URL-prefix scoping vpath already
	 * provides (CONCEPTS.md §5 — this is not a separate mechanism). forwards
	 * the fluent facade to its root node, but returns itself so chained calls
	 * keep registering siblings under the router.
	 */
	class router final : public server::vpath, public facade, public std::enable_shared_from_this<router> {
	public:
		explicit router(std::string mount_path = "/", std::uint32_t prio = 0x80000000u);

	public:
		std::shared_ptr<facade> any(target_ptr t) override;
		std::shared_ptr<facade> any(const std::string& path, target_ptr t) override;
		std::shared_ptr<facade> method(const protocol::http_method& m, target_ptr t) override;
		std::shared_ptr<facade> method(const protocol::http_method& m, const std::string& path, target_ptr t) override;
		std::shared_ptr<facade> param(const std::string& name, std::function<bool(const std::string&)> predicate) override;
		std::shared_ptr<facade> prepend(middleware_ptr m) override;
		std::shared_ptr<facade> append(middleware_ptr m) override;
		std::shared_ptr<facade> group(std::function<void(std::shared_ptr<facade>)> body) override;
		std::shared_ptr<facade> resolve(const std::string& path) override;

	protected:
		async::task<std::optional<server::response>> on_handle(server::request& req) override;

	private:
		route_ptr root_;
	};

	inline std::shared_ptr<router> make_router(std::string mount_path = "/") {
		return std::make_shared<router>(std::move(mount_path));
	}

}
