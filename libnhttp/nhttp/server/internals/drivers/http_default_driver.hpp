#pragma once
#include "../../http_link.hpp"
#include "../../http_params.hpp"
#include "../http_chunked_buffer.hpp"

namespace nhttp {
	class stream;

namespace server {
	class http_raw_listener;
	class http_raw_context;
	class http_raw_request_content;
	class http_raw_content_handler;
	
namespace drivers {

	/**
	 * control state of http_raw_link.
	 */
	enum nhttp_control_state {
		NSESS_PREPARING = 0,
		NSESS_RECEIVE_REQUEST,
		NSESS_WAITING_CONTEXT,
		NSESS_SEND_RESPONSE,
		NSESS_RESETTING,
		NSESS_MAX
	};

	/**
	 * event control of http_raw_link.
	 */
	enum nhttp_event_control {
		EVENT_FAILURE = 0,
		EVENT_AGAIN,
		EVENT_RETRY,
		EVENT_SUCCESS
	};

	class NHTTP_API http_default_driver : public http_link_driver {
	private:
		http_params params;
		http_raw_listener* listener;
		http_raw_link* raw_link;

		/* future holder and line buffer. */
		time_t timestamp;
		future<void> future_holder;
		std::vector<char> line_buf;

		std::atomic<int8_t> state;
		std::atomic<int8_t> context_state;
		std::shared_ptr<http_raw_context> current;
		std::shared_ptr<stream> content;

		/* content handler. */
		http_raw_content_handler* content_handler;

	private:
		struct {
			int8_t has_done : 1;
			int8_t has_target : 1;
			int8_t has_error : 1; /* 0: no error, 1: unrecoverable error. */
			int8_t read_more : 1;

			ssize_t found_lf;
		} receives;

		struct {
			int8_t keep_alive : 1;
			int8_t has_raised : 1;
		} contexts;

		struct {
			int8_t out_type : 1; /* 0: identity, 1: chunked. */
			int8_t out_state : 1; /* 0: none, 1: terminating. */
			int8_t buffer_state : 1; /* 0: generating, 1: sending */

			char hex_buf[18];

			int16_t buffer_bnk;
			int16_t buffer_pad; /* always 2 if chunked. */
			int64_t buffer_off;
			int64_t buffer_len;
		} sends;

	private:
		/* reset state structures. */
		inline void reset_states() {
			memset(&receives, 0, sizeof(receives));
			memset(&contexts, 0, sizeof(contexts));
			memset(&sends, 0, sizeof(sends));

			/* find LF if buffer isn't empty. */
			receives.found_lf = buffer->find('\n');
			receives.read_more = receives.found_lf < 0;
			contexts.keep_alive = 1;
		}

	public:
		http_default_driver(http_raw_listener* listener, http_raw_link* raw_link);

	protected:
		virtual void on_initiate(const socket_t& socket,
			const std::shared_ptr<asyncs::context>& asyncs,
			const std::shared_ptr<http_chunked_buffer>& buffer,
			const std::shared_ptr<http_link>& link);

		virtual void on_finalize();

	protected:
		virtual bool on_event();

	private:
		int32_t on_receive();
		int32_t on_handle();
		int32_t on_send();
	};

}
}
}