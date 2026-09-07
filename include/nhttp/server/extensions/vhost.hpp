#pragma once

#include "../extension.hpp"

#include <functional>
#include <regex>
#include <string>

namespace nhttp::server {

	/**
	 * class vhost.
	 * mounts a nested extension_registry under a hostname predicate (exact
	 * match, regex, or an arbitrary lambda) — matched against request::hostname
	 * (the Host header, port stripped).
	 */
	class vhost final : public extension {
	public:
		using predicate_type = std::function<bool(const std::string&)>;

		explicit vhost(predicate_type predicate, std::uint32_t prio = 0x80000000u);

	public:
		void extends(extension_ptr ext);

		std::uint32_t priority() const noexcept override { return priority_; }
		async::task<bool> wants(request& req) override;
		async::task<response> handle(request& req) override;

	private:
		predicate_type predicate_;
		std::uint32_t priority_;
		extension_registry registry_;
	};

	std::shared_ptr<vhost> vhost_for(std::string exact_hostname);
	std::shared_ptr<vhost> vhost_for(const std::regex& pattern);
	std::shared_ptr<vhost> vhost_for(vhost::predicate_type predicate);

}
