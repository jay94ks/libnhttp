#include "nhttp/server/extensions/vpath.hpp"

#include <algorithm>

namespace nhttp::server {

	namespace {

		bool path_has_prefix(std::string_view path, std::string_view prefix) noexcept {
			if (prefix.empty() || prefix == "/")
				return true;

			if (prefix.back() == '/')
				prefix.remove_suffix(1);

			if (path.size() < prefix.size() || path.substr(0, prefix.size()) != prefix)
				return false;

			return path.size() == prefix.size() || path[prefix.size()] == '/';
		}

	}

	std::string_view subpath_of(request& req) {
		if (vpath_tag* tag = req.tags.get<vpath_tag>(); tag && !tag->subpaths.empty())
			return tag->subpaths.back();

		return req.path();
	}

	vpath::vpath(std::string base_path, std::uint32_t prio)
		: base_path_(std::move(base_path)), priority_(prio)
	{
		if (base_path_.size() > 1 && base_path_.back() == '/')
			base_path_.pop_back();
	}

	void vpath::extends(extension_ptr ext) {
		registry_.add(std::move(ext));
	}

	async::task<bool> vpath::wants(request& req) {
		co_return path_has_prefix(subpath_of(req), base_path_);
	}

	async::task<std::optional<response>> vpath::on_handle(request&) {
		co_return std::nullopt;
	}

	async::task<response> vpath::handle(request& req) {
		const std::string_view current = subpath_of(req);
		const std::size_t prefix_len = std::min(current.size(), base_path_.size());
		std::string remainder(current.substr(prefix_len));

		if (remainder.empty())
			remainder = "/";

		vpath_tag& tag = req.tags.ensure<vpath_tag>();
		tag.subpaths.push_back(remainder);

		std::optional<response> nested = co_await registry_.dispatch(req);
		response result = nested ? std::move(*nested) : response();
		bool handled = nested.has_value();

		if (!handled) {
			if (std::optional<response> own = co_await on_handle(req)) {
				result = std::move(*own);
				handled = true;
			}
		}

		tag.subpaths.pop_back();

		if (!handled)
			result = make_response(404);

		co_return result;
	}

}
