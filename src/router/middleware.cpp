#include "nhttp/router/middleware.hpp"

namespace nhttp::router {

	void middleware_stack::prepend(middleware_ptr m) {
		stack_.insert(stack_.begin(), std::move(m));
	}

	void middleware_stack::append(middleware_ptr m) {
		stack_.push_back(std::move(m));
	}

	async::task<response> middleware_stack::handle(request& req, target_ptr final_target) const {
		return invoke(0, req, final_target);
	}

	async::task<response> middleware_stack::invoke(std::size_t index, request& req, target_ptr final_target) const {
		if (index >= stack_.size())
			co_return co_await final_target->handle(req);

		const middleware_ptr m = stack_[index];
		const std::size_t next_index = index + 1;

		co_return co_await m->handle(req, [this, next_index, final_target](request& r) {
			return invoke(next_index, r, final_target);
		});
	}

}
