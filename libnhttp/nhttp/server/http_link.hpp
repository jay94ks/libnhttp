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
		friend class http_link_driver;
		friend class http_raw_link;

	protected:
		std::atomic<bool> _is_alive;
		hal::spinlock_t spinlock;
		std::shared_ptr<http_link_driver> driver;

	public:
		inline bool is_alive() const { return _is_alive; }

		/**
		 * replace link driver once.
		 * @warn DON'T call this after closing context.
		 */
		inline void replace(std::shared_ptr<http_link_driver> driver) {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			this->driver = driver;
		}
	};

	/**
	 * class http_link_hook.
	 * handles http link's event and processing routines.
	 */
	class NHTTP_API http_link_driver {
		friend class http_raw_link;

	protected:
		socket_t socket;
		http_raw_link* raw_link;
		std::shared_ptr<asyncs::context> asyncs;
		std::shared_ptr<http_chunked_buffer> buffer;
		std::shared_ptr<http_link> link;

	public:
		virtual ~http_link_driver() { }

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
		/* event handler. */
		virtual bool on_event() { return false; }

		/**
		 * replace driver if required.
		 * (this method shouldn't be called if replacement disallowed)
		 * @returns
		 *	 > 0: succeed.
		 *	 = 0: not set.
		 *   < 0: lock failure. - if this returned, should disconnect connection immediately.
		 */
		inline int32_t replace_driver(uint32_t spins = 3) {
			while(spins--) {
				if (link->spinlock.try_lock()) {
					if (auto new_driver = link->driver) {
						link->driver = nullptr;
						link->spinlock.unlock();

						replace_driver(new_driver);
						return 1;
					}

					link->spinlock.unlock();
					return 0;
				}
			}

			return -1;
		}

	private:
		/* this method should be called at event loop. */
		void replace_driver(std::shared_ptr<http_link_driver> new_driver);
	};

}
}