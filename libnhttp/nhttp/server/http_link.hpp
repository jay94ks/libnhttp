#pragma once
#include "../types.hpp"
#include "http_taggable.hpp"

namespace nhttp {
namespace server {

	class http_raw_link;

	/**
	 * class http_link.
	 * logical http link (shared by http_raw_link)
	 */
	class NHTTP_API http_link : public http_taggable {
		friend class http_raw_link;

	protected:
		std::atomic<bool> _is_alive;

	public:
		inline bool is_alive() const { return _is_alive; }
	};

}
}