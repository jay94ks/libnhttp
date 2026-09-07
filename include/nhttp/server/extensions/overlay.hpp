#pragma once

#include "../extension.hpp"
#include "../../async/thread_pool.hpp"

#include <optional>
#include <string>
#include <utility>
#include <sys/stat.h>

namespace nhttp::server {

	/**
	 * class overlay.
	 * maps a URL path prefix onto a filesystem directory, serving files with
	 * full conditional-GET + byte-Range support (via static_content.hpp's
	 * shared engine). requests for a directory (or the mount root) fall back to
	 * `index_file`.
	 */
	class overlay final : public extension {
	public:
		/* `pool` is shared across all workers (thread-safe to submit jobs from
		 * any thread); no io_context is taken here — every request resumes on
		 * its OWN connection's context (request::io_ctx), never one fixed at
		 * construction time. see request.hpp's doc comment on io_ctx for why. */
		overlay(std::string base_dir, std::string index_file, async::thread_pool& pool,
			std::uint32_t prio = 0xE0000000u);

	public:
		std::uint32_t priority() const noexcept override { return priority_; }
		async::task<bool> wants(request& req) override;
		async::task<response> handle(request& req) override;

	private:
		/* resolves the request's subpath to an existing regular file (following
		 * index_file_ for directories), or nullopt if nothing servable exists —
		 * shared by wants() (so this extension only claims requests it can
		 * actually serve, letting others fall through otherwise) and handle().
		 * nullopt is also returned in the path-traversal-rejected case; wants()
		 * treats that as "not mine" and handle() re-checks to report 403. */
		async::task<std::optional<std::pair<std::string, struct stat>>> resolve(request& req) const;

		std::string base_dir_;
		std::string index_file_;
		async::thread_pool* pool_;
		std::uint32_t priority_;
	};

	inline std::shared_ptr<overlay> overlay_of(std::string base_dir, std::string index_file, async::thread_pool& pool) {
		return std::make_shared<overlay>(std::move(base_dir), std::move(index_file), pool);
	}

}
