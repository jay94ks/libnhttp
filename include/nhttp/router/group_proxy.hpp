#pragma once

#include "facade.hpp"

#include <vector>

namespace nhttp::router {

	/**
	 * class group_proxy.
	 * the facade returned by facade::group(...): forwards every registration
	 * call through to `inner_`, but records which facade each call actually
	 * touched (the resolved node, not `inner_` itself, for path-qualified
	 * calls) so a later prepend()/append() on this proxy fans out to every
	 * route registered inside the group body at once — see CONCEPTS.md §5's
	 * grouping ergonomic.
	 */
	class group_proxy final : public facade, public std::enable_shared_from_this<group_proxy> {
	public:
		explicit group_proxy(facade_ptr inner) : inner_(std::move(inner)) { }

	public:
		std::shared_ptr<facade> any(target_ptr t) override;
		std::shared_ptr<facade> any(const std::string& path, target_ptr t) override;
		std::shared_ptr<facade> method(const protocol::http_method& m, target_ptr t) override;
		std::shared_ptr<facade> method(const protocol::http_method& m, const std::string& path, target_ptr t) override;
		std::shared_ptr<facade> param(const std::string& name, std::function<bool(std::string_view)> predicate) override;
		std::shared_ptr<facade> prepend(middleware_ptr m) override;
		std::shared_ptr<facade> append(middleware_ptr m) override;
		std::shared_ptr<facade> group(std::function<void(std::shared_ptr<facade>)> body) override;
		std::shared_ptr<facade> resolve(const std::string& path) override;

	private:
		facade_ptr record(facade_ptr touched);

		facade_ptr inner_;
		std::vector<facade_ptr> touched_;
	};

}
