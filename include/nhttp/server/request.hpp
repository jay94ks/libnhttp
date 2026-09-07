#pragma once

#include "tag_storage.hpp"
#include "../protocol/http_resource.hpp"
#include "../protocol/http_header.hpp"
#include "../io/stream.hpp"
#include "../async/io_context.hpp"

#include <memory>
#include <string>

namespace nhttp::server {

	/**
	 * class request.
	 * the facade handed to user code: parsed request line/headers plus the
	 * decoded body as a plain stream (never null — an empty memory_stream when
	 * there's no body), and a request-scoped tag_storage (reset every request,
	 * unlike the connection's own tag_storage — see CONCEPTS.md §1).
	 */
	class request {
	public:
		protocol::http_resource resource;
		protocol::http_headers headers;
		std::shared_ptr<io::stream> body;
		std::string hostname; // from the Host header, port (and IPv6 brackets) stripped
		tag_storage tags;

		/**
		 * the io_context actually running this request's connection right now.
		 * any extension that offloads work to a thread_pool (overlay,
		 * single_file, ...) MUST resume on this exact context, not one fixed at
		 * the extension's own construction time — a connection is pinned to
		 * whichever worker accepted it (CLAUDE.md's concurrency invariant), and
		 * resuming on a different one would silently violate that. never null
		 * while a request is being handled.
		 */
		async::io_context* io_ctx = nullptr;

	public:
		const protocol::http_method& method() const noexcept { return resource.method; }
		const std::string& path() const noexcept { return resource.path; }
	};

}
