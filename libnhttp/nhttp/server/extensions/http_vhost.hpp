#pragma once
#include "http_extension.hpp"
#include "../http_context.hpp"
#include "../http_link.hpp"
#include <stack>
#include <regex>

namespace nhttp {
namespace server {

	class http_vhost;
	using http_vhost_ptr = std::shared_ptr<http_vhost>;

	/**
	 * class http_vhost_tag.
	 * tag for marking the request is handled by.
	 */
	class NHTTP_API http_vhost_tag {
	public:
		std::stack<http_vhost*> vhosts;
	};

	/**
	 * class http_vhost.
	 * virtual host extension.
	 * @note host-related extensions should override this class.
	 */
	class NHTTP_API http_vhost : public http_extendable_extension {
	private:
		struct vhost_t { };
		static constexpr vhost_t by_vhost = { };

	private:
		struct predicate {
			void* callable;
			void(*dtor)(void*);
			bool(*call)(void*, const http_context_ptr&);
		};

	private:
		predicate _predicate = { 0, };

	private:
		http_vhost(vhost_t, bool is_static);

	public:
		http_vhost(std::string name);
		http_vhost(std::regex regex);

		template<typename lambda_type>
		http_vhost(lambda_type&& lambda) : http_vhost(by_vhost, false) {
			static_assert(
				std::is_same<decltype(
					std::declval<lambda_type>()(std::declval<http_context_ptr>())
				), bool>::value, "lambda should return boolean!"
			);

			_predicate.callable = new lambda_type(std::move(lambda));
			_predicate.dtor = [](void* p) { delete (lambda_type*)p; };
			_predicate.call = [](void* p, const std::shared_ptr<http_context>& c) {
				return (* (lambda_type*) p)(c);
			};
		}

		virtual ~http_vhost();

	private:
		virtual bool on_collect(std::shared_ptr<http_context> context) override;

	protected:
		/* called before calling the on_handle method, test if it can be handled. */
		virtual bool on_enter(std::shared_ptr<http_context> context);

		/* called after calling the on_handle method, clean states if needed. */
		virtual void on_leave(std::shared_ptr<http_context> context);

	protected:
		/* handle vhost request (fallback) */
		virtual bool on_handle(std::shared_ptr<http_context> context, extended_t) override { return false; }
	};

	/**
	 * create vhost for specific domain.
	 */
	inline http_vhost_ptr vhost_for(std::string name) {
		return std::make_shared<http_vhost>(std::move(name));
	}

	/**
	 * create vhost for regex matched domains.
	 */
	inline http_vhost_ptr vhost_for(std::regex regex) {
		return std::make_shared<http_vhost>(std::move(regex));
	}

	template<typename pred_type>
	inline http_vhost_ptr vhost_by_pred(pred_type&& pred) {
		return std::make_shared<http_vhost_ptr>(std::move(pred));
	}

	/**
	 * get virtual host of the context.
	 * @returns nullptr if not under vhost.
	 */
	inline http_vhost* vhost_of(std::shared_ptr<http_context> context) {
		auto* tag = context->link->get_tag_ptr<http_vhost_tag>();
		return tag && tag->vhosts.size() ? tag->vhosts.top() : nullptr;
	}
}
}