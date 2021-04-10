#pragma once
#include "../types.hpp"
#include "../net/socket.hpp"
#include "../net/socket_watcher.hpp"
#include "../net/base/listener_base.hpp"
#include "http_params.hpp"
#include <memory>

namespace nhttp {
namespace asyncs {
	class context;
}
namespace server {
	class http_chunked_alloc;
	class http_raw_context;
	class http_raw_link;

	/**
	 * class http_raw_listener.
	 * http listener for accepting http sessions.
	 */
	class http_raw_listener : public base::listener_base {
		friend class http_raw_link;

	private:
		http_params params;
		std::shared_ptr<http_chunked_alloc> chunk_alloc;

	public:
		http_raw_listener(const socket_watcher& watcher, const http_params& params);
		virtual ~http_raw_listener() { terminate(); }

	public:
		inline const http_params& get_params() const { return params; }

	protected:
		/* allocate a http link. */
		virtual base::session_base* on_enter() override;

		/* de-allocate a http link. */
		virtual void on_leave(base::session_base* link) override;

	protected:
		virtual void on_raw_context(const std::shared_ptr<http_raw_context>& context, bool has_error) = 0;
	};

}
}