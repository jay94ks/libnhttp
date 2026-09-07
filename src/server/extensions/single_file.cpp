#include "nhttp/server/extensions/single_file.hpp"
#include "nhttp/server/extensions/static_content.hpp"
#include "nhttp/protocol/http_method.hpp"
#include "nhttp/protocol/http_mime_type.hpp"
#include "nhttp/io/file_stream.hpp"

namespace nhttp::server {

	single_file::single_file(std::string path, async::thread_pool& pool,
		std::optional<std::string> mime_override, std::uint32_t prio)
		: path_(std::move(path)), pool_(&pool), mime_override_(std::move(mime_override)), priority_(prio)
	{
	}

	async::task<std::optional<platform::file_info>> single_file::stat_file(request& req) const {
		const platform::file_info info = co_await pool_->run(*req.io_ctx, [this] { return platform::stat_file(path_); });

		if (info.kind != platform::file_kind::regular_file)
			co_return std::nullopt;

		co_return info;
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

		std::unique_ptr<io::file_stream> file = co_await io::file_stream::open(*req.io_ctx, *pool_, path_, "rb");

		if (!file)
			co_return make_response(404);

		std::shared_ptr<io::stream> shared_file = std::move(file);
		const std::string etag = make_etag(info->mtime, info->size);
		const std::string_view mime = mime_override_ ? std::string_view(*mime_override_) : protocol::mime_type_from_extension(path_);

		co_return co_await serve_stream_conditionally(req, shared_file, info->size, info->mtime, mime, etag);
	}

}
