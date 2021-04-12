#pragma once
#include "http_extension.hpp"

namespace nhttp {
namespace server {
	
	/**
	 * class http_websock_ep.
	 * websocket endpoint extension.
	 */
	class NHTTP_API http_websocket_ep : public http_extension {
	public:
		http_websocket_ep() { }
		virtual ~http_websocket_ep() { }

	private:
		virtual bool on_collect(std::shared_ptr<http_context> context) override;
		virtual bool on_handle(std::shared_ptr<http_context> context) override;

	protected:
		virtual bool on_connect(std::shared_ptr<http_context> context);
		virtual void on_message(std::shared_ptr<http_context> context);
		virtual void on_disconnect(std::shared_ptr<http_context> context);
	};
}
}