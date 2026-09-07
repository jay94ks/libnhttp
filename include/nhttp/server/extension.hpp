#pragma once

#include "request.hpp"
#include "response.hpp"
#include "../async/task.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace nhttp::server {

	/**
	 * class extension.
	 * the two-phase contract every listener-level plugin implements: a cheap
	 * `wants` predicate, then `handle` to actually produce a response. tried in
	 * ascending priority order by an extension_registry, first acceptor wins —
	 * see CONCEPTS.md §4. 0x00000000-0xefffffff is free for user extensions;
	 * 0xf0000000+ is reserved for library-internal ones (matches the historical
	 * convention this carries forward).
	 */
	class extension {
	public:
		virtual ~extension() = default;

	public:
		virtual std::uint32_t priority() const noexcept { return 0x80000000u; }

		virtual async::task<bool> wants(request& req) = 0;
		virtual async::task<response> handle(request& req) = 0;
	};

	using extension_ptr = std::shared_ptr<extension>;

	/**
	 * class extension_registry.
	 * a priority-ordered collection of extensions, dispatched by trying `wants`
	 * on each in turn (ascending priority) and calling `handle` on the first
	 * that accepts.
	 */
	class extension_registry {
	public:
		void add(extension_ptr ext);

		/* nullopt if no extension accepted the request. */
		async::task<std::optional<response>> dispatch(request& req);

	private:
		std::vector<extension_ptr> extensions_; // kept sorted by priority()
	};

}
