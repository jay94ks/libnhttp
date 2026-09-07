#include "nhttp/router/router.hpp"

namespace nhttp::router {

	const route_state& route_of(server::request& req) {
		static const route_state empty;
		route_match_tag* tag = req.tags.get<route_match_tag>();
		return tag ? tag->state : empty;
	}

	router::router(std::string mount_path, std::uint32_t prio)
		: server::vpath(std::move(mount_path), prio), root_(std::make_shared<route>(route_kind::root))
	{
	}

	std::shared_ptr<facade> router::any(target_ptr t) {
		root_->any(std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> router::any(const std::string& path, target_ptr t) {
		root_->any(path, std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> router::method(const protocol::http_method& m, target_ptr t) {
		root_->method(m, std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> router::method(const protocol::http_method& m, const std::string& path, target_ptr t) {
		root_->method(m, path, std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> router::param(const std::string& name, std::function<bool(std::string_view)> predicate) {
		root_->param(name, std::move(predicate));
		return shared_from_this();
	}

	std::shared_ptr<facade> router::prepend(middleware_ptr m) {
		root_->prepend(std::move(m));
		return shared_from_this();
	}

	std::shared_ptr<facade> router::append(middleware_ptr m) {
		root_->append(std::move(m));
		return shared_from_this();
	}

	std::shared_ptr<facade> router::group(std::function<void(std::shared_ptr<facade>)> body) {
		return root_->group(std::move(body));
	}

	std::shared_ptr<facade> router::resolve(const std::string& path) {
		return root_->resolve(path);
	}

	async::task<std::optional<server::response>> router::on_handle(server::request& req) {
		route_state state;
		const std::string_view path = server::subpath_of(req);
		const route_ptr matched = root_->route_match(state, path);

		if (!matched)
			co_return std::nullopt;

		req.tags.ensure<route_match_tag>().state = state;

		const target_ptr t = matched->get_target(req.method());

		if (!t) {
			if (matched->has_any_target())
				co_return server::make_response(405);

			co_return std::nullopt;
		}

		co_return co_await matched->middlewares().handle(req, t);
	}

}
