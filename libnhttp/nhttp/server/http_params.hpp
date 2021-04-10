#pragma once
#include "../types.hpp"

namespace nhttp {
namespace server {
	/**
	 * struct http_params.
	 * basic parameters for http_raw_listener.
	 */
	struct http_params {
		/* worker count*/
		int32_t worker_count = 2;

		/* request timeout in second. */
		int32_t timeout = 5;

		/* protocol buffer size in kbytes. */
		size_t buffer_size_in_kb = 8;

		/**
		 * maximum total protocol buffers in count.
		 * @note: listener will pre-allocate buffers.
		 */
		size_t max_total_buffers = 2048;

		struct {
			/* enable expose `Server` header or not. */
			int8_t enable = 1;

			/* text for `Server:` header */
			std::string watermark = "libnhttp v1.0";
		} advertise;

		struct {
			/* linger in second, 0 for disable. */
			int32_t linger = 5;

			struct {
				/* TCP keep-alive in second. 0 for disable. */
				int32_t idle = 5;

				/* keep alive probe packet interval. */
				int32_t interval = 10;

				/* max count to probe. */
				int32_t max = 10;
			} keepalive;
		} tcp;
	};
}
}