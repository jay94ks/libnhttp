#pragma once

#include "../extension.hpp"
#include "../../ws/connection.hpp"

#include <functional>
#include <memory>
#include <string>

namespace nhttp::server {

	/**
	 * class websocket_endpoint.
	 * the WebSocket handshake extension: validates the Upgrade request, computes
	 * Sec-WebSocket-Accept, and on success hands the connection off to a real
	 * ws::ws_connection (frame I/O, not just the handshake — see CONCEPTS.md §6).
	 * `on_connect` owns the connection's whole lifetime: it's expected to loop
	 * calling `co_await ws->receive()` until it returns nullopt.
	 */
	class websocket_endpoint final : public extension {
	public:
		using connect_handler = std::function<async::task<void>(std::shared_ptr<ws::ws_connection>)>;

		websocket_endpoint(std::string path, connect_handler on_connect, std::uint32_t prio = 0x80000000u);

	public:
		std::uint32_t priority() const noexcept override { return priority_; }
		async::task<bool> wants(request& req) override;
		async::task<response> handle(request& req) override;

	private:
		std::string path_;
		connect_handler on_connect_;
		std::uint32_t priority_;
	};

	inline std::shared_ptr<websocket_endpoint> websocket_endpoint_for(std::string path, websocket_endpoint::connect_handler on_connect) {
		return std::make_shared<websocket_endpoint>(std::move(path), std::move(on_connect));
	}

}
