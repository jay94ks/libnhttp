#include "nhttp/server/extensions/vhost.hpp"

namespace nhttp::server {

	vhost::vhost(predicate_type predicate, std::uint32_t prio)
		: predicate_(std::move(predicate)), priority_(prio)
	{
	}

	void vhost::extends(extension_ptr ext) {
		registry_.add(std::move(ext));
	}

	async::task<bool> vhost::wants(request& req) {
		co_return predicate_ && predicate_(req.hostname);
	}

	async::task<response> vhost::handle(request& req) {
		std::optional<response> nested = co_await registry_.dispatch(req);
		co_return nested ? std::move(*nested) : make_response(404);
	}

	std::shared_ptr<vhost> vhost_for(std::string exact_hostname) {
		return std::make_shared<vhost>([hostname = std::move(exact_hostname)](const std::string& h) { return h == hostname; });
	}

	std::shared_ptr<vhost> vhost_for(const std::regex& pattern) {
		return std::make_shared<vhost>([pattern](const std::string& h) { return std::regex_match(h, pattern); });
	}

	std::shared_ptr<vhost> vhost_for(vhost::predicate_type predicate) {
		return std::make_shared<vhost>(std::move(predicate));
	}

}
