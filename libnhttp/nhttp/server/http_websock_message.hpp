#pragma once
#include "../types.hpp"
#include "internals/http_raw_request_content.hpp"

namespace nhttp {
namespace server {

	class NHTTP_API http_websock_message : public http_raw_request_content {
	private:
		int8_t masking_enabled : 1;
		uint32_t masking_key;

	public:
		http_websock_message(std::shared_ptr<http_chunked_buffer> buffer, ssize_t total_bytes)
			: http_raw_request_content(buffer, total_bytes), masking_enabled(0), masking_key(0) { }

		http_websock_message(std::shared_ptr<http_chunked_buffer> buffer, ssize_t total_bytes, uint32_t masking_key)
			: http_raw_request_content(buffer, total_bytes), masking_enabled(1)
		{
			this->masking_key = (masking_key >> 24) | (masking_key << 8);
		}

	public:
		/* read websocket message from buffer. */
		virtual int32_t read(void* buf, size_t len) {
			if (masking_enabled) {
				std::lock_guard<decltype(spinlock)> guard(spinlock);
				return read_locked(buf, len);
			}

			/* if non-masked stream, just read only. */
			return http_raw_request_content::read(buf, len);
		}

	protected:
		/**
		 * read bytes from stream with already locked.
		 * @returns:
		 *	> 0: read size.
		 *  = 0: end of stream.
		 *  < 0: not supported or not available if non-block.
		 * @note:
		 *	errno == EWOULDBLOCK: for non-block mode.
		 */
		virtual int32_t read_locked(void* buf, size_t len) {
			if (masking_enabled) {
				int32_t bytes = http_raw_request_content::read_locked(buf, len);

				/* unmask bytes. */
				if (bytes > 0) {
					uint8_t* masked_b = (uint8_t*)buf;
					uint8_t* masked_e = masked_b + bytes;

					while (masked_b < masked_e) {
						uint8_t mask = uint8_t(masking_key & 0xff);

						/* rotate masking key for getting first 8 bits at next cycle. */
						masking_key = (masking_key >> 24) | (masking_key << 8);

						*masked_b = *masked_b ^ mask;
						++masked_b;
					}
				}

				return bytes;
			}

			/* if non-masked stream, just read only. */
			return http_raw_request_content::read_locked(buf, len);
		}
	};

}
}