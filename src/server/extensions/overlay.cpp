#include "nhttp/server/extensions/overlay.hpp"
#include "nhttp/server/extensions/static_content.hpp"
#include "nhttp/server/extensions/vpath.hpp"
#include "nhttp/protocol/http_method.hpp"
#include "nhttp/protocol/http_mime_type.hpp"
#include "nhttp/io/file_stream.hpp"

namespace nhttp::server {

	overlay::overlay(std::string base_dir, std::string index_file, async::thread_pool& pool, std::uint32_t prio)
		: base_dir_(std::move(base_dir)), index_file_(std::move(index_file)), pool_(&pool), priority_(prio)
	{
		if (base_dir_.size() > 1 && base_dir_.back() == '/')
			base_dir_.pop_back();
	}

	async::task<std::optional<std::pair<std::string, struct stat>>> overlay::resolve(request& req) const {
		const std::optional<std::string> qualified = qualify_relative_path(subpath_of(req));

		if (!qualified)
			co_return std::nullopt;

		std::string fs_path = base_dir_;

		if (!qualified->empty()) {
			fs_path += '/';
			fs_path += *qualified;
		}

		async::io_context& ctx = *req.io_ctx;
		struct stat st{};
		bool exists = co_await pool_->run(ctx, [fs_path, &st] { return ::stat(fs_path.c_str(), &st) == 0; });

		if (exists && S_ISDIR(st.st_mode)) {
			std::string index_path = fs_path;

			if (!index_path.empty() && index_path.back() != '/')
				index_path += '/';

			index_path += index_file_;

			exists = co_await pool_->run(ctx, [index_path, &st] { return ::stat(index_path.c_str(), &st) == 0; });
			fs_path = std::move(index_path);
		}

		if (!exists || !S_ISREG(st.st_mode))
			co_return std::nullopt;

		co_return std::make_pair(std::move(fs_path), st);
	}

	async::task<bool> overlay::wants(request& req) {
		if (req.method() != protocol::http_method::GET() && req.method() != protocol::http_method::HEAD())
			co_return false;

		// a path-traversal attempt is claimed unconditionally so handle() can give
		// a definitive 403, rather than silently falling through to whatever
		// extension is tried next.
		if (!qualify_relative_path(subpath_of(req)))
			co_return true;

		co_return (co_await resolve(req)).has_value();
	}

	async::task<response> overlay::handle(request& req) {
		if (!qualify_relative_path(subpath_of(req)))
			co_return make_response(403);

		std::optional<std::pair<std::string, struct stat>> resolved = co_await resolve(req);

		if (!resolved)
			co_return make_response(404);

		auto& [fs_path, st] = *resolved;

		std::unique_ptr<io::file_stream> file = co_await io::file_stream::open(*req.io_ctx, *pool_, fs_path, "rb");

		if (!file)
			co_return make_response(404);

		std::shared_ptr<io::stream> shared_file = std::move(file);
		const std::string etag = make_etag(st.st_mtime, st.st_size);
		const std::string_view mime = protocol::mime_type_from_extension(fs_path);

		co_return co_await serve_stream_conditionally(req, shared_file, st.st_size, st.st_mtime, mime, etag);
	}

}
