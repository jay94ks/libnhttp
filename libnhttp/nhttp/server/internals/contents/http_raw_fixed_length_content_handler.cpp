#include "http_raw_fixed_length_content_handler.hpp"

#include "../http_raw_link.hpp"
#include "../http_chunked_buffer.hpp"
#include "../http_raw_request_content.hpp"

namespace nhttp {
namespace server {

	void http_raw_fixed_len_content_handler::on_initiate() {
		state.cont_mark = buffer->get_size();
	}

	void http_raw_fixed_len_content_handler::on_finalize() {
	}

	int32_t http_raw_fixed_len_content_handler::on_event(socket_t& socket) {
		size_t avail = buffer->get_left_capacity();
		size_t chunk = buffer->get_chunk_size();

		if (feed && !feed->wanna_read())
			return EVENT_AGAIN;

		if (!buffer->get_size())
			state.read_more = 1;

		if (state.read_more && avail <= (chunk >> 2)) {
			size_t size = buffer->get_size();

			/**
			 * if buffered length is longer than 2 * chunk,
			 * wait until buffered bytes being read.
			 */
			if (size >= (chunk << 1)) {
				state.read_more = 0;
				return EVENT_AGAIN;
			}

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

			/* skip bytes instead of pushing live_buf into buffer. */
			if (skip_all && state.cont_left) {
				size_t wastes = read > state.cont_left ?
								size_t(state.cont_left) : read;

				state.cont_read += wastes;
				state.cont_mark -= wastes;

				if (!(state.cont_left -= wastes))
					state.read_more = 0;

				/* if reading content section and in skipping mode, waste all content bytes. */
				if (read - wastes > 0) {
					size_t len = buffer->write(live_buf + wastes, read - wastes);

					avail -= len;
					state.cont_mark += len;
				}
			}

			else {
				/* push live buf to buffer. */
				size_t len = buffer->write(live_buf, read);

				avail -= len;
				state.cont_mark += len;
			}
		}

		if (state.cont_left && state.cont_mark) {
			bool is_last_notify = state.cont_mark >= state.cont_left;
			size_t cont_bytes = is_last_notify ? size_t(state.cont_left) : state.cont_mark;

			state.cont_left -= cont_bytes;
			state.cont_read += cont_bytes;
			//received_bytes -= cont_bytes;

			if (cont_bytes) {
				if (feed) {
					feed->notify(cont_bytes, is_last_notify);
				}

				else buffer->skip(cont_bytes);
			}

			/* marks cont_bytes as read. */
			state.cont_mark -= cont_bytes;
		}

		if (state.cont_left <= 0) {
			return EVENT_SUCCESS;
		}

		return EVENT_AGAIN;
	}

}
}