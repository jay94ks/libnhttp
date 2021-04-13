#pragma once
#include "xfwk_target.hpp"

namespace nhttp {
namespace server {
namespace xfwk {

	class NHTTP_API xfwk_websocket {

	};

	/**
	 * class xfwk_websocket_target.
	 * a resource target for websocket.
	 */
	class NHTTP_API xfwk_websocket_target : public xfwk_target {
	private:
		virtual ~xfwk_websocket_target() { }

	public:
		virtual http_response_ptr handle(http_request_ptr request) const;

	protected:
		virtual void on_newbie() { }
	};
}
}
}