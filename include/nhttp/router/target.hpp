#pragma once

#include "../server/request.hpp"
#include "../server/response.hpp"
#include "../async/task.hpp"

#include <memory>
#include <type_traits>
#include <utility>

namespace nhttp::router {

	using server::request;
	using server::response;

	/**
	 * class target.
	 * the real handler for a matched route — implement this yourself, or use
	 * target_by(...) to wrap a lambda or a member function.
	 */
	class target {
	public:
		virtual ~target() = default;
		virtual async::task<response> handle(request& req) const = 0;
	};

	using target_ptr = std::shared_ptr<target>;

	namespace detail {

		template<typename F>
		class lambda_target final : public target {
		public:
			explicit lambda_target(F fn) : fn_(std::move(fn)) { }

			async::task<response> handle(request& req) const override {
				if constexpr (std::is_same_v<std::invoke_result_t<const F&, request&>, async::task<response>>)
					co_return co_await fn_(req);
				else
					co_return fn_(req);
			}

		private:
			F fn_;
		};

		template<typename C, typename Ret>
		class method_target final : public target {
		public:
			using member_fn = Ret(C::*)(request&);

			method_target(std::shared_ptr<C> instance, member_fn fn) : instance_(std::move(instance)), fn_(fn) { }

			async::task<response> handle(request& req) const override {
				if constexpr (std::is_same_v<Ret, async::task<response>>)
					co_return co_await (instance_.get()->*fn_)(req);
				else
					co_return (instance_.get()->*fn_)(req);
			}

		private:
			std::shared_ptr<C> instance_;
			member_fn fn_;
		};

	}

	/* wraps a lambda `request& -> response` or `request& -> task<response>` as a target. */
	template<typename F>
	target_ptr target_by(F fn) {
		return std::make_shared<detail::lambda_target<F>>(std::move(fn));
	}

	/* wraps a bound member function (either signature above) as a target. */
	template<typename C, typename Ret>
	target_ptr target_by(std::shared_ptr<C> instance, Ret(C::* fn)(request&)) {
		return std::make_shared<detail::method_target<C, Ret>>(std::move(instance), fn);
	}

}
