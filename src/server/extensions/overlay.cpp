#include "nhttp/server/extensions/overlay.hpp"
#include "nhttp/server/extensions/static_content.hpp"
#include "nhttp/server/extensions/vpath.hpp"
#include "nhttp/protocol/http_method.hpp"
#include "nhttp/protocol/http_mime_type.hpp"
#include "nhttp/io/file_stream.hpp"

namespace {

	/* bridges overlay::wants()'s resolve() to overlay::handle()'s so a request
	 * that reaches handle() after wants() already claimed it doesn't stat the
	 * filesystem twice — see PLAN.md's P2 (this exact tradeoff was flagged but
	 * deliberately deferred back in Phase 8, see CLAUDE.md). Guarded by `owner`
	 * (the specific overlay instance that computed it): extension_registry::
	 * dispatch() calls wants() then immediately handle() on the *same*
	 * extension with nothing else running in between (see extension.cpp), so
	 * this is safe even with multiple overlay instances mounted on one
	 * listener — a cache left behind by a different overlay's wants() (one
	 * that returned false and was never handle()'d) is simply never reused. */
	struct resolve_cache {
		const void* owner = nullptr;
		bool computed = false;
		std::optional<std::pair<std::string, nhttp::platform::file_info>> value;
	};

}

namespace nhttp::server {

	overlay::overlay(std::string base_dir, std::string index_file, async::thread_pool& pool, std::uint32_t prio)
		: base_dir_(std::move(base_dir)), index_file_(std::move(index_file)), pool_(&pool), priority_(prio)
	{
		if (base_dir_.size() > 1 && base_dir_.back() == '/')
			base_dir_.pop_back();
	}

	async::task<std::optional<std::pair<std::string, platform::file_info>>> overlay::resolve(request& req) const {
		resolve_cache& cache = req.tags.ensure<resolve_cache>();

		if (cache.computed && cache.owner == this)
			co_return cache.value;

		cache.owner = this;
		cache.computed = true;

		const std::optional<std::string> qualified = qualify_relative_path(subpath_of(req));

		if (!qualified) {
			cache.value = std::nullopt;
			co_return cache.value;
		}

		std::string fs_path = base_dir_;

		if (!qualified->empty()) {
			fs_path += '/';
			fs_path += *qualified;
		}

		async::io_context& ctx = *req.io_ctx;
		platform::file_info info = co_await pool_->run(ctx, [fs_path] { return platform::stat_file(fs_path); });

		if (info.kind == platform::file_kind::directory) {
			std::string index_path = fs_path;

			if (!index_path.empty() && index_path.back() != '/')
				index_path += '/';

			index_path += index_file_;

			info = co_await pool_->run(ctx, [index_path] { return platform::stat_file(index_path); });
			fs_path = std::move(index_path);
		}

		if (info.kind != platform::file_kind::regular_file) {
			cache.value = std::nullopt;
			co_return cache.value;
		}

		cache.value = std::make_pair(std::move(fs_path), info);
		co_return cache.value;
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

		std::optional<std::pair<std::string, platform::file_info>> resolved = co_await resolve(req);

		if (!resolved)
			co_return make_response(404);

		auto& [fs_path, info] = *resolved;

		std::unique_ptr<io::file_stream> file = co_await io::file_stream::open(*req.io_ctx, *pool_, fs_path, "rb", info.size);

		if (!file)
			co_return make_response(404);

		std::shared_ptr<io::stream> shared_file = std::move(file);
		const std::string etag = make_etag(info.mtime, info.size);
		const std::string_view mime = protocol::mime_type_from_extension(fs_path);

		co_return co_await serve_stream_conditionally(req, shared_file, info.size, info.mtime, mime, etag);
	}

}
