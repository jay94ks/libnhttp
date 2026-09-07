#include "nhttp/server/extensions/websocket_endpoint.hpp"
#include "nhttp/ws/handshake.hpp"
#include "nhttp/protocol/http_method.hpp"

#include <algorithm>
#include <cctype>

namespace nhttp::server {

	namespace {

		bool contains_token_ci(const std::string& value, std::string_view token) noexcept {
			std::string lowered = value;
			std::transform(lowered.begin(), lowered.end(), lowered.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			return lowered.find(token) != std::string::npos;
		}

	}

	websocket_endpoint::websocket_endpoint(std::string path, connect_handler on_connect, std::uint32_t prio)
		: path_(std::move(path)), on_connect_(std::move(on_connect)), priority_(prio)
	{
	}

	async::task<bool> websocket_endpoint::wants(request& req) {
		if (req.method() != protocol::http_method::GET() || req.path() != path_)
			co_return false;

		const std::string* upgrade = req.headers.get(protocol::header_names::UPGRADE);
		const std::string* connection = req.headers.get(protocol::header_names::CONNECTION);

		if (!upgrade || !connection)
			co_return false;

		co_return contains_token_ci(*upgrade, "websocket") && contains_token_ci(*connection, "upgrade");
	}

	async::task<response> websocket_endpoint::handle(request& req) {
		const std::string* key = req.headers.get(protocol::header_names::SEC_WEBSOCKET_KEY);

		if (!key)
			co_return make_response(400);

		response r;
		r.status = protocol::http_status(101);
		r.headers.set(std::string(protocol::header_names::UPGRADE), "websocket");
		r.headers.set(std::string(protocol::header_names::CONNECTION), "Upgrade");
		r.headers.set(std::string(protocol::header_names::SEC_WEBSOCKET_ACCEPT), ws::compute_accept_key(*key));

		connect_handler on_connect = on_connect_;

		r.upgrade_handler = [on_connect](std::shared_ptr<io::stream> wire, std::string leftover) -> async::task<void> {
			auto conn = std::make_shared<ws::ws_connection>(std::move(wire), std::move(leftover));
			co_await on_connect(conn);
		};

		co_return r;
	}

}
