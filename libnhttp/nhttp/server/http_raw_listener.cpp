#include "http_raw_listener.hpp"
#include "../asyncs/context.hpp"

#include "internals/http_raw_link.hpp"
#include "internals/http_chunked_buffer.hpp"

namespace nhttp {
namespace server {

	http_raw_listener::http_raw_listener(const socket_watcher& watcher, const http_params& params)
		: base::listener_base(watcher, params.worker_count), params(params)
	{
		chunk_alloc = std::make_shared<http_chunked_alloc>(
			params.max_total_buffers, params.buffer_size_in_kb * 1024);
	}

	base::session_base* http_raw_listener::on_enter() {
		if (http_chunked_bytes* chunk = chunk_alloc->alloc()) {
			auto buffer = std::make_shared<http_chunked_buffer>(chunk_alloc, chunk);
			return new http_raw_link(this, buffer);
		}

		return nullptr;
	}

	void http_raw_listener::on_leave(base::session_base* link) {
		delete link;
	}

}
}