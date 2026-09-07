#pragma once

#include <string>
#include <string_view>

namespace nhttp::ws {

	/* computes the Sec-WebSocket-Accept value for a client's Sec-WebSocket-Key,
	 * per RFC 6455 §1.3: base64(sha1(key + the RFC's fixed magic GUID)). */
	std::string compute_accept_key(std::string_view client_key);

}
