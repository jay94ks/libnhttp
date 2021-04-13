#include "http_raw_websocket_content_handler.hpp"

#include "../http_raw_link.hpp"
#include "../http_chunked_buffer.hpp"
#include "../http_raw_request_content.hpp"

namespace nhttp {
namespace server {

	void http_raw_websocket_content_handler::on_initiate() {
		frame_hdr_sz = 0;
		memset(&frame_hdr, 0, sizeof(frame_hdr));
		state.read_more = 0;

		state.cont_mark = buffer->get_size();
		state.cont_phase = CONP_HEADER;
	}

	void http_raw_websocket_content_handler::on_finalize() {
	}

	int32_t http_raw_websocket_content_handler::on_event(socket_t& socket) {
		size_t avail = buffer->get_left_capacity();
		size_t chunk = buffer->get_chunk_size();

		if (!feed->wanna_read())
			return EVENT_AGAIN;

		if (!buffer->get_size())
			state.read_more = 1;

		if (state.read_more && avail <= (chunk >> 2)) {
			size_t size = buffer->get_size();

			/**
			 * if buffered length is longer than 2 * chunk,
			 * wait until buffered bytes being read.
			 */
			if (size >= (chunk << 1))
				return EVENT_AGAIN;

			/* try preallocate more chunks. */
			if (!buffer->preallocate() && !avail)
				return EVENT_AGAIN;
		}

		while (avail && state.read_more) {
			uint8_t live_buf[2048];
			size_t slice = avail > sizeof(live_buf) ? sizeof(live_buf) : avail;
			ssize_t read = socket.read(live_buf, slice);

			if (read <= 0) {
				int32_t err = socket.get_errno();

				if (err == EINTR)
					continue;

				if (err == EAGAIN || err == EWOULDBLOCK) {
					state.read_more = 0;
					break;
				}

				return EVENT_FAILURE;
			}

			/**
			 * push live buf to buffer. 
			 * web socket don't need skip mechanism.
			 */
			size_t len = buffer->write(live_buf, read);

			avail -= len;
			state.cont_mark += len;
		}

		if (state.cont_phase == CONP_HEADER) {
			size_t sz = buffer->peek(&frame_hdr, sizeof(frame_hdr));

			if (frame_hdr_sz <= 0 && sz >= 2) {
				/**
				 * All messages from client should set mask bit.
				 * Otherwise, disconnect it immediately.
				 */
				if (!frame_hdr.hdr.msk) {
					return EVENT_FAILURE;
				}

				if (frame_hdr.hdr.len == 0x7f) { 
					state.frame_mode = 3;
					frame_hdr_sz = 2 + 8 + 4; /* hdr + plen + mkey. */
#if NHTTP_LITTLE_ENDIAN
					for (int32_t i = 0; i < 4; ++i) {
						uint8_t tmp = frame_hdr.packed_bytes[2 + i];
						frame_hdr.packed_bytes[2 + i] = frame_hdr.packed_bytes[2 + (7 - i)];
						frame_hdr.packed_bytes[2 + (7 - i)] = tmp;
					}
#endif
				}

				else if (frame_hdr.hdr.len == 0x7e) {
					state.frame_mode = 2;
					frame_hdr_sz = 2 + 2 + 4; /* hdr + plen + mkey. */

#if NHTTP_LITTLE_ENDIAN
					uint8_t tmp = frame_hdr.packed_bytes[2];
					frame_hdr.packed_bytes[2] = frame_hdr.packed_bytes[3];
					frame_hdr.packed_bytes[3] = tmp;
#endif
				}

				else {
					state.frame_mode = 1;
					frame_hdr_sz = 2 + 4; /* hdr + plen + mkey. */
				}
			}

			if (frame_hdr_sz && sz >= frame_hdr_sz) {
				if (frame_hdr.hdr.opc > 2) {
					/* control frames. */

					return EVENT_RETRY;
				}

				state.cont_phase = CONP_BODY;
				state.cont_mark -= buffer->skip(frame_hdr_sz);
				memcpy(&frame_cur, &frame_hdr, sizeof(frame_cur));

				switch (state.frame_mode) {
					case 1:
						state.cont_plen = frame_hdr.hdr.len;
						state.mask_key = frame_hdr._n_1.mkey;
						break;

					case 2:
						state.cont_plen = frame_hdr._n_2.plen;
						state.mask_key = frame_hdr._n_2.mkey;
						break;

					case 3:
						state.cont_plen = frame_hdr._n_8.plen;
						state.mask_key = frame_hdr._n_8.mkey;
						break;
				}
			}
		}

		switch (state.frame_mode) {
			case 1: return on_event_n1(socket);
			case 2: return on_event_n2(socket);
			case 3: return on_event_n8(socket);
			default: break;
		}

		return EVENT_AGAIN;
	}

	int32_t http_raw_websocket_content_handler::on_event_n1(socket_t& socket) {
		
		return EVENT_AGAIN;
	}

	int32_t http_raw_websocket_content_handler::on_event_n2(socket_t& socket) {
		return EVENT_AGAIN;
	}

	int32_t http_raw_websocket_content_handler::on_event_n8(socket_t& socket) {
		return EVENT_AGAIN;
	}

}
}