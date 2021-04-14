#pragma once
#include "../../types.hpp"
#include "../../net/socket.hpp"
#include "../../net/socket_watcher.hpp"
#include "../../net/base/session_base.hpp"
#include "../http_params.hpp"
#include "../http_link.hpp"

namespace nhttp {
	class stream;

namespace server {
	class http_raw_listener;
	class http_chunked_buffer;

	/**
	 * class http_raw_link.
	 * http link which handles connection itself.
	 */
	class NHTTP_API http_raw_link : public base::session_base {
		friend class http_link_driver;

	private:
		http_params params;
		http_raw_listener* listener;
		future<void> future_holder;

		std::shared_ptr<http_link> link;
		std::shared_ptr<http_link_driver> driver;
		std::shared_ptr<http_link_driver> replace_to;
		std::shared_ptr<http_chunked_buffer> buffer;

	public:
		http_raw_link(http_raw_listener* listener, std::shared_ptr<http_chunked_buffer> buffer);
		~http_raw_link() { }

	protected:
		/* initiate link. */
		virtual void on_initiate(const socket_t& socket, const std::shared_ptr<asyncs::context>& asyncs) override;

		/* finalize link. */
		virtual void on_finalize() override;

		/* handle socket events. */
		virtual bool on_event() override;

	protected:
		/* set driver replacement. */
		inline void on_replace(std::shared_ptr<http_link_driver> driver) {
			if (this->driver == driver)
				return;

			replace_to = driver;
		}
	};

}
}