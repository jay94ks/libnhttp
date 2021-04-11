#pragma once
#include "../../types.hpp"
#include "../../protocol/http_method.hpp"
#include "../../utils/this_ptr.hpp"
#include "../../utils/lambda_t.hpp"

namespace nhttp {
namespace server {
namespace xfwk {

	class xfwk_route;
	class xfwk_target;
	class xfwk_middleware;
	class xfwk_middleware_stack;

	class xfwk_facade;
	using xfwk_facade_ptr = std::shared_ptr<xfwk_facade>;

	/**
	 * class xfwk_facade.
	 * provides facades for various HTTP methods.
	 */
	class NHTTP_API xfwk_facade {
	public:
		virtual ~xfwk_facade() { }

	protected:
		/* get route of base path. */
		virtual std::shared_ptr<xfwk_route> get_route() const = 0;

		/* get or set middleware stack. @warn: DO NOT set under routing process! */
		virtual std::shared_ptr<xfwk_middleware_stack> get_middlewares() const = 0;
		virtual void set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) = 0;

	public:
		/* set target to HEAD, OPTIONS, GET, POST, PUT, DELETE, PATCH methods. */
		virtual this_ptr<xfwk_facade> any(std::shared_ptr<xfwk_target> target);
		inline this_ptr<xfwk_facade> head(std::shared_ptr<xfwk_target> target)								{ return method(http_method::HEAD, target); }
		inline this_ptr<xfwk_facade> options(std::shared_ptr<xfwk_target> target)							{ return method(http_method::OPTIONS, target); }
		inline this_ptr<xfwk_facade> get(std::shared_ptr<xfwk_target> target)								{ return method(http_method::GET, target); }
		inline this_ptr<xfwk_facade> post(std::shared_ptr<xfwk_target> target)								{ return method(http_method::POST, target); }
		inline this_ptr<xfwk_facade> put(std::shared_ptr<xfwk_target> target)								{ return method(http_method::PUT, target); }
		inline this_ptr<xfwk_facade> patch(std::shared_ptr<xfwk_target> target)								{ return method(http_method::PATCH, target); }
		inline this_ptr<xfwk_facade> delet(std::shared_ptr<xfwk_target> target)								{ return method(http_method::DELETE, target); }

		/* set target for path to HEAD, OPTIONS, GET, POST, PUT, DELETE, PATCH methods. */
		virtual this_ptr<xfwk_facade> any(const std::string& path, std::shared_ptr<xfwk_target> target);
		inline this_ptr<xfwk_facade> head(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::HEAD, path, target); }
		inline this_ptr<xfwk_facade> options(const std::string path, std::shared_ptr<xfwk_target> target)	{ return method(http_method::OPTIONS, path, target); }
		inline this_ptr<xfwk_facade> get(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::GET, path, target); }
		inline this_ptr<xfwk_facade> post(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::POST, path, target); }
		inline this_ptr<xfwk_facade> put(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::PUT, path, target); }
		inline this_ptr<xfwk_facade> patch(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::PATCH, path, target); }
		inline this_ptr<xfwk_facade> delet(const std::string path, std::shared_ptr<xfwk_target> target)		{ return method(http_method::DELETE, path, target); }

		/* set target for specific method. */
		virtual this_ptr<xfwk_facade> method(const http_method& method, std::shared_ptr<xfwk_target> target);
		virtual this_ptr<xfwk_facade> method(const http_method& method, const std::string& path, std::shared_ptr<xfwk_target> target);

		/* get target of specific method. */
		std::shared_ptr<xfwk_target> method(const http_method& method);
		std::shared_ptr<xfwk_target> method(const http_method& method, const std::string& path);

		/* set parameter's test predicate. */
		this_ptr<xfwk_facade> param(const std::string& name, lambda_t<bool(const std::string&)> predicate);

		/* prepend or append a middleware on the stack. */
		virtual this_ptr<xfwk_facade> prepend(std::shared_ptr<xfwk_middleware> middleware);
		virtual this_ptr<xfwk_facade> append(std::shared_ptr<xfwk_middleware> middleware);

		/* make a grouped route. */
		virtual std::shared_ptr<xfwk_facade> group(lambda_t<void(std::shared_ptr<xfwk_facade>)> lambda);
	};

	/**
	 * class xfwk_facade_group.
	 * provides route->group( ... lambda ... ) facade.
	 */
	class NHTTP_API xfwk_facade_group {
	public:
		virtual ~xfwk_facade_group() { }

	protected:
		/* get route of base path. */
		virtual std::shared_ptr<xfwk_route> get_route() const = 0;

	public:
		/* make a route group. */
		std::shared_ptr<xfwk_facade> group(lambda_t<void(std::shared_ptr<xfwk_facade>)> lambda);
	};

	/**
	 * class xfwk_facade_proxy.
	 * proxy version of xfwk_facade.
	 */
	class NHTTP_API xfwk_facade_proxy : public xfwk_facade {
	private:
		std::shared_ptr<xfwk_route> route;

	public:
		xfwk_facade_proxy(std::shared_ptr<xfwk_route> route) : route(route) { }
		virtual std::shared_ptr<xfwk_route> get_route() const override { return route; }

	protected:
		/* get or set middleware stack. @warn: DO NOT set under routing process! */
		virtual std::shared_ptr<xfwk_middleware_stack> get_middlewares() const override;
		virtual void set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) override;
	};

	/**
	 * class xfwk_facade_middlewares.
	 * provides facades for various HTTP middlewares.
	 */
	class NHTTP_API xfwk_facade_middlewares {
	public:
		virtual ~xfwk_facade_middlewares() { }

	protected:
		/* get or set middleware stack. @warn: DO NOT set under routing process! */
		virtual std::shared_ptr<xfwk_middleware_stack> get_middlewares() const = 0;
		virtual void set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) = 0;

	public:
		/* prepend or append a middleware on the stack. */
		this_ptr<xfwk_facade_middlewares> prepend(std::shared_ptr<xfwk_middleware> middleware);
		this_ptr<xfwk_facade_middlewares> append(std::shared_ptr<xfwk_middleware> middleware);
	};
}
}
}