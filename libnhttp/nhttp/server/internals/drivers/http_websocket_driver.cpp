#include "http_websocket_driver.hpp"
#include "../../extensions/http_websock_ep.hpp"

namespace nhttp {
namespace server {
namespace drivers {

	http_websocket_driver::http_websocket_driver(const std::shared_ptr<http_websocket>& websocket)
		: websocket(websocket), frame_hdr_sz(0), wanna_close(0)
	{
	}

	void http_websocket_driver::on_initiate(const socket_t& socket,
		const std::shared_ptr<asyncs::context>& asyncs, 
		const std::shared_ptr<http_chunked_buffer>& buffer,
		const std::shared_ptr<http_link>& link)
	{
		http_link_driver::on_initiate(socket, asyncs, buffer, link);

	}

	void http_websocket_driver::on_finalize() {
		http_link_driver::on_finalize();
	}

	bool http_websocket_driver::on_event() {
		/* if not opened, skip. */
		if (!has_open) {
			
			return true;
		}

		/* if wanna_close == 1, disconnect connection. */
		if (wanna_close) {
			return false;
		}

		return false;
	}

}
}
}