#pragma once
#include "http_extension.hpp"
#include "../http_context.hpp"
#include "../http_link.hpp"
#include <stack>

namespace nhttp {
namespace server {

	class http_vpath;
	using http_vpath_ptr = std::shared_ptr<http_vpath>;

	/**
	 * class http_path_tag.
	 * tag for marking the request is handled by.
	 */
	class NHTTP_API http_vpath_tag {
	public:
		std::stack<http_vpath*> vpaths;
		std::stack<std::string> subpaths;
	};

	/**
	 * class http_vpath.
	 * handles the HTTP path.
	 * @note path-related extensions should override this class.
	 */
	class NHTTP_API http_vpath : public http_extendable_extension {
	private:
		std::string base_path;

	public:
		http_vpath(std::string path);
		virtual ~http_vpath() { }

	private:
		virtual bool on_collect(std::shared_ptr<http_context> context) override;

		/* called before calling the on_handle method, test if it can be handled. */
		virtual bool on_enter(std::shared_ptr<http_context> context) override;

		/* called after calling the on_handle method, clean states if needed. */
		virtual void on_leave(std::shared_ptr<http_context> context) override;

	protected:
		/* called for handling a context. */
		virtual bool on_handle(std::shared_ptr<http_context> context, extended_t) override { return false; }
	};

	/* get current scoped vpath instance. */
	inline http_vpath* vpath_of(std::shared_ptr<http_context> context) {
		http_vpath_tag* tag = context->link->get_tag_ptr<http_vpath_tag>();

		if (tag && tag->vpaths.size())
			return tag->vpaths.top();

		return nullptr;
	}

	/* get current scoped sub-path. */
	inline const std::string& subpath_of(std::shared_ptr<http_context> context) {
		http_vpath_tag* tag = context->link->get_tag_ptr<http_vpath_tag>();

		if (tag && tag->subpaths.size())
			return tag->subpaths.top();

		return context->request->get_target().get_path();
	}
}
}