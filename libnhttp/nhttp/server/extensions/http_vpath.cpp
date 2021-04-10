#include "http_vpath.hpp"
#include "../../hal/rwlock_t.hpp"
#include "../../utils/path.hpp"

namespace nhttp {
namespace server {

	http_vpath::http_vpath(std::string path)
		: http_extendable_extension(0xf0001000), base_path(qualify_path(path))
	{
	}

	bool http_vpath::on_collect(std::shared_ptr<http_context> context) {
		const auto& req = subpath_of(context);

		if (req.size() >= base_path.size()) {
			if (strnicmp(req.c_str(), base_path.c_str(), base_path.size()))
				return false;

			char ch = req[base_path.size()];
			if (!ch || ch == '/')
				return true;
		}

		return false;
	}

	/* called before calling the on_handle method, test if it can be handled. */
	bool http_vpath::on_enter(std::shared_ptr<http_context> context) {
		http_vpath_tag* tag = context->link->ensured_tag<http_vpath_tag>();
		const auto& req = subpath_of(context);

		/* push current vpath instance. */
		tag->vpaths.push(this);

		/* make sub-path string. */
		if (req.size() == base_path.size() || (
			req.size() == base_path.size() + 1 && req[base_path.size()] == '/'))
			 tag->subpaths.push(std::string());
		else tag->subpaths.push(req.c_str() + base_path.size() + 1);

		return true; 
	}

	void http_vpath::on_leave(std::shared_ptr<http_context> context) {
		http_vpath_tag* tag = context->link->get_tag_ptr<http_vpath_tag>();
		
		/* restore previous state. */
		if (tag) {
			tag->vpaths.pop();
			tag->subpaths.pop();
		}
	}


}
}