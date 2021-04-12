#include "http_raw_link.hpp"
#include "http_raw_request_content.hpp"
#include "contents/http_raw_chunked_content_handler.hpp"
#include "contents/http_raw_fixed_length_content_handler.hpp"

#include "../../utils/path.hpp"
#include "../../io/stream.hpp"
#include "../http_raw_context.hpp"
#include "../http_raw_listener.hpp"

#include "../http_websock_message.hpp"

namespace nhttp {
namespace server {
	http_raw_link::http_raw_link(http_raw_listener* listener, std::shared_ptr<http_chunked_buffer> buffer)
		: listener(listener), buffer(buffer), content_handler(nullptr)
	{
		params = listener->get_params();
	}

	void http_raw_link::on_initiate(const socket_t& socket,
			const std::shared_ptr<asyncs::context>& asyncs)
	{
		session_base::on_initiate(socket, asyncs);
		hal::socket_raw_t sock = socket.get_raw();

		state = NSESS_PREPARING;
		timestamp = time(nullptr);

		sock.set_non_blocking(true);
		sock.set_naggle_enabled(false);

		sock.set_linger(params.tcp.linger * 1000);
		sock.set_keepalive(
			params.tcp.keepalive.idle * 1000,
			params.tcp.keepalive.interval * 1000,
			params.tcp.keepalive.max);

		(link = std::make_shared<http_link>())
			->_is_alive.store(true);
		
		/* reset state structures. */
		reset_states();
	}

	void http_raw_link::on_finalize() {
		if (content_handler) {
			content_handler->on_finalize();
			delete content_handler;

			content_handler = nullptr;
		}
		/*if (current_feed) {
			current_feed->disconnect();
		}*/

		if (current) {
			current->unconfigure();
		}

		if (future_holder && !future_holder.is_completed()) {
			future_holder.wait(-1);
		}

		link->_is_alive.store(false);
		line_buf.clear();

		current = nullptr;
		link = nullptr;

		session_base::on_finalize();
	}
	
	bool http_raw_link::on_event() {
		while(true) {
			int32_t ret = EVENT_AGAIN;

			switch (state) {
				case NSESS_PREPARING:
					current = std::make_shared<http_raw_context>();

					/* configure context. */
					current->configure(this, [](http_raw_context& context) {
						++context.raw_link->context_state;
						context.unconfigure();
					});

					current->link = link;
					context_state = 0;

					ret = EVENT_SUCCESS;
					break;

				case NSESS_RECEIVE_REQUEST:
					if (socket.can_read())
						ret = on_receive();

					break;

				case NSESS_WAITING_CONTEXT:
					ret = on_handle();
					break;

				case NSESS_SEND_RESPONSE:
					if (socket.can_write())
						ret = on_send();

					break;

				case NSESS_RESETTING:
					if (content_handler) {
						content_handler->on_finalize();
						delete content_handler;

						content_handler = nullptr;
					}
					/*if (current_feed) {
						current_feed->disconnect();
					}*/

					/* if 101 Switching Protocol and websocket, */
					if (current->response.status.get_code() == 101) {
						if (const auto* upgrade = current->response.headers.get(http_header::UPGRADE)) {
							if (!strnicmp(upgrade, "websocket", 8)) {
								contexts.keep_alive = 0;

								state = NSESS_WAITING_CONTEXT;
								ret = EVENT_RETRY;
								break;
							}
						}
					}

					if (current) {
						current->unconfigure();
					}

					current = nullptr;
					timestamp = time(nullptr);


					/* if line_buf is larger than half chunk, make it less than. */
					if (line_buf.size() > params.buffer_size_in_kb * 512)
						line_buf.resize(params.buffer_size_in_kb * 512);

					if (receives.has_error || !contexts.keep_alive) {
						ret = EVENT_FAILURE;
						break;
					}
					
					reset_states();
					ret = EVENT_SUCCESS;
					break;
			}

			if (ret == EVENT_FAILURE)
				return false;

			else if (ret == EVENT_SUCCESS) {
				state = (state + 1) % NSESS_MAX;
				continue;
			}

			else if (ret == EVENT_RETRY) {
				continue;
			}

			return true;
		}
	}

	int32_t http_raw_link::on_receive() {
		if(receives.has_done) {
			current->request.timestamp = time(nullptr);
			return EVENT_SUCCESS;
		}

		size_t avail = buffer->get_left_capacity();

		if (avail > 0 && receives.read_more) {
			uint8_t live_buf[2048];
			size_t slice = avail > sizeof(live_buf) ? sizeof(live_buf) : avail;
			ssize_t read = socket.read(live_buf, slice);

			if (read <= 0) {
				int32_t err = socket.get_errno();

				if (err == EINTR)
					return EVENT_RETRY;

				if (err == EAGAIN || err == EWOULDBLOCK) {
					if (time(nullptr) - timestamp >= params.timeout) {
						current->response.status.set(408); // 408 Request Timeout.
						receives.has_error = 1;
						return EVENT_SUCCESS;
					}

					return EVENT_AGAIN;
				}

				return EVENT_FAILURE;
			}

			/* if no LF cached, */
			if (receives.found_lf < 0) {
				/* find LF from live buffer. */
				if (void* t = memchr(live_buf, '\n', read)) {
					size_t offset = size_t((uint8_t*) t - live_buf);
					receives.found_lf = ssize_t(buffer->get_size() + offset);
				}
			}

			/* write to buffer. */
			buffer->write(live_buf, read);
			receives.read_more = 0;
		}

		else if (receives.found_lf < 0) {
			if (avail > 0) {
				receives.read_more = 1;
				return EVENT_RETRY;
			}

			/* protocol error: request headers are too large. */
			current->response.status.set(431);
			receives.has_error = 1;
			return EVENT_SUCCESS;
		}

		/* more bytes required. */
		if (receives.found_lf < 0)
			return EVENT_AGAIN;

		if (!receives.has_target) {
			size_t expected_size = receives.found_lf + 1;

			if (line_buf.size() < expected_size)
				line_buf.resize(expected_size);

			buffer->read(&line_buf[0], expected_size);

			/* find next LF. */
			receives.found_lf = buffer->find('\n');

			/* malformed request ?*/
			if (!http_resource::try_parse(current->request.target, &line_buf[0], expected_size)) {
				current->response.status.set(400); // 400 Bad Request.
				receives.has_error = 1;
				return EVENT_SUCCESS;
			}

			receives.has_target = 1;
			return EVENT_RETRY;
		}

		else {
			size_t expected_size = receives.found_lf + 1;

			if (line_buf.size() < expected_size)
				line_buf.resize(expected_size);

			buffer->read(&line_buf[0], expected_size);

			if (receives.found_lf == 0 ||
			   (receives.found_lf == 1 && line_buf[0] == '\r'))
			{
				receives.has_done = 1;
				receives.found_lf = -1;
				return EVENT_RETRY;
			}

			/* find next LF. */
			http_header header_buf;
			receives.found_lf = buffer->find('\n');

			if (!http_header::try_parse(header_buf, &line_buf[0], expected_size)) {
				current->response.status.set(400); // 400 Bad Request.
				receives.has_error = 1;
				return EVENT_SUCCESS;
			}

			current->request.headers.vec.push_back(std::move(header_buf));
			return EVENT_RETRY;
		}

		return EVENT_AGAIN;
	}

	int32_t http_raw_link::on_handle() {
		if(!contexts.has_raised) {
			contexts.has_raised = 1;

			future_holder = asyncs->future_of([this]() {
				/* if has error, no parse headers. */
				if (!receives.has_error) {
					/**
					 * parse headers:
					 * 1. content-length,
					 * 2. transfer-encoding,
					 */
					auto host = current->request.headers.find_one(http_header::HOST);
					auto content_length = current->request.headers.find_one(http_header::CONTENT_LENGTH);
					auto transfer_encoding = current->request.headers.get(http_header::TRANSFER_ENCODING);
					auto no_such_header = current->request.headers.vec.end();
					const auto& method = current->request.target.get_method();

					/* set remote address. */
					current->local_addr = socket.get_local_addr();
					current->remote_addr = socket.get_remote_addr();

					union {
						ipv6_addr _ipv6;
						ipv4_addr _ipv4;
					};

					if (socket.get_local_addr(_ipv6))
						current->port = int32_t(_ipv6.port);

					else if (socket.get_local_addr(_ipv4))
						current->port = int32_t(_ipv4.port);

					/**
					 * remove port number from hostname.
					 */
					if (host == no_such_header) 
						current->hostname = current->local_addr;

					else {
						const char* hostname = host->get_value().c_str();
						const char* seperator;
						bool is_ipv6;
						ipv6_addr temp; 

						/* IPv6 connection. */
						if (!(is_ipv6 = socket.get_local_addr<ipv6_addr>(temp)))
							 seperator = (const char*)memchr(hostname, ':', host->get_value().size());
						else seperator = (const char*)memchr(hostname, ']', host->get_value().size());
						
						/* if `IP`:`PORT` notation, */
						if (seperator) {
							hostname = ltrim(hostname, size_t(seperator - hostname));

							if (is_ipv6 && *hostname == '[')
								++hostname;

							if (hostname != seperator) {
								http_header& header = const_cast<http_header&>(*host);
								header.set_value(std::string(hostname, size_t(seperator - hostname)));
							}
						}
					}

					if (!current->hostname.size())
						current->hostname = current->local_addr;

					/**
					 * "Messages MUST NOT include both a Content-Length header field and a non-identity transfer-coding.
					 * If the message does include a non-identity transfer-coding, the Content-Length MUST be ignored."
					 * (RFC 2616, Section 4.4)
					 */
					if (!transfer_encoding || !strnicmp(transfer_encoding, "identity", 8)) {
						if (content_length != no_such_header) {
							auto* handler = new http_raw_fixed_len_content_handler();

							handler->buffer = buffer;
							handler->feed = std::make_shared<http_raw_request_content>(buffer,
								size_t(handler->state.cont_left = to_int64(content_length->get_value())));

							(content_handler = handler)->on_initiate();
						}
					}
					else if (!strnicmp(transfer_encoding, "chunked", 7)) {
						auto* handler = new http_raw_chunked_content_handler();

						handler->buffer = buffer;
						handler->feed = std::make_shared<http_raw_request_content>(buffer, -1);

						(content_handler = handler)->on_initiate();
					}
					else {
						current->response.status.set(400);
						receives.has_error = 1;
					}

					/* if GET, DELETE with request-content, make it to 400 Bad Request. */
					if (content_handler) {
						if (!method.is(NMETHOD_REQUEST_CONTENT)) {
							/* but it isn't protocol error. just no set request-content. */
							current->response.status.set(400);

							/* unset feed and set skip flag. */
							content_handler->feed = nullptr;
							content_handler->skip_all = true;
						}

						else {
							current->request.content = content_handler->feed;
						}
					}


					/* parse query-string. */
					http_query_string::try_parse(
						current->request.queries,
						current->request.target.get_query_string());

					/* qualify path string. */
					current->request.target.set_path(
						qualify_path(current->request.target.get_path()));
				}

				listener->on_raw_context(current, receives.has_error);
			});

			receives.found_lf = -1;
		}

		if (future_holder && !future_holder.is_completed())
			return EVENT_AGAIN;

		if (context_state) {
			/*if (content_handler) {		// --> for skipping content body, don't delete here.
				content_handler->on_finalize();
				delete content_handler;

				content_handler = nullptr;
				return EVENT_RETRY;
			}*/

			if (!content_handler) {
				if (!future_holder.is_completed())
					return EVENT_AGAIN;

				return EVENT_SUCCESS;
			}

			//contexts.cont_skip = 1;
			content_handler->feed = nullptr;
			content_handler->skip_all = true;
		}

		/* if content feed is being or now skipping content bytes, */
		if (content_handler) {
			int32_t state = content_handler->on_event(socket);

			if (state == EVENT_SUCCESS) {
				content_handler->on_finalize();
				delete content_handler;

				content_handler = nullptr;
				return EVENT_RETRY;
			}

			return state;
			////if (contexts.cont_type == CONT_WEBSOCKET) {
			////	/* send buffers before receiving data frames. */


			////	/* for websockets, no skip needed. just disconnect it. */
			////	if (contexts.cont_skip) {
			////		contexts.cont_type = CONT_NONE;
			////		contexts.keep_alive = 0;
			////		return EVENT_RETRY;
			////	}

			////	size_t buffered_bytes = buffer->get_size();
			////	size_t received_bytes = 0;

			////	/* set marker for detecting incoming bytes. */
			////	if (buffered_bytes < size_t(contexts.cont_mark)) {
			////		contexts.cont_mark = buffered_bytes;
			////		return EVENT_RETRY;
			////	}

			////	if ((received_bytes = buffered_bytes - contexts.cont_mark) > 0) {
			////		if (contexts.cont_left <= 0) {

			////			/* at least, 2 bytes required. */
			////			if (received_bytes < 2)
			////				return EVENT_AGAIN;

			////			uint8_t header[14];

			////			/* peek header bytes. */
			////			size_t peeks = buffer->peek(header, 14);

			////			/**
			////				 0               1               2               3
			////				 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
			////				+-+-+-+-+-------+-+-------------+-------------------------------+
			////				|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
			////				|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
			////				|N|V|V|V|       |S|             |   (if payload len==126/127)   |
			////				| |1|2|3|       |K|             |                               |
			////				+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
			////				 4               5               6               7
			////				+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
			////				|     Extended payload length continued, if payload len == 127  |
			////				+ - - - - - - - - - - - - - - - +-------------------------------+
			////				 8               9               10              11
			////				+ - - - - - - - - - - - - - - - +-------------------------------+
			////				|                               |Masking-key, if MASK set to 1  |
			////				+-------------------------------+-------------------------------+
			////			*/

			////			/* before parsing header bytes, waiting its payload fields. */
			////			int8_t plen_type = 
			////				 (header[1] & 0x7f) == 0x7f ? 8 :		// --> requires 8 bytes more.
			////				((header[1] & 0x7f) == 0x7e ? 2 : 0);	// --> requires 2 bytes more.

			////			uint32_t mask_key = 0;

			////			contexts.cont_mask = (header[1] >> 7) == 1 ? 1 : 0; /* `mask` bit. */
			////			contexts.cont_opcd = header[0] & 0x0f;				/* `opcode` bit */

			////			/* non-masked frame is disallowed. */
			////			if (!contexts.cont_mask || 
			////			     contexts.cont_opcd == 0x08) // 0x08: --> connection close.
			////				 return EVENT_FAILURE;

			////			if (contexts.cont_opcd > 2) {
			////				/* control frames. */
			////				char live_buf[1024];

			////				// received_bytes
			////				//0x09: PING
			////				//0x0A: PONG
			////			}

			////			else {
			////				/* message frames. */

			////			}

			////			/* if not end of feed and reading yet, */
			////			if (current_feed) {
			////				/* wait its end. */
			////				if (!current_feed->is_end_of())
			////					return EVENT_AGAIN;

			////				/* disconnect current feed. */
			////				current_feed->disconnect();
			////			}

			////			if (peeks <  size_t(2) + plen_type + 4)  return EVENT_AGAIN;
			////			buffer->skip(size_t(2) + plen_type + 4); // --> skip header bytes.

			////			/* then update length markup. */
			////			contexts.cont_mark -= size_t(2) + plen_type + 4;
			////			received_bytes -= size_t(2) + plen_type + 4;

			////			contexts.cont_push = (header[0] >> 7) == 1 ? 1 : 0; /* `fin`    bit. */
			////			contexts.cont_left = !plen_type ? header[1] & 0x7f : (plen_type == 4 ?
			////				uint64_t((uint64_t(header[2]) << 56) | (uint64_t(header[3]) << 48) | (uint64_t(header[4]) << 40) | (uint64_t(header[5]) << 32) |
			////					     (uint64_t(header[6]) << 24) | (uint64_t(header[7]) << 16) | (uint64_t(header[8]) << 8 ) | (uint64_t(header[9]) << 0 )) :
			////				uint64_t((uint64_t(header[2]) << 8 ) | (uint64_t(header[3]) << 0 ))
			////			);

			////			contexts.cont_read = 0;
			////			if (contexts.cont_mask) {
			////				mask_key = (uint32_t(header[10]) << 24) | (uint32_t(header[11]) << 16) | (uint32_t(header[12]) << 8) | (uint32_t(header[13]) << 0);
			////				current_feed = std::make_shared<http_websock_message>(buffer, contexts.cont_left, mask_key);
			////			}

			////			else current_feed = std::make_shared<http_websock_message>(buffer, contexts.cont_left);

			////			/* notify message begins. */
			////			future_holder = asyncs->future_of([this]() {
			////				current->request.target.set_method(http_method::WS_NOTIFY);
			////				current->request.content = current_feed;

			////				listener->on_raw_context(current, receives.has_error);
			////			});
			////		}

			////		if (!current_feed || current_feed->is_disconnected()) {
			////			/* skip bytes. */
			////			contexts.cont_left -= buffer->skip(contexts.cont_left);
			////			return EVENT_AGAIN;
			////		}

			////		if (received_bytes) {
			////			bool is_last_notify = received_bytes >= size_t(contexts.cont_left);
			////			size_t cont_bytes = is_last_notify ? size_t(contexts.cont_left) : received_bytes;

			////			contexts.cont_left -= cont_bytes;
			////			contexts.cont_read += cont_bytes;
			////			received_bytes -= cont_bytes;

			////			if (cont_bytes) {
			////				current_feed->notify(cont_bytes, contexts.cont_push);
			////			}

			////			/* marks cont_bytes as read. */
			////			contexts.cont_mark += cont_bytes;
			////		}
			////	}

			////}
		}

		return EVENT_AGAIN;
	}

	int32_t http_raw_link::on_send() {
		/* generates response header bytes. */
		if (!sends.buffer_state) {
			future_holder = asyncs->future_of([this]() {
				auto& status = current->response.status;
				auto& headers = current->response.headers;
				std::string live_buf;
				size_t length = 0;

				/* prevent content changed during sending response. */
				content = current->response.content;

				/* qualify invalid or already ended stream to nullptr. */
				if (content && (!content->is_valid() || content->is_end_of()))
					content = nullptr;

				if (content) {
					/* try set non-block. */
					content->set_nonblock(true);
				}

				/* set `Date` header which is requested time. */
				if (!headers.get(http_header::DATE)) {
					headers.set(http_header::DATE, http_date(timestamp).stringify());
				}

				/* if `Server` header not set, set it as watermark from params. */
				if (params.advertise.enable && !headers.get(http_header::SERVER)) {
					headers.set(http_header::SERVER, params.advertise.watermark);
				}

				/* set `Content-Length` header or `Transfer-Encoding` header. */
				if (content && (length = content->get_length()) < 0) {
					/* requires chunked transfer. */
					headers.set(http_header::TRANSFER_ENCODING, "chunked");
					headers.unset(http_header::CONTENT_LENGTH);

					sends.out_type = 1;
				}

				else {
					headers.set(http_header::CONTENT_LENGTH, std::to_string(length));
					headers.unset(http_header::TRANSFER_ENCODING);

					sends.out_type = 0;
				}

				/* encode response headers. */
				status.stringify(live_buf);
				headers.stringify(live_buf);

				/* copy to line buffer. */
				if (line_buf.size() < live_buf.size())
					line_buf.resize(live_buf.size());

				/* resize line_buf to 1/2 size of buffer chunk. */
				if (line_buf.size() < params.buffer_size_in_kb * 512)
					line_buf.resize(params.buffer_size_in_kb * 512);

				/* if chunk encoding, buffer should have blank at front of bytes. */
				if (sends.out_type) {
					size_t temp = line_buf.size(), chars = 1;
					while (temp) { ++chars; temp >>= 4; }

					sends.buffer_bnk = int16_t(chars + 2); // for: hexhex...CRLF.
					sends.buffer_pad = 2;		  // for: terminating CRLF.
				}

				memcpy(&line_buf[0], live_buf.c_str(), live_buf.size());
				sends.buffer_len = live_buf.size();
			});

			sends.buffer_state = 1;
		}
		
		/* wait asynchronous task completed. */
		if (!future_holder.is_completed())
			return EVENT_AGAIN;

		if (!sends.buffer_len) {
			/* test content has reached on end of stream or not.*/
			if (!content || content->is_end_of()) {
				if (!sends.out_type || !sends.out_state) {
					/* disconnect if protocol error occurred. */
					if (receives.has_error)
						return EVENT_FAILURE;

					return EVENT_SUCCESS;
				}

				/* if chunk terminator required, */
				sends.out_state = 0;

				/* write "0\r\n\r\n" to buffer. */
				if (line_buf.size() < 5)
					line_buf.resize(5);

				memcpy(&line_buf[0], "0\r\n\r\n", 5);
				sends.buffer_len = 5;
				return EVENT_RETRY;
			}

			if (!content->is_nonblock()) {
				/* asynchronous reading. */
				future_holder = asyncs->future_of([this]() {
					char* front = &line_buf[0] + sends.buffer_bnk;
					size_t safe_size = line_buf.size() - size_t(sends.buffer_bnk + sends.buffer_pad);
					ssize_t read = content->read(front, safe_size);

					if (read <= 0) {
						/* may done. */
						content = nullptr;
						sends.out_state = 1;
						return;
					}

					/* if chunk encoding, prepend hex length and append terminating CRLF. */
					if (sends.out_type) {
						safe_size = to_hex(sends.hex_buf, sizeof(sends.hex_buf) - sends.buffer_pad, read);

						sends.hex_buf[safe_size + 0] = '\r';
						sends.hex_buf[safe_size + 1] = '\n';
						safe_size += 2;

						memcpy(front - safe_size, sends.hex_buf, safe_size);
						memmove(&line_buf[0], front - safe_size, read + safe_size);
						front = &line_buf[0] + read + safe_size;

						*(front++) = '\r';
						*(front++) = '\n';

						read += safe_size + 2;
					}

					sends.buffer_len = read;
					});

				return EVENT_AGAIN;
			}

			/* if non-blocking stream, read on event loop. */
			char* front = &line_buf[0] + sends.buffer_bnk;
			size_t safe_size = line_buf.size() - size_t(sends.buffer_bnk + sends.buffer_pad);
			ssize_t read = content->read(front, safe_size);

			if (read <= 0) {
				int32_t err = content->get_errno();

				if (err == EINTR)
					return EVENT_RETRY;

				if (err == EWOULDBLOCK)
					return EVENT_AGAIN;

				else if (err)
					return EVENT_FAILURE;

				content = nullptr;
				sends.out_state = 1;
			}

			/* if chunk encoding, prepend hex length and append terminating CRLF.*/
			if (sends.out_type) {
				safe_size = to_hex(sends.hex_buf, sizeof(sends.hex_buf) - sends.buffer_pad, read);

				sends.hex_buf[safe_size + 0] = '\r';
				sends.hex_buf[safe_size + 1] = '\n';
				safe_size += 2;

				memcpy(front - safe_size, sends.hex_buf, safe_size);
				memmove(&line_buf[0], front - safe_size, read + safe_size);
				front = &line_buf[0] + read + safe_size;

				*(front++) = '\r';
				*(front++) = '\n';

				read += safe_size + sends.buffer_pad;
			}

			sends.buffer_len = read;
		}

		if (sends.buffer_len) { /* if there are buffered bytes, flush them. */
			ssize_t sent = socket.write(&line_buf[0] + sends.buffer_off, sends.buffer_len);

			if (sent <= 0) {
				int32_t err = socket.get_errno();

				if (err == EINTR)
					return EVENT_RETRY;

				if (err == EAGAIN || err == EWOULDBLOCK) {
					return EVENT_AGAIN;
				}

				return EVENT_FAILURE;
			}

			sends.buffer_off += sent;
			sends.buffer_len -= sent;

			if (!sends.buffer_len) {
				sends.buffer_off = 0;
				sends.buffer_len = 0;
			}
		}

		return EVENT_AGAIN;
	}
}
}