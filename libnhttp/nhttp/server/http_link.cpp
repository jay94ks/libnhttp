#include "http_link.hpp"
#include "internals/http_raw_link.hpp"

namespace nhttp {
namespace server {

	void http_link_driver::replace_driver(std::shared_ptr<http_link_driver> new_driver) {
		if (raw_link) {
			raw_link->on_replace(std::move(new_driver));
		}
	}

}
}