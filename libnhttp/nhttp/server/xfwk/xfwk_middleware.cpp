#include "xfwk_middleware.hpp"
#include "xfwk_target.hpp"
#include <stack>

namespace nhttp {
namespace server {
namespace xfwk {

	class xfwk_middleware_stack;

	/**
	 * struct xfwk_middleware_state.
	 * state which indicates current processing middleware.
	 */
	struct xfwk_middleware_state {
		const xfwk_middleware* middleware;
		xfwk_target_ptr target;

		union {
			void*		ptr;
			uint64_t	u64;
			uint32_t	u32;
			uint16_t	u16;
			uint8_t		u8;
		} data;
	};

	/**
	 * struct xfwk_middleware_stack_state.
	 * state which indicates current processing middleware stack.
	 */
	struct xfwk_middleware_stack_state {
		xfwk_middleware::next_type next, this_next;
		const xfwk_middleware_stack* mstack;
		size_t offset;
	};

	/**
	 * class xfwk_middleware_tag.
	 * tag for identifying middleware.
	 */
	class NHTTP_API xfwk_middleware_tag {
	public:
		std::stack<xfwk_middleware_state> states;
	};

	/**
	 * class xfwk_middleware_stack_tag.
	 * tag for identifying middleware stack.
	 */
	class NHTTP_API xfwk_middleware_stack_tag {
	public:
		std::stack<xfwk_middleware_stack_state> states;
	};

	http_response_ptr xfwk_middleware::handle_for(http_request_ptr request, const std::shared_ptr<xfwk_target>& target) const {
		auto mid_tag = request->ensured_tag<xfwk_middleware_tag>();
		xfwk_middleware_state state;

		state.middleware = this;
		state.target = target;
		state.data.ptr = nullptr;

		/* push current middleware state. */
		mid_tag->states.push(std::move(state));

		auto response = handle(request, [](http_request_ptr req) {
			const auto& state = req->ensured_tag<xfwk_middleware_tag>()->states.top();
			return state.target->handle(req);
		});

		/* pop current middleware state. */
		mid_tag->states.pop();
		return response;
	}

	http_response_ptr xfwk_middleware_stack::handle(http_request_ptr request, next_type next) const {
		auto mid_tag = request->ensured_tag<xfwk_middleware_stack_tag>();
		xfwk_middleware_stack_state state;

		state.next = next;
		state.mstack = this;
		state.offset = 0;

		state.this_next = [](http_request_ptr req) {
			auto& state = req->ensured_tag<xfwk_middleware_stack_tag>()->states.top();
			const auto& middlewares = state.mstack->vec;

			if (state.offset < middlewares.size()) {
				auto current = middlewares[state.offset++];
				return current->handle(req, state.this_next);
			}

			return state.next(req);
		};

		/* push current middleware stack. */
		mid_tag->states.push(state);

		auto response = state.this_next(request);
		mid_tag->states.pop();

		return response;
	}
}
}
}