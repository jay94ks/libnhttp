#include "xfwk_router.hpp"
#include "xfwk_target.hpp"
#include "xfwk_route.hpp"
#include "xfwk_middleware.hpp"

namespace nhttp {
namespace server {
namespace xfwk {

	/**
	 * class xfwk_root_route.
	 * root route for router default.
	 */
	class xfwk_root_route : public xfwk_route {
	public:
		xfwk_root_route() 
			: xfwk_route(3) { }

	protected:
		virtual bool test(const std::string& in_name) const override { return true; }
	};

	xfwk_router::xfwk_router() 
		: root_route(std::make_shared<xfwk_root_route>())
	{
	}

	xfwk_router::xfwk_router(std::string path)
		: http_vpath(std::move(path)), root_route(std::make_shared<xfwk_root_route>())
	{
	}

	bool xfwk_router::on_enter(std::shared_ptr<http_context> context) {
		xfwk_router_link_tag* tag = context->link->ensured_tag<xfwk_router_link_tag>();

		tag->xpaths.push(subpath_of(context));
		tag->routers.push(this);

		return http_vpath::on_enter(context);
	}

	void xfwk_router::on_leave(std::shared_ptr<http_context> context) {
		http_vpath::on_leave(context);

		if (auto* tag = context->link->get_tag_ptr<xfwk_router_link_tag>()) {
			tag->xpaths.pop();
			tag->routers.pop();
		}
	}

	bool xfwk_router::on_handle(std::shared_ptr<http_context> context, extended_t) {
		xfwk_router_link_tag* tag = context->link->get_tag_ptr<xfwk_router_link_tag>();
		
		xfwk_path&			path = link_xpath_of(context);
		xfwk_route_state	state;

		if (context->request->get_target().get_method().is_invalid())
			return false; /* notification for generating error page. */

		/* route path by registered routers. */
		if (root_route->route(state, path) && state.route) {
			http_request_ptr request = context->request;
			http_response_ptr response;

			if (auto target = state.route->get_target()) {
				auto mstack = state.route->get_middlewares();

				/* set route state as request tag. */
				request->set_tag(std::move(state));

				if (mstack)
					 response = mstack->handle_for(request, target);
				else response = target->handle(context->request);

				/* if handled and response generated, send it and close the context. */
				if (response) {
					context->response = response;
					context->close();
					return true;
				}

				/* unset route state if failed to handle. */
				request->unset<xfwk_route_state>();
			}
		}

		return false;
	}

}
}
}