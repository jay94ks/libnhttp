#pragma once
#include "../http_context.hpp"
#include "../../utils/this_ptr.hpp"

namespace nhttp {
namespace server {
namespace xfwk {

	class xfwk_middleware;
	using xfwk_middleware_ptr = std::shared_ptr<xfwk_middleware>;

	class xfwk_target;
	class xfwk_middleware_stack;

	/**
	 * class xfwk_middleware.
	 * middleware for a resource.
	 */
	class NHTTP_API xfwk_middleware {
		friend class xfwk_middleware_stack;

	public:
		typedef http_response_ptr(* next_type)(http_request_ptr request);
		virtual ~xfwk_middleware() { }

	public:
		/* handle target using this middleware. */
		http_response_ptr handle_for(http_request_ptr request, const std::shared_ptr<xfwk_target>& target) const;

	protected:
		/* handle target. */
		virtual http_response_ptr handle(http_request_ptr request, next_type next) const = 0;
	};

	/**
	 * class xfwk_middleware_stack.
	 * stack of middlewares.
	 */
	class NHTTP_API xfwk_middleware_stack : public xfwk_middleware {
	public:
		std::vector<xfwk_middleware_ptr> vec;
		virtual ~xfwk_middleware_stack() { }

	public:
		inline this_ptr<xfwk_middleware_stack> prepend(xfwk_middleware_ptr middleware) { vec.insert(vec.begin(), middleware); return this; }
		inline this_ptr<xfwk_middleware_stack> append(xfwk_middleware_ptr middleware) { vec.push_back(middleware); return this; }

	protected:
		/* handle target. */
		virtual http_response_ptr handle(http_request_ptr request, next_type next) const override;
	};

}
}
}