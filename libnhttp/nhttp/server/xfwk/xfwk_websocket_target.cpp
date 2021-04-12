#include "xfwk_websocket_target.hpp"
#include "../http_link.hpp"
#include "../../depends/sha1/sha1.hpp"

namespace nhttp {
namespace server {
namespace xfwk {
	
	/**
	 * class websocket_tag.
	 * internal use only.
	 */
	class websocket_tag {
	public:
		/* determines this link upgraded already or not. */
		int8_t is_handshaked : 1;
	};

	http_response_ptr xfwk_websocket_target::handle(http_request_ptr request) const {
		websocket_tag* tag	= request->get_link()->get_tag_ptr<websocket_tag>();
		const auto& target = request->get_target();

		if (tag && tag->is_handshaked) {
			/* notification about read/write for websocket? */
			return make_response(400);
		}

		/* if it isn't by GET method, 400 bad request. */
		if (target.get_method() != http_method::GET)
			return make_response(400);
		
		const auto& headers = request->get_headers();
		auto sec_key = headers.find_one(http_header::SEC_WEBSOCKET_KEY);

		/* if `Sec-Websocket-Key` header not set, generates 400 bad request.*/
		if (sec_key == headers.vec.end())
			return make_response(400);

		char base64[SHA1_BASE64_SIZE];
		static const char* MAGIC_STRING = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
		auto response = make_response(101); /* 101 Switching Protocol. */

		sha1().add((sec_key->get_value() + MAGIC_STRING).c_str())
			  .finalize().print_base64(base64);

		/* set `Sec-Websocket-Accept` header. */
		response->headers.set(http_header::SEC_WEBSOCKET_ACCEPT, base64);

		/* then return response. */
		tag->is_handshaked = 1;
		return response;
	}
}
}
}