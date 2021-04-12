#pragma once
#include "../../types.hpp"

namespace nhttp {
namespace server {

	class http_raw_request_content;

	/**
	 * class http_raw_content_handler.
	 * handles raw content events.
	 */
	class NHTTP_API http_raw_content_handler {
	public:
		std::shared_ptr<http_raw_request_content> feed;

	public:
		/* */
		virtual int32_t on_event() = 0;
	};

}
}