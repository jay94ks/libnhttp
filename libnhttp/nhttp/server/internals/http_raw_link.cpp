#include "http_raw_link.hpp"
#include "../http_raw_listener.hpp"
#include "drivers/http_default_driver.hpp"

namespace nhttp {
namespace server {
	http_raw_link::http_raw_link(http_raw_listener* listener, std::shared_ptr<http_chunked_buffer> buffer)
		: listener(listener), buffer(buffer)
	{
		params = listener->get_params();
	}

	void http_raw_link::on_initiate(const socket_t& socket,
			const std::shared_ptr<asyncs::context>& asyncs)
	{
		session_base::on_initiate(socket, asyncs);
		hal::socket_raw_t sock = socket.get_raw();

		sock.set_non_blocking(true);
		sock.set_naggle_enabled(false);

		sock.set_linger(params.tcp.linger * 1000);
		sock.set_keepalive(
			params.tcp.keepalive.idle * 1000,
			params.tcp.keepalive.interval * 1000,
			params.tcp.keepalive.max);

		(link = std::make_shared<http_link>())
			->_is_alive.store(true);

		/* configure default protocol driver. */
		driver = std::make_shared<drivers::http_default_driver>(listener, this);

		/* initiate protocol driver.*/
		driver->on_initiate(socket, asyncs, buffer, link);
	}

	void http_raw_link::on_finalize() {
		if (future_holder && !future_holder.is_completed()) {
			future_holder.wait(-1);
		}
		
		driver->on_finalize();
		link->_is_alive.store(false);

		driver = nullptr;
		link = nullptr;

		session_base::on_finalize();
	}
	
	bool http_raw_link::on_event() {
		/* wait completion if future task didn't finished. */
		return driver->on_event();
	}

	void http_raw_link::switch_driver(std::shared_ptr<http_link_driver> driver) {
	}

}
}