#pragma once
#include "../types.hpp"
#include "../net/socket.hpp"
#include "../asyncs/context.hpp"
#include "http_taggable.hpp"

namespace nhttp {
namespace server {

	class http_raw_link;
	class http_link_driver;
	class http_chunked_buffer;

	/**
	 * class http_link.
	 * logical http link (shared by http_raw_link)
	 */
	class NHTTP_API http_link : public http_taggable {
		friend class http_raw_link;

	protected:
		std::atomic<bool> _is_alive;

		hal::spinlock_t spinlock;
		std::shared_ptr<http_link_driver> driver;

	public:
		inline bool is_alive() const { return _is_alive; }

		/**
		 * replace link driver once.
		 * @note link driver can be configured only once.
		 * @warn DON'T call this after closing context.
		 */
		inline void replace(std::shared_ptr<http_link_driver> driver) {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			this->driver = driver;
		}
	};

	/**
	 * class http_link_hook.
	 * driver http link's event and processing routines.
	 */
	class NHTTP_API http_link_driver {
		friend class http_raw_link;

	protected:
		socket_t socket;
		std::shared_ptr<asyncs::context> asyncs;
		std::shared_ptr<http_chunked_buffer> buffer;

		std::shared_ptr<http_link> link;

	protected:
		virtual void on_initiate(const socket_t& socket,
			const std::shared_ptr<asyncs::context>& asyncs,
			const std::shared_ptr<http_chunked_buffer>& buffer,
			const std::shared_ptr<http_link>& link)
		{
			this->socket = socket;
			this->asyncs = asyncs;
			this->buffer = buffer;
			this->link = link;
		}

		virtual void on_finalize() {
			this->socket = socket_t();
			this->asyncs = nullptr;
			this->buffer = nullptr;
			this->link = nullptr;
		}

	protected:
		virtual bool on_event() { return false; }
	};

}
}