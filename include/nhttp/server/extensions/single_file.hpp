#pragma once

#include "../extension.hpp"
#include "../../async/thread_pool.hpp"

#include <optional>
#include <string>
#include <sys/stat.h>

namespace nhttp::server {

	/**
	 * class single_file.
	 * serves one fixed file at every path this extension is asked about — the
	 * same conditional-GET + Range engine as overlay (static_content.hpp),
	 * just resolving a fixed path instead of a per-request one.
	 */
	class single_file final : public extension {
	public:
		/* no io_context is taken here — see overlay.hpp's doc comment on why:
		 * every request resumes thread_pool work on its OWN connection's
		 * context (request::io_ctx), never one fixed at construction time. */
		single_file(std::string path, async::thread_pool& pool,
			std::optional<std::string> mime_override = std::nullopt, std::uint32_t prio = 0x80000000u);

	public:
		std::uint32_t priority() const noexcept override { return priority_; }
		async::task<bool> wants(request& req) override;
		async::task<response> handle(request& req) override;

	private:
		async::task<std::optional<struct stat>> stat_file(request& req) const;

		std::string path_;
		async::thread_pool* pool_;
		std::optional<std::string> mime_override_;
		std::uint32_t priority_;
	};

	inline std::shared_ptr<single_file> file_of(std::string path, async::thread_pool& pool) {
		return std::make_shared<single_file>(std::move(path), pool);
	}

}
