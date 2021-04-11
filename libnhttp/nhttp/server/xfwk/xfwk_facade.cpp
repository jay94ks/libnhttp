#include "xfwk_facade.hpp"
#include "xfwk_route.hpp"
#include "xfwk_target.hpp"
#include "xfwk_middleware.hpp"
#include <set>

namespace nhttp {
namespace server {
namespace xfwk {

	this_ptr<xfwk_facade> xfwk_facade::any(std::shared_ptr<xfwk_target> target) {
		get_route()->set_target(target);
		return this;
	}

	this_ptr<xfwk_facade> xfwk_facade::any(const std::string& path, std::shared_ptr<xfwk_target> target) {
		get_route()->child(path)->set_target(target);
		return this;
	}

	this_ptr<xfwk_facade> xfwk_facade::method(const http_method& method, std::shared_ptr<xfwk_target> new_target) {
		auto route = get_route();
		auto target = route->get_target();

		if (!target) {
			route->set_target(target = std::make_shared<xfwk_unified_target>());
		}

		if (!target->is_unifed()) {
			/* if the target isn't unified, replace it to unified. */
			auto rep_target = std::make_shared<xfwk_unified_target>();
			route->set_target(rep_target)->any(target);

			target = rep_target;
		}

		/* and then, set target for the method. */
		std::dynamic_pointer_cast<xfwk_unified_target>(target)
			->set_target_for(method, new_target);

		return this;
	}

	this_ptr<xfwk_facade> xfwk_facade::method(const http_method& method, const std::string& path, std::shared_ptr<xfwk_target> new_target) {
		get_route()->child(path)->method(method, new_target);
		return this;
	}

	std::shared_ptr<xfwk_target> xfwk_facade::method(const http_method& method) {
		auto route = get_route();
		auto target = route->get_target();

		if (!target)				return nullptr;
		if (!target->is_unifed())	return target;

		return std::dynamic_pointer_cast<xfwk_unified_target>(target)->get_target_for(method);
	}

	std::shared_ptr<xfwk_target> xfwk_facade::method(const http_method& method, const std::string& path) {
		auto route = get_route()->child(path);
		auto target = route->get_target();

		if (!target)				return nullptr;
		if (!target->is_unifed())	return target;

		return std::dynamic_pointer_cast<xfwk_unified_target>(target)->get_target_for(method);
	}

	this_ptr<xfwk_facade> xfwk_facade::param(const std::string& name, lambda_t<bool(const std::string&)> predicate) {
		auto route = get_route()->find_param(name);

		NHTTP_DEBUG(
			NHTTP_ENSURE(route,
				"no such parameter node set! release binary will not check its validity!"
			);

			if (route) {
				route->set_predicate(predicate);
			}
		);

		NHTTP_RELEASE( route->set_predicate(predicate); );
		return this;
	}

	this_ptr<xfwk_facade> xfwk_facade::prepend(std::shared_ptr<xfwk_middleware> middleware) {
		auto mstack = get_middlewares();

		if (!mstack) {
			mstack = std::make_shared<xfwk_middleware_stack>();
			set_middlewares(mstack);
		}

		mstack->prepend(middleware);
		return this;
	}

	this_ptr<xfwk_facade> xfwk_facade::append(std::shared_ptr<xfwk_middleware> middleware) {
		auto mstack = get_middlewares();

		if (!mstack) {
			mstack = std::make_shared<xfwk_middleware_stack>();
			set_middlewares(mstack);
		}

		mstack->append(middleware);
		return this;
	}

	std::shared_ptr<xfwk_middleware_stack> xfwk_facade_proxy::get_middlewares() const {
		return get_route()->get_middlewares();
	}

	void xfwk_facade_proxy::set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) {
		get_route()->set_middlewares(middlewares);
	}

	class xfwk_facade_group_capturer : public xfwk_facade_proxy {
		typedef xfwk_facade_proxy super_type;

	public:
		std::set<std::shared_ptr<xfwk_facade>> captures;

	public:
		xfwk_facade_group_capturer(std::shared_ptr<xfwk_route> route)
			: xfwk_facade_proxy(route) { }

	public:
		virtual this_ptr<xfwk_facade> any(std::shared_ptr<xfwk_target> target) override {
			captures.emplace(get_route());
			super_type::any(target);

			return this;
		}

		virtual this_ptr<xfwk_facade> any(const std::string& path, std::shared_ptr<xfwk_target> target) override {
			auto child = get_route()->child(path);

			captures.emplace(child);
			child->set_target(target);

			return this;
		}

		virtual this_ptr<xfwk_facade> method(const http_method& _method, std::shared_ptr<xfwk_target> target) override {
			captures.emplace(get_route());
			super_type::method(_method, target);

			return this;
		}

