#pragma once
#include "../http_context.hpp"
#include "../../utils/this_ptr.hpp"
#include "xfwk_facade.hpp"

namespace nhttp {
namespace server {
namespace xfwk {

	class xfwk_target;
	using xfwk_target_ptr = std::shared_ptr<xfwk_target>;

	class xfwk_middleware_stack;

	/**
	 * class xfwk_target.
	 * a resource target for single.
	 */
	class NHTTP_API xfwk_target : public xfwk_facade_middlewares {
	private:
		std::shared_ptr<xfwk_middleware_stack> middlewares;
		
	public:
		virtual ~xfwk_target() { }

	public:
		/**
		 * -- xfwk_facade_middlewares interface --
		 * get or set middleware stack. @warn: DO NOT set under routing process!
		 */
		virtual std::shared_ptr<xfwk_middleware_stack> get_middlewares() const override { return middlewares; }
		virtual void set_middlewares(const std::shared_ptr<xfwk_middleware_stack>& middlewares) override { this->middlewares = middlewares; }

	public:
		/* determines this target is unified or not. */
		virtual bool is_unifed() const { return false; }

		/* handle request and generate response. */
		virtual http_response_ptr handle(http_request_ptr request) const = 0;
	};

	/**
	 * class xfwk_lambda_target.
	 * a resource target by lambda expression.
	 */
	template<typename lambda_type>
	class NHTTP_API xfwk_lambda_target : public xfwk_target {
	private:
		lambda_type lambda;

	public:
		xfwk_lambda_target(lambda_type&& lambda)
			: lambda(std::forward<lambda_type>(lambda)) { }

	public:
		virtual http_response_ptr handle(http_request_ptr request) const override {
			return lambda(request);
		}
	};

	/* create a target with lambda expression. */
	template<typename lambda_type>
	inline auto target_by(lambda_type&& lambda) {
		return std::make_shared<xfwk_lambda_target<lambda_type>>(std::move(lambda));
	}

	/**
	 * class xfwk_method_target.
	 * a resource target by class method.
	 */
	template<typename class_type>
	class NHTTP_API xfwk_method_target : public xfwk_target {
	public:
		typedef http_response_ptr(class_type::* method)(http_request_ptr);
		typedef http_response_ptr(class_type::* const_method)(http_request_ptr) const;

	private:
		std::shared_ptr<class_type> instance;
		int32_t type;

		union {
			method _method;
			const_method _const_method;
		} target;

	public:
		xfwk_method_target(std::shared_ptr<class_type> instance, method _method)
			: instance(instance), type(0) { target._method = _method; }
			
		xfwk_method_target(std::shared_ptr<class_type> instance, const_method _const_method)
			: instance(instance), type(1) { target._const_method = _const_method; }

	public:
		virtual http_response_ptr handle(http_request_ptr request) const override {
			if (type) {
				const class_type* obj = instance.get();
				const_method mptr = target._const_method;

				return (obj->*mptr)(request);
			}

			else {
				class_type* obj = instance.get();
				method mptr = target._method;

				return (obj->*mptr)(request);
			}
		}
	};

	/* create a target with an object and its method. */
	template<typename class_type>
	inline auto target_by(std::shared_ptr<class_type> instance, http_response_ptr(class_type::* method)(http_request_ptr)) {
		return std::make_shared<xfwk_method_target<class_type>>(instance, method);
	}

	/* create a target with an object and its method. */
	template<typename class_type>
	inline auto target_by(std::shared_ptr<class_type> instance, http_response_ptr(class_type::* method)(http_request_ptr) const) {
		return std::make_shared<xfwk_method_target<class_type>>(instance, method);
	}

	/**
	 * class xfwk_unified_target.
	 * a collection of resource targets that implement individual methods.
	 */
	class NHTTP_API xfwk_unified_target : public xfwk_target {
	private:
		std::map<http_method, xfwk_target_ptr> targets;

	public:
		virtual ~xfwk_unified_target() { }

	public:
		/* determines this target is unified or not. */
		virtual bool is_unifed() const { return true; }

		/* determines a target implemented for method or not. */
		inline bool has_target_for(const http_method& method) const {
			return targets.find(method) != targets.end();
		}

		/* get target for method. */
		inline xfwk_target_ptr get_target_for(const http_method& method) const {
			auto i = targets.find(method);

			if (i != targets.end())
				return i->second;

			return nullptr;
		}

		/* set target for method. */
		inline this_ptr<xfwk_unified_target> set_target_for(const http_method& method, xfwk_target_ptr target) {
			if (target)
				targets.emplace(method, target);

			else unset_target_for(method);
			return this;
		}

		/* unset target for method. */
		inline this_ptr<xfwk_unified_target> unset_target_for(const http_method& method) {
			auto i = targets.find(method);

			if (i != targets.end())
				targets.erase(method);

			return this;
		}

	public:
		/* call individual method for handling the request. */
		virtual http_response_ptr handle(http_request_ptr request) const {
			auto i = targets.find(request->get_target().get_method());

			if (i != targets.end() && i->second) {
				return i->second->handle(request);
			}

			if (targets.size()) /* if other methods implemented, */
				return make_response(405);

			/* otherwise, returns no response. */
			return nullptr;
		}
	};

}
}
}