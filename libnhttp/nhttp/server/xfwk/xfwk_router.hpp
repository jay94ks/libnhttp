#pragma once
#include "../extensions/http_vpath.hpp"
#include "../http_context.hpp"
#include "xfwk_path.hpp"
#include "xfwk_facade.hpp"

namespace nhttp {
namespace server {
namespace xfwk {

	class xfwk_router;
	using xfwk_router_ptr = std::shared_ptr<xfwk_router>;

	class xfwk_route;
	class xfwk_middleware_stack;

	/**
	 * class xfwk_router_link_tag.
	 * tag for marking which router is used.
	 */
	class NHTTP_API xfwk_router_link_tag {
	public:
		std::stack<xfwk_router*> routers;
		std::stack<xfwk_path> xpaths;
	};

	/**
	 * class xfwk_router_request_tag.
	 * tag for marking which path handled.
	 */
	class NHTTP_API xfwk_router_request_tag {
	public:
		xfwk_path xpath;
	};

	/**
	 * class xfwk_router.
	 * xfwk router: route http path to specified xfwk resources.
	 */
	class NHTTP_API xfwk_router : public http_vpath, public xfwk_facade {
	private:
		std::shared_ptr<xfwk_route> root_route;
		std::shared_ptr<xfwk_middleware_stack> middlewares;
		
	public:
		xfwk_router();
		xfwk_router(std::string path);
		xfwk_router(std::shared_ptr<xfwk_route> route) : root_route(route) { }
		xfwk_router(std::string path, std::shared_ptr<xfwk_route> route)
			: http_vpath(std::move(path)), root_route(route)
		{
		}

		virtual ~xfwk_router() { }

	public:
		/* get root route. */
		virtual std::shared_ptr<xfwk_route> get_route() const override { return root_route; }

	public:
		/**
		 * -- xfwk_facade_middlewares interface --
		 * get or set middleware stack. @warn: DO NOT set under routing process!
		 */
		virtual std::shared_ptr<xfwk_middleware_stack> get_middlewares() const override { return middlewares; }
		virtual void set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) override { this->middlewares = middlewares; }

	protected:
		/* add router tag and remove router tag. */
		virtual bool on_enter(std::shared_ptr<http_context> context) override;
		virtual void on_leave(std::shared_ptr<http_context> context) override;

	protected:
		/* get xfwk_path as reference. */
		inline xfwk_path& link_xpath_of(const std::shared_ptr<http_context>& context) {
			xfwk_router_link_tag* tag = context->link->get_tag_ptr<xfwk_router_link_tag>();

			NHTTP_RANGE_ASSERT(
				tag && tag->xpaths.size(),
				"are you sure the scope is inside of xfwk_router?"
			);

			return tag->xpaths.top();
		}

	protected:
		/* handle http request under base path. */
		virtual bool on_handle(std::shared_ptr<http_context> context, extended_t) override;
	};

	/* get xpath from request. */
	inline xfwk_path& xpath_of(std::shared_ptr<http_request> request) {
		NHTTP_DEBUG(
			auto* tag = request->get_tag_ptr<xfwk_router_request_tag>();

			NHTTP_ENSURE(tag,
				"are you sure the scope is inside of xfwk_router?"
			);

			if (!tag) {
				tag = request->set_tag<xfwk_router_request_tag>();
			}

			return tag->xpath;
		);

		/* on release binary, this will not test the tag is valid or not. */
		NHTTP_RELEASE(
			return request->get_tag_ptr<xfwk_router_request_tag>()->xpath;
		);
	}

}
}
}