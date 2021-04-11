#pragma once
#include "xfwk_path.hpp"
#include "../http_context.hpp"
#include "../../assert.hpp"
#include "../../utils/this_ptr.hpp"
#include "../../utils/lambda_t.hpp"
#include "xfwk_facade.hpp"

namespace nhttp {
namespace server {
namespace xfwk {

	class xfwk_route;
	using xfwk_route_ptr = std::shared_ptr<xfwk_route>;

	class xfwk_router;
	class xfwk_param_route;
	class xfwk_static_route;
	class xfwk_wildcard_route;

	class xfwk_target;
	class xfwk_middleware_stack;

	/**
	 * class xfwk_route_state.
	 * for storing route states.
	 */
	class NHTTP_API xfwk_route_state {
	public:
		xfwk_route_ptr route;
		std::map<std::string, std::string> captures;
		std::string path_remainder;
		int32_t depth = 0;
	};

	/**
	 * get route state of.
	 */
	inline xfwk_route_state& route_of(http_request_ptr context) {
		NHTTP_DEBUG(
			auto state_ptr = context->get_tag_ptr<xfwk_route_state>();

			NHTTP_RANGE_ASSERT(state_ptr,
				"the context have no route state!"
			);

			return *state_ptr;
		);

		NHTTP_RELEASE(
			return *context->get_tag_ptr<xfwk_route_state>();
		);
	}

	/**
	 * class xfwk_route.
	 * represents routable node.
	 */
	class NHTTP_API xfwk_route 
		: public std::enable_shared_from_this<xfwk_route>,
		  public xfwk_facade
	{
	public:
		typedef lambda_t<bool(const std::string&)> predicate_type;

	private:
		int32_t _type;

	protected:
		std::string name;
		std::vector<xfwk_route_ptr> children;
		std::vector<xfwk_route_ptr> params;
		xfwk_route_ptr wildcard;

	private:
		std::shared_ptr<xfwk_target> target;
		std::shared_ptr<xfwk_middleware_stack> middlewares;

	protected:
		xfwk_route(int32_t type) : _type(type) { }
		xfwk_route(int32_t type, const std::string& name)
			: _type(type), name(name) { }

	public:
		virtual ~xfwk_route() { }

	protected:
		/* interface -- xfwk_facade -- */
		virtual std::shared_ptr<xfwk_route> get_route() const override {
			return std::const_pointer_cast<xfwk_route>(shared_from_this());
		}

	public:
		/**
		 * -- xfwk_facade_middlewares interface --
		 * get or set middleware stack. @warn: DO NOT set under routing process!
		 */
		virtual std::shared_ptr<xfwk_middleware_stack> get_middlewares() const override { return middlewares; }
		virtual void set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) override { this->middlewares = middlewares; }

	public:
		/* determines this route is static or not. */
		inline bool is_static() const { return _type == 0; }

		/* determines this route is param or not. */
		inline bool is_param() const { return _type == 1; }

		/* determines this route is wildcard or not. */
		inline bool is_wildcard() const { return _type == 2; }

		/* determines this route is root node or not. */
		inline bool is_root() const { return _type == 3; }

		/* get name of this route. */
		inline const std::string& get_name() const { return name; }

	public:
		/* get child or create child if nothing. */
		xfwk_route_ptr child(const std::string& path);

		/* get child by name. */
		xfwk_route_ptr get_child(const std::string& name) const;

		/* remove all children. */
		this_ptr<xfwk_route> clear();

		/* create new child by name. */
		xfwk_route_ptr new_child(const std::string& name);

		/* add child and if the name already taken, replaces it. */
		this_ptr<xfwk_route> add_child(xfwk_route_ptr child);

		/* add static child and if the name already taken, replaces it. */
		std::shared_ptr<xfwk_static_route> add_static(const std::string& name);

		/* add parameter child and if the name already taken, replaces it. */
		std::shared_ptr<xfwk_param_route> add_param(const std::string& name, predicate_type predicate);

		/* add wildcard child and if the name already taken, replaces it. */
		std::shared_ptr<xfwk_wildcard_route> add_wildcard();

		/* remove child. */
		this_ptr<xfwk_route> remove_child(const std::string& name);

		/* find param route from this and children. */
		std::shared_ptr<xfwk_param_route> find_param(const std::string& name) const;

		/* make this route to router. */
		std::shared_ptr<xfwk_router> as_router();

		/* make this route to router. */
		std::shared_ptr<xfwk_router> as_router(std::string mapping_path);

		/* get target resource. */
		inline std::shared_ptr<xfwk_target> get_target() const { return target; }

		/* set target resource. */
		this_ptr<xfwk_route> set_target(std::shared_ptr<xfwk_target> target) { this->target = target; return this; }

	private:
		static ssize_t binary_search(const std::vector<xfwk_route_ptr>& children, const std::string& name);
		static ssize_t binary_search_insert(const std::vector<xfwk_route_ptr>& children, const std::string& name);

	protected:
		virtual bool test(const std::string& in_name) const = 0;

	public:
		/* route the path to target. */
		virtual bool route(xfwk_route_state& state, xfwk_path& path) const;
	};

	/**
	 * class xfwk_static_route.
	 * represents static named route node.
	 */
	class NHTTP_API xfwk_static_route : public xfwk_route {
	public:
		xfwk_static_route(const std::string& name)
			: xfwk_route(0, name) { }

	protected:
		virtual bool test(const std::string& in_name) const override {
			return name.size() == in_name.size() && name == in_name;
		}
	};

	/**
	 * class xfwk_param_route.
	 * represents parameter route node.
	 */
	class NHTTP_API xfwk_param_route : public xfwk_route {
	private:
		mutable predicate_type predicate;

	public:
		xfwk_param_route(const std::string& name)
			: xfwk_route(1, name) { }

		xfwk_param_route(const std::string& name, predicate_type predicate)
			: xfwk_route(1, name), predicate(std::move(predicate)) { }

	protected:
		virtual bool test(const std::string& in_name) const override {
			return !predicate || predicate(in_name);
		}

		/* route the path to target and capture current name as parameter. */
		virtual bool route(xfwk_route_state& state, xfwk_path& path) const override;

	public:
		/* set predicate functor. */
		inline this_ptr<xfwk_param_route> set_predicate(predicate_type predicate) {
			std::swap(this->predicate, predicate);
			return this;
		}
	};

	/**
	 * class xfwk_wildcard_route.
	 * represents wildcard route node.
	 * @note: wildcard node can have any children, but, no route-able to children.
	 */
	class NHTTP_API xfwk_wildcard_route : public xfwk_route {
	public:
		xfwk_wildcard_route() : xfwk_route(2) { }

	protected:
		virtual bool test(const std::string& in_name) const override { return true; }

	public:
		/* route the path to target and capture path remainder. */
		virtual bool route(xfwk_route_state& state, xfwk_path& path) const override;
	};
}
}
}