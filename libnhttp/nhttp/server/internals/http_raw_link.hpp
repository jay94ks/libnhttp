#pragma once
#include "../../types.hpp"
#include "../../net/socket.hpp"
#include "../../net/socket_watcher.hpp"
#include "../../net/base/session_base.hpp"
#include "../http_params.hpp"
#include "../http_link.hpp"
#include "http_chunked_buffer.hpp"

namespace nhttp {
	class stream;

namespace server {
	class http_raw_listener;
	class http_raw_context;
	class http_raw_request_content;
	
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
	 * class http_raw_link.
	 * http link which handles connection itself.
	 */
	class NHTTP_API http_raw_link : public base::session_base {
	private:
		http_params params;
		http_raw_listener* listener;
		std::atomic<int8_t> state;
		std::atomic<int8_t> context_state;
		std::shared_ptr<http_raw_context> current;
		std::shared_ptr<http_chunked_buffer> buffer;
		std::shared_ptr<http_raw_request_content> current_feed;
		std::shared_ptr<http_link> link;
		std::shared_ptr<stream> content;

		time_t timestamp;
		future<void> future_holder;
		std::vector<char> line_buf;

	private:
		enum {
			CONT_NONE = 0,
			CONT_FIXED,
			CONT_CHUNKED
		};

		enum {
			CONP_HEADER = 0,
			CONP_BODY,
			CONP_BODY_END
		};

		struct {
			int8_t has_done    : 1;
			int8_t has_target  : 1;
			int8_t has_error   : 1; /* 0: no error, 1: unrecoverable error. */
			int8_t read_more   : 1;

			ssize_t found_lf;
		} receives;

		struct {
			int8_t has_raised  : 1;
			int8_t cont_skip   : 1;
			int8_t cont_type   : 2; /* 0: none, 1: fixed_len, 2: chunked. */
			int8_t cont_phase  : 2; /* 0: chunk-header, 1: chunk-body, 2: chunk-body-end */

			int64_t cont_left;
			int64_t cont_read;
			int64_t cont_mark;
		} contexts;

		struct {
			int8_t out_type     : 1; /* 0: identity, 1: chunked. */
			int8_t out_state    : 1; /* 0: none, 1: terminating. */
			int8_t buffer_state : 1; /* 0: generating, 1: sending */
			
			char hex_buf[18];

			int16_t buffer_bnk;
			int16_t buffer_pad; /* always 2 if chunked. */
			int64_t buffer_off;
			int64_t buffer_len;
		} sends;

	public:
		http_raw_link(http_raw_listener* listener, std::shared_ptr<http_chunked_buffer> buffer);
		~http_raw_link() { }

	protected:
		/* initiate link. */
		virtual void on_initiate(const socket_t& socket, const std::shared_ptr<asyncs::context>& asyncs) override;

		/* finalize link. */
		virtual void on_finalize() override;

		/* handle socket events. */
		virtual bool on_event() override;

	private:
		enum {
			EVENT_FAILURE = 0,
			EVENT_AGAIN,
			EVENT_RETRY,
			EVENT_SUCCESS
		};

		/* reset state structures. */
		inline void reset_states() {
			memset(&receives, 0, sizeof(receives));
			memset(&contexts, 0, sizeof(contexts));
			memset(&sends, 0, sizeof(sends));

			/* find LF if buffer isn't empty. */
			receives.found_lf = buffer->find('\n');
			receives.read_more = receives.found_lf < 0;
		}

	private:
		int32_t on_receive();
		int32_t on_handle();
		int32_t on_send();
	};

}
}