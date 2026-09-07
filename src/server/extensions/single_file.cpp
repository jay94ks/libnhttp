#include "nhttp/server/extensions/single_file.hpp"
#include "nhttp/server/extensions/static_content.hpp"
#include "nhttp/protocol/http_method.hpp"
#include "nhttp/protocol/http_mime_type.hpp"
#include "nhttp/io/file_stream.hpp"

namespace {

	/* same wants()-then-handle() stat-caching tradeoff as overlay's
	 * resolve_cache (see its doc comment and PLAN.md's P2), and the same
	 * `owner` guard for the same reason: multiple single_file instances can be
	 * mounted on one listener, each at a different fixed path, and the tag
	 * slot below is keyed only by C++ type — not by which instance wrote it —
	 * so a cache left by one instance's wants() (which returned false and was
	 * never handle()'d) must never be mistaken for another instance's result. */
	struct stat_cache {
		const void* owner = nullptr;
		bool computed = false;
		std::optional<nhttp::platform::file_info> value;
	};

}

namespace nhttp::server {

	single_file::single_file(std::string path, async::thread_pool& pool,
		std::optional<std::string> mime_override, std::uint32_t prio)
		: path_(std::move(path)), pool_(&pool), mime_override_(std::move(mime_override)), priority_(prio)
	{
	}

	async::task<std::optional<platform::file_info>> single_file::stat_file(request& req) const {
		stat_cache& cache = req.tags.ensure<stat_cache>();

		if (cache.computed && cache.owner == this)
			co_return cache.value;

		cache.owner = this;
		cache.computed = true;

		const platform::file_info info = co_await pool_->run(*req.io_ctx, [this] { return platform::stat_file(path_); });

		cache.value = (info.kind == platform::file_kind::regular_file) ? std::optional(info) : std::nullopt;
		co_return cache.value;
	}

	async::task<bool> single_file::wants(request& req) {
		if (req.method() != protocol::http_method::GET() && req.method() != protocol::http_method::HEAD())
			co_return false;

		co_return (co_await stat_file(req)).has_value();
	}

	async::task<response> single_file::handle(request& req) {
		const std::optional<platform::file_info> info = co_await stat_file(req);

		if (!info)
			co_return make_response(404);

		std::unique_ptr<io::file_stream> file = co_await io::file_stream::open(*req.io_ctx, *pool_, path_, "rb", info->size);

		if (!file)
			co_return make_response(404);

		std::shared_ptr<io::stream> shared_file = std::move(file);
		const std::string etag = make_etag(info->mtime, info->size);
		const std::string_view mime = mime_override_ ? std::string_view(*mime_override_) : protocol::mime_type_from_extension(path_);

		co_return co_await serve_stream_conditionally(req, shared_file, info->size, info->mtime, mime, etag);
	}

}
