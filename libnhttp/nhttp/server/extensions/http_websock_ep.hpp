#pragma once
#include "http_extension.hpp"
#include "../http_taggable.hpp"
#include "../../hal/spinlock_t.hpp"
#include "../../utils/lambda_t.hpp"

namespace nhttp {
	class stream;

namespace server {
namespace drivers {
	class http_websocket_driver;
}

	class http_websocket;
	using http_websocket_ptr = std::shared_ptr<http_websocket>;

	struct http_websocket_params {
		/* maximum payload per message. */
		uint64_t max_payload_bytes = 32 * 1024;

		struct {
			/* allowed idle before trying to ping. */
			int32_t idle = 5;

			/* interval to send ping. */
			int32_t interval = 10;

			/* maximum count to send ping. */
			int32_t max = 10;
		} ping;
	};
	
	/**
	 * class http_websock_ep.
	 * websocket endpoint extension.
	 */
	class NHTTP_API http_websocket_ep : public http_extension {
		friend class http_websocket_driver;

	public:
		http_websocket_ep() { }
		virtual ~http_websocket_ep() { }

	private:
		virtual bool on_collect(std::shared_ptr<http_context> context) override;
		virtual bool on_handle(std::shared_ptr<http_context> context) override;

	protected:
		virtual bool on_connect(http_websocket_ptr context) { return false; }
		virtual void on_message(http_websocket_ptr context) { }
		virtual void on_disconnect(http_websocket_ptr context) { }
	};

	/**
	 * class http_websocket.
	 * websocket from http_link and http_context.
	 */
	class NHTTP_API http_websocket : public http_taggable {
		friend class drivers::http_websocket_driver;

	public:
		typedef lambda_t<void(http_websocket&)> on_open_callback;
		typedef lambda_t<void(http_websocket&)> on_close_callback;
		typedef lambda_t<void(http_websocket&)> on_message_callback;

	private:
		std::shared_ptr<http_context> context;
		

	private:
		/* user side callbacks. */
		on_open_callback _on_open;
		on_close_callback _on_close;
		on_message_callback _on_message;

	public:
		http_websocket(const std::shared_ptr<http_context>& context)
			: context(context) { }

	};
}
}