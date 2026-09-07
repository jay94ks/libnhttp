#pragma once

#include "target.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace nhttp::router {

	using next_fn = std::function<async::task<response>(request&)>;

	/**
	 * class middleware.
	 * chain-of-responsibility filter/pre-handler run before a target — decides
	 * whether/how to call `next` (which either invokes the next middleware in
	 * the stack, or the target itself once the stack is exhausted).
	 */
	class middleware {
	public:
		virtual ~middleware() = default;
		virtual async::task<response> handle(request& req, next_fn next) const = 0;
	};

	using middleware_ptr = std::shared_ptr<middleware>;

	/**
	 * class middleware_stack.
	 * composes a sequence of middleware around a terminal target.
	 */
	class middleware_stack {
	public:
		void prepend(middleware_ptr m);
		void append(middleware_ptr m);

		async::task<response> handle(request& req, target_ptr final_target) const;

	private:
		async::task<response> invoke(std::size_t index, request& req, target_ptr final_target) const;

		std::vector<middleware_ptr> stack_;
	};

}
