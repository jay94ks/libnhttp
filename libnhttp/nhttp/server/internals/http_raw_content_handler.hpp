#pragma once
#include "../../types.hpp"
#include "../../net/socket.hpp"

namespace nhttp {
namespace server {

	class http_chunked_buffer;
	class http_raw_request_content;

	enum {
		CONT_NONE = 0,
		CONT_FIXED,
		CONT_CHUNKED,
		CONT_WEBSOCKET
	};

	enum {
		CONP_HEADER = 0,
		CONP_BODY,
		CONP_BODY_END
	};

	/**
	 * class http_raw_content_handler.
	 * handles raw content events.
	 */
	class NHTTP_API http_raw_content_handler {
	public:
		bool skip_all;

		std::shared_ptr<http_chunked_buffer> buffer;
		std::shared_ptr<http_raw_request_content> feed;

	public:
		/* initiate content handler. */
		virtual void on_initiate() = 0;

		/* finalize and cleanup content handler. */
		virtual void on_finalize() = 0;

		/* handle socket events for feeding content. */
		virtual int32_t on_event(socket_t& socket) = 0;
	};
}
}