#include "http_raw_chunked_content_handler.hpp"
#include "../../../utils/strings.hpp"

#include "../http_raw_link.hpp"
#include "../http_chunked_buffer.hpp"
#include "../http_raw_request_content.hpp"

namespace nhttp {
namespace server {

	void http_raw_chunked_content_handler::on_initiate() {
		state.found_lf = -1;
	}

	void http_raw_chunked_content_handler::on_finalize() {
	}

	int32_t http_raw_chunked_content_handler::on_event(socket_t& socket) {
		size_t avail = buffer->get_left_capacity();
		size_t chunk = buffer->get_chunk_size();

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

			if (state.cont_phase == CONP_HEADER ||
				state.cont_phase == CONP_BODY_END)
			{
				if (state.found_lf < 0) {
					/* find LF from live buffer. */
					if (void* t = memchr(live_buf, '\n', read)) {
						size_t offset = size_t((uint8_t*)t - live_buf);
						state.found_lf = ssize_t(buffer->get_size() + offset);
					}
				}
			}

			/* skip bytes instead of pushing live_buf into buffer. */
			if (skip_all && state.cont_left) {
				size_t wastes = size_t(read) > state.cont_left ?
					size_t(state.cont_left) : read;

				state.cont_read += wastes;
				if (!(state.cont_left -= wastes))
					state.read_more = 0;

				/* if reading content section and in skipping mode, waste all content bytes. */
				if (read - wastes > 0) {
					buffer->write(live_buf + wastes, read - wastes);
					avail -= read - wastes;
				}
			}

			else {
				/* push live buf to buffer. */
				buffer->write(live_buf, read);
				avail -= read;
			}
		}

		size_t buffered_bytes;

		/**
		 * expects:
		 *	HEX_HEX...\r\n (or \n only).
		 *  CONTENT\r\n
		 *		... (REPEAT) ...
		 *  0\r\n
		 *  \r\n
		 */
		while ((buffered_bytes = buffer->get_size()) > 0) {
			size_t received_bytes = 0;

			/* set marker for detecting incoming bytes. */
			if (int64_t(buffered_bytes) < state.cont_mark) {
				state.cont_mark = buffered_bytes;
				continue;
			}

			if ((received_bytes = buffered_bytes - state.cont_mark) <= 0)
				break;

			if (state.cont_phase == CONP_HEADER) {
				if (state.found_lf < 0) {
					state.found_lf = buffer->find('\n');

					if (state.found_lf < 0) {
						state.read_more = 1;
						break;
					}
				}

				/* if LF found, read a line then parse length of chunk. */
				if (line_buf.size() < size_t(state.found_lf + 1))
					line_buf.resize(state.found_lf + 1);

				buffer->read(&line_buf[0], state.found_lf + 1);
				received_bytes -= state.found_lf + 1;

				/* handle chunk body. */
				state.cont_phase = CONP_BODY;
				state.cont_left = to_int64(&line_buf[0], 16, state.found_lf + 1);
				state.cont_read = 0;

				/* marks receives.found_lf + 1 as read. */
				state.cont_mark -= state.found_lf + 1;
			}

			if (state.cont_phase == CONP_BODY) {
				if (state.cont_left && received_bytes) {
					bool is_last_notify = received_bytes >= size_t(state.cont_left);
					size_t cont_bytes = is_last_notify ? state.cont_left : received_bytes;

					state.cont_left -= cont_bytes;
					state.cont_read += cont_bytes;
					received_bytes -= cont_bytes;

					if (cont_bytes) {
						if (feed) {
							feed->notify(cont_bytes, is_last_notify);
						}

						else buffer->skip(cont_bytes);
					}

					/* marks cont_bytes as read. */
					state.cont_mark += cont_bytes;
				}

				if (state.cont_left <= 0) {
					state.cont_phase = CONP_BODY_END;
					state.found_lf = buffer->find('\n');
				}

				else break;
			}

			if (state.cont_phase == CONP_BODY_END) {
				if (state.found_lf < 0) {
					state.found_lf = buffer->find('\n');

					if (state.found_lf < 0) {
						state.read_more = 1;
						break;
					}
				}

				if (line_buf.size() < size_t(state.found_lf + 1))
					line_buf.resize(state.found_lf + 1);

				buffer->read(&line_buf[0], state.found_lf + 1);
				received_bytes -= state.found_lf + 1;

				/* marks receives.found_lf + 1 as read. */
				state.cont_mark -= state.found_lf + 1;

				if (state.cont_read <= 0) {
					return EVENT_SUCCESS;
				}

				state.cont_phase = CONP_HEADER;
				state.found_lf = buffer->find('\n');
			}
		}

		return EVENT_AGAIN;
	}

}
}