		virtual this_ptr<xfwk_facade> method(const http_method& _method, const std::string& path, std::shared_ptr<xfwk_target> target) override {
			auto child = get_route()->child(path);

			captures.emplace(child);
			child->method(_method, target);

			return this;
		}

		virtual std::shared_ptr<xfwk_facade> group(lambda_t<void(std::shared_ptr<xfwk_facade>)> lambda) override {
			auto facade = super_type::group(std::move(lambda));

			captures.emplace(facade);

			return facade;
		}
	};

	class xfwk_facade_group_impl : public xfwk_facade {
	public:
		std::shared_ptr<xfwk_facade_group_capturer> facades;

	protected:
		/* get route of base path. */
		virtual std::shared_ptr<xfwk_route> get_route() const override { return facades->get_route(); }

	protected:
		/* get or set middleware stack. @warn: DO NOT set under routing process! */
		virtual std::shared_ptr<xfwk_middleware_stack> get_middlewares() const override { return get_route()->get_middlewares(); }
		virtual void set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) override { get_route()->set_middlewares(middlewares); }

	public:
		virtual this_ptr<xfwk_facade> any(std::shared_ptr<xfwk_target> target) override {
			for (auto each : facades->captures) { each->any(target); }
			return this;
		}

		virtual this_ptr<xfwk_facade> any(const std::string& path, std::shared_ptr<xfwk_target> target) override {
			for (auto each : facades->captures) { each->any(path, target); }
			return this;
		}

		virtual this_ptr<xfwk_facade> method(const http_method& _method, std::shared_ptr<xfwk_target> target) override {
			for (auto each : facades->captures) { each->method(_method, target); }
			return this;
		}

		virtual this_ptr<xfwk_facade> method(const http_method& _method, const std::string& path, std::shared_ptr<xfwk_target> target) override {
			for (auto each : facades->captures) { each->method(_method, path, target); }
			return this;
		}

		virtual std::shared_ptr<xfwk_facade> group(lambda_t<void(std::shared_ptr<xfwk_facade>)> lambda) override {
			std::shared_ptr<xfwk_facade_group_impl> out_facade;

			for (auto each : facades->captures) {
				auto impl = std::dynamic_pointer_cast<xfwk_facade_group_impl>(each->group(lambda));

				if (!out_facade)
					 out_facade = impl;

				else {
					for (auto each : impl->facades->captures) {
						out_facade->facades->captures.emplace(each);
					}
				}
			}

			return out_facade;
		}

		/* prepend or append a middleware on the stack. */
		virtual this_ptr<xfwk_facade> prepend(std::shared_ptr<xfwk_middleware> middleware) override {
			for (auto each : facades->captures) { each->prepend(middleware); }
			return this;
		}

		virtual this_ptr<xfwk_facade> append(std::shared_ptr<xfwk_middleware> middleware) override {
			for (auto each : facades->captures) { each->prepend(middleware); }
			return this;
		}
	};

	std::shared_ptr<xfwk_facade> xfwk_facade::group(lambda_t<void(std::shared_ptr<xfwk_facade>)> lambda) {
		std::shared_ptr<xfwk_facade_group_impl> facade = std::make_shared<xfwk_facade_group_impl>();

		facade->facades = std::make_shared<xfwk_facade_group_capturer>(get_route());
		lambda(std::dynamic_pointer_cast<xfwk_facade>(facade->facades));

		return facade;
	}

	std::shared_ptr<xfwk_facade> xfwk_facade_group::group(lambda_t<void(std::shared_ptr<xfwk_facade>)> lambda) {
		std::shared_ptr<xfwk_facade_group_impl> facade = std::make_shared<xfwk_facade_group_impl>();

		facade->facades = std::make_shared<xfwk_facade_group_capturer>(get_route());
		lambda(std::dynamic_pointer_cast<xfwk_facade>(facade->facades));

		return facade;
	}

	this_ptr<xfwk_facade_middlewares> xfwk_facade_middlewares::prepend(std::shared_ptr<xfwk_middleware> middleware) {
		auto mstack = get_middlewares();

		if (!mstack) {
			mstack = std::make_shared<xfwk_middleware_stack>();
			set_middlewares(mstack);
		}

		mstack->prepend(middleware);
		return this;
	}

	this_ptr<xfwk_facade_middlewares> xfwk_facade_middlewares::append(std::shared_ptr<xfwk_middleware> middleware) {
		auto mstack = get_middlewares();

		if (!mstack) {
			mstack = std::make_shared<xfwk_middleware_stack>();
			set_middlewares(mstack);
		}

		mstack->append(middleware);
		return this;
	}
}
}
}