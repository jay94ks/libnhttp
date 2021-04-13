#pragma once
#include "http_raw_context.hpp"
#include "http_taggable.hpp"
#include "../io/memory_stream.hpp"
#include "../io/range_stream.hpp"
#include "../protocol/http_mime_type.hpp"
#include "../protocol/http_form_data.hpp"

namespace nhttp {
namespace server {

	class http_link;
	class http_request;
	class http_response;
	class http_context;
	class http_listener;
	struct http_listener_proxy;

	/* shortcut definitions: */
	using http_request_ptr = std::shared_ptr<http_request>;
	using http_response_ptr = std::shared_ptr<http_response>;
	using http_context_ptr = std::shared_ptr<http_context>;

	/**
	 * class http_request.
	 * wraps raw context to facade request.
	 */
	class NHTTP_API http_request : public http_taggable {
		friend class http_context;

	private:
		std::shared_ptr<http_raw_context> raw;

	public:
		http_request(const std::shared_ptr<http_raw_context>& raw)
			: raw(raw) { }

		~http_request() {
			for (auto& each : tags) {
				each.second.dtor(each.second.data);
			}

			tags.clear();
		}

	public:
		/* form data. */
		http_form_data forms;

	public:
		/* get raw link object. */
		inline std::shared_ptr<http_link> get_link() const { return raw->link; }

		/* get request timestamp. (read only) */
		inline time_t get_timestamp() const { return raw->request.timestamp; }

		/* get request port number. (read only) */
		inline int32_t get_port() const { return raw->port; }

		/* get request host name. (read only) */
		inline const std::string& get_hostname() const { return raw->hostname; }

		/* get local address. (read only) */
		inline const std::string& get_local_addr() const { return raw->local_addr; }

		/* get remote address. (read only) */
		inline const std::string& get_remote_addr() const { return raw->remote_addr; }

		/* get target resource information. (read only) */
		inline const http_resource& get_target() const { return raw->request.target; }

		/* get parsed query string. */
		inline http_query_string& get_queries() { return raw->request.queries; }
		inline const http_query_string& get_queries() const { return raw->request.queries; }

		/* get request headers. */
		inline http_headers& get_headers() { return raw->request.headers; }
		inline const http_headers& get_headers() const { return raw->request.headers; }

		/* get request body stream. (read only) */
		inline const std::shared_ptr<stream>& get_request_body() const { return raw->request.content; }
	};

	/**
	 * class http_response.
	 * wraps raw response to facade response.
	 */
	class NHTTP_API http_response : public http_raw_response {
	public:
		http_response(const http_status& status) { this->status = status; }

		/* shortcut of http_response(http_status::...). */
		http_response(int32_t code, const char* phrase = nullptr) {
			NHTTP_ENSURE(code >= 100 && code <= 515,
				"invalid http response code!");

			status.set(code, phrase);
		}

		http_response(const std::string& str, const http_mime_type& mime = http_mime_type::TEXT_HTML) {
			(content = std::make_shared<memory_stream>())
				->write(str.c_str(), str.size());

			std::string s = mime.stringify();
			content->seek(0, SEEK_SET);

			if (s.size() > 0) {
				headers.set(http_header::CONTENT_TYPE, s);
			}
		}

		http_response(const std::wstring& str, const http_mime_type& mime = http_mime_type::TEXT_HTML)
			: http_response(wcs_to_mbs(str), mime) { }

		http_response(const std::shared_ptr<stream>& stream, size_t length, const http_mime_type& mime = http_mime_type::APPLICATION_OCTET)
			: http_response(stream, 0, length, mime) { }

		http_response(const std::shared_ptr<stream>& stream, const http_mime_type& mime = http_mime_type::APPLICATION_OCTET) {
			std::string s = mime.stringify();
			content = stream;

			if (s.size() > 0) {
				headers.set(http_header::CONTENT_TYPE, s);
			}
		}

		http_response(const std::shared_ptr<stream>& stream, size_t offset, size_t length, const http_mime_type& mime = http_mime_type::APPLICATION_OCTET) {
			std::string s = mime.stringify();
			content = std::make_shared<range_stream>(stream, offset, offset + length);

			if (s.size() > 0) {
				headers.set(http_header::CONTENT_TYPE, s);
			}
		}
	};

	/**
	 * make response object.
	 */
	template<typename ... types>
	inline http_response_ptr make_response(types&& ... args) {
		return std::make_shared<http_response>(std::forward<types>(args) ...);
	}

	/**
	 * class http_context.
	 * wraps raw context to facade context.
	 */
	class NHTTP_API http_context {
		friend class http_listener;

	private:
		std::shared_ptr<http_raw_context> raw;
		std::weak_ptr<http_context> this_weak;
		bool _is_closed;

	public:
		http_context(const std::shared_ptr<http_raw_context>& raw)
			: raw(raw), _is_closed(false)
		{
			request = std::make_shared<http_request>(raw);
			response = std::make_shared<http_response>(200);
			link = raw->link;
		}

	public:
		http_request_ptr request;
		http_response_ptr response;
		std::shared_ptr<http_link> link;
		std::shared_ptr<http_taggable> global;

	public:
		/* determines this context has closed or not. */
		inline bool is_closed() const {
			return _is_closed;
		}

		/* close context and send response. */
		inline bool close(bool keepalive = true) {
			NHTTP_ENSURE(
				!_is_closed,
				"this stream is already closed."
			);

			if (!_is_closed) {
				NHTTP_INIT_ASSERT(
					request, 
					"request object is overwritten at somewhere!"
				);

				_is_closed = true;
				if (!response) {
					response = make_response(200);
				}

				if (request->hooks.size()) {
					for (auto each : request->hooks) {
						each->on_response(*request.get(), *response.get());
					}
				}

				/* move assignment. */
				raw->response.status = std::move(response->status);
				raw->response.headers = std::move(response->headers);
				raw->response.content = response->content;

				/* then close context. */
				return raw->close(keepalive);
			}

			return false;
		}
	};

}
}