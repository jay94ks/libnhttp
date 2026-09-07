#include "nhttp/router/group_proxy.hpp"

namespace nhttp::router {

	facade_ptr group_proxy::record(facade_ptr touched) {
		touched_.push_back(touched);
		return touched;
	}

	std::shared_ptr<facade> group_proxy::any(target_ptr t) {
		record(inner_)->any(std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> group_proxy::any(const std::string& path, target_ptr t) {
		record(inner_->resolve(path))->any(std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> group_proxy::method(const protocol::http_method& m, target_ptr t) {
		record(inner_)->method(m, std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> group_proxy::method(const protocol::http_method& m, const std::string& path, target_ptr t) {
		record(inner_->resolve(path))->method(m, std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> group_proxy::param(const std::string& name, std::function<bool(std::string_view)> predicate) {
		inner_->param(name, std::move(predicate));
		return shared_from_this();
	}

	std::shared_ptr<facade> group_proxy::prepend(middleware_ptr m) {
		for (const facade_ptr& f : touched_)
			f->prepend(m);

		return shared_from_this();
	}

	std::shared_ptr<facade> group_proxy::append(middleware_ptr m) {
		for (const facade_ptr& f : touched_)
			f->append(m);

		return shared_from_this();
	}

	std::shared_ptr<facade> group_proxy::group(std::function<void(std::shared_ptr<facade>)> body) {
		auto nested = std::make_shared<group_proxy>(shared_from_this());
		body(nested);
		touched_.push_back(nested);
		return nested;
	}

	std::shared_ptr<facade> group_proxy::resolve(const std::string& path) {
		return inner_->resolve(path);
	}

}
