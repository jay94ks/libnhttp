#include "nhttp/server/response.hpp"
#include "nhttp/io/memory_stream.hpp"

namespace nhttp::server {

	response make_response(int status_code) {
		response r;
		r.status = protocol::http_status(status_code);
		r.content_length = 0;
		return r;
	}

	response make_response(std::string text, std::string_view mime) {
		response r;
		r.status = protocol::http_status(200);
		r.headers.set(std::string(protocol::header_names::CONTENT_TYPE), std::string(mime));

		std::vector<std::uint8_t> bytes(text.begin(), text.end());
		r.content_length = static_cast<std::int64_t>(bytes.size());
		r.body = std::make_shared<io::memory_stream>(std::move(bytes));

		return r;
	}

	response make_response(std::shared_ptr<io::stream> body, std::string_view mime, std::int64_t length) {
		response r;
		r.status = protocol::http_status(200);
		r.headers.set(std::string(protocol::header_names::CONTENT_TYPE), std::string(mime));
		r.body = std::move(body);
		r.content_length = length >= 0 ? length : (r.body ? r.body->get_length() : 0);

		return r;
	}

}
