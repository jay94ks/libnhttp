#pragma once

#include "../request.hpp"
#include "../response.hpp"
#include "../../async/task.hpp"

#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace nhttp::server {

	/* an opaque strong ETag combining mtime and size — cheap to compute, no
	 * content hashing required. */
	std::string make_etag(std::time_t mtime, std::int64_t size);

	/**
	 * qualifies a URL path segment-by-segment (resolving "." and ".."),
	 * rejecting anything that would escape above the root — the one, shared
	 * implementation used everywhere a request path is mapped onto a
	 * filesystem path, per CONCEPTS.md's "path normalization is centralized"
	 * principle. returns nullopt if the path attempts to traverse above root.
	 */
	std::optional<std::string> qualify_relative_path(std::string_view path);

	/**
	 * the shared conditional-GET (If-Match/If-None-Match/If-Modified-Since/
	 * If-Range) + byte-Range engine used by both directory-overlay and
	 * single-file serving — see CONCEPTS.md §3: this logic must not be
	 * duplicated between the two.
	 */
	async::task<response> serve_stream_conditionally(
		const request& req,
		std::shared_ptr<io::stream> content,
		std::int64_t size,
		std::time_t mtime,
		std::string_view mime,
		const std::string& etag);

}
