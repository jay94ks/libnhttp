#include "xfwk_route.hpp"
#include "xfwk_router.hpp"

namespace nhttp {
namespace server {
namespace xfwk {

	xfwk_route_ptr xfwk_route::child(const std::string& path) {
		xfwk_path _path(path);
		xfwk_route_ptr _route = shared_from_this();

		while (_path.get_size()) {
			std::string name = _path.get_name_at(0);
			_path.pop_front();

			if (auto child = _route->get_child(name)) {
				_route = child;
				continue;
			}
			
			_route = _route->new_child(name);
		}

		return _route;
	}

	xfwk_route_ptr xfwk_route::get_child(const std::string& name) const {
		if (!name.size() || (name.size() == 1 && name[0] == '*'))
			return wildcard;

		ssize_t index = binary_search(children, name);

		if (index < 0)
			return nullptr;

		return children[index];
	}

	this_ptr<xfwk_route> xfwk_route::clear() {
		children.clear();
		params.clear();
		wildcard = nullptr;
		return this;
	}

	xfwk_route_ptr xfwk_route::new_child(const std::string& name) {
		xfwk_route_ptr new_route;

		if (!name.size() || (name.size() == 1 && name[0] == '*'))
			 new_route = std::make_shared<xfwk_wildcard_route>();

		else if (name[0] == ':')
			 new_route = std::make_shared<xfwk_param_route>(name);

		else new_route = std::make_shared<xfwk_static_route>(name);

		add_child(new_route);
		return new_route;
	}

	this_ptr<xfwk_route> xfwk_route::add_child(xfwk_route_ptr child) {
		//ssize_t index = binary_search(children, child->name);
		remove_child(child->name);

		/* insert route to collections. */
		ssize_t index = binary_search_insert(children, child->name);
		children.insert(children.begin() + index, child);

		/* root or wildcard. */
		if (child->is_wildcard() || child->is_root())
			wildcard = child;

		else if (child->is_param()) {
			index = binary_search_insert(params, child->name);
			params.insert(params.begin() + index, child);
		}

		return this;
	}

	std::shared_ptr<xfwk_static_route> xfwk_route::add_static(const std::string& name) {
		auto newbie = std::make_shared<xfwk_static_route>(name);

		remove_child(name);
		add_child(newbie);

		return newbie;
	}

	std::shared_ptr<xfwk_param_route> xfwk_route::add_param(const std::string& name, predicate_type predicate) {
		auto newbie = std::make_shared<xfwk_param_route>(name, std::move(predicate));

		remove_child(name);
		add_child(newbie);

		return newbie;
	}

	std::shared_ptr<xfwk_wildcard_route> xfwk_route::add_wildcard() {
		auto newbie = std::make_shared<xfwk_wildcard_route>();

		remove_child("*");
		add_child(newbie);

		return newbie;
	}

	this_ptr<xfwk_route> xfwk_route::remove_child(const std::string& name) {
		ssize_t index = binary_search(children, name);

		if (index >= 0) {
			xfwk_route_ptr old = children[index];
			children.erase(children.begin() + index);

			if (wildcard == old)
				wildcard = nullptr;

			if ((index = binary_search(params, old->name)) >= 0)
				params.erase(params.begin() + index);
		}

		return this;
	}

	std::shared_ptr<xfwk_param_route> xfwk_route::find_param(const std::string& name) const {
		for (const auto& each : params) {
			if (each->name == name)
				return std::dynamic_pointer_cast<xfwk_param_route>(each);
		}

		for (const auto& each : children) {
			if (auto found = each->find_param(name))
				return found;
		}

		return nullptr;
	}

	std::shared_ptr<xfwk_router> xfwk_route::as_router() {
		return std::make_shared<xfwk_router>(shared_from_this());
	}

	std::shared_ptr<xfwk_router> xfwk_route::as_router(std::string mapping_path) {
		return std::make_shared<xfwk_router>(
			std::move(mapping_path), shared_from_this()
		);
	}

	ssize_t xfwk_route::binary_search(const std::vector<xfwk_route_ptr>& children, const std::string& name) {
		size_t left = 0, right = (children.size() > 0 ? children.size() - 1 : 0);
		if (!children.size())	return -1;

		int32_t vL = strcmp_x(children.at(left)->name, name);
		if (vL == 0)			return left;
		else if (vL > 0)		return -1;		/* name < left:  out of range */

		int32_t vR = strcmp_x(children.at(right)->name, name);
		if (vR == 0)			return right;
		else if (vR < 0)		return -1;		/* right < name: out of range */

		while (left < right) {
			size_t mid = (left + right) >> 1;

			if (mid == left) {
				break;
			}

			int32_t vM = strcmp_x(children.at(mid)->name, name);
			if (vM == 0)
				return mid;

			else if (vM < 0) {				/* mid < name: scope to right */
				left = mid;
				vL = vM;
			}

			else if (vM > 0) {				/* name < mid: scope to left */
				right = mid;
				vR = vM;
			}
		}

		return -1;
	}

	ssize_t xfwk_route::binary_search_insert(const std::vector<xfwk_route_ptr>& children, const std::string& name) {
		size_t left = 0, right = (children.size() > 0 ? children.size() - 1 : 0);
		if (!children.size())	return 0;

		int32_t vL = strcmp_x(children.at(left)->name, name);
		if (vL == 0)			return -1;			/* left == name: already */
		else if (vL > 0)		return 0;			/* prepend. */

		int32_t vR = strcmp_x(children.at(left)->name, name);
		if (vR == 0)			return -1;			/* right == name: already */
		else if (vR < 0)		return right + 1;	/* push_back. */

		while (left < right) {
			size_t mid = (left + right) >> 1;
			int32_t vM = strcmp_x(children.at(left)->name, name);

			if (vM == 0)
				return -1;					/* mid == name: already */

			else if (vM < 0) {				/* mid < name: scope to right */
				if (left == mid)			/* no more. */
					return left + 1;

				left = mid;
				vL = vM;
			}

			else if (vM > 0) {				/* name < mid: scope to left */
				if (right == mid)			/* no more. */
					return right;

				right = mid;
				vR = vM;
			}
		}

		return children.size();
	}

	/* route the path to target. */

	bool xfwk_route::route(xfwk_route_state& state, xfwk_path& path) const {
		if (!path.get_size()) {
			state.route = std::const_pointer_cast<xfwk_route>(shared_from_this());
			state.path_remainder = path.get_subpath(0).get_path();
			return true;
		}

		/* if no children: */
		if (!children.size() && !wildcard)
			return false;

		/* clone for restoring state. */
		xfwk_path bkp_path = path;
		xfwk_route_state bkp_state = state;

		std::string name = path.get_name_at(0);
		auto child = get_child(name);

		/* increase depth. */
		++state.depth;

		if (child && child->is_static()) {
			/* pop front path. */
			path.pop_front();
			if (child->route(state, path)) {
				return true;
			}

			/* restore previous state. */
			path = bkp_path;
			state = bkp_state;
		}

		if (params.size()) {
			/* clone for choosing deep state. */
			xfwk_path deep_path = path;
			xfwk_route_state deep_state = state;

			bool choosen = false;
			--deep_state.depth;

			for (auto each : params) {
				if (each->test(name)) {
					if (!each->route(state, path)) {
						/* restore previous state. */
						path = bkp_path;
						state = bkp_state;
						continue;
					}

					/* if found more deep, replace it. */
					if (deep_state.depth < state.depth) {
						deep_path = path;
						deep_state = state;
					}

					choosen = true;
				}
			}

			if (choosen) {
				path = std::move(deep_path);
				state = std::move(deep_state);
				return true;
			}
		}

		/* if no wildcard can accept route, restore to original. */
		if (!wildcard || !wildcard->route(state, path)) {
			path = std::move(bkp_path);
			state = std::move(bkp_state);
			return false;
		}

		return true;
	}

	bool xfwk_param_route::route(xfwk_route_state& state, xfwk_path& path) const {
		if (!path.get_size())
			return false;

		auto i = state.captures.find(name);
		std::string org; bool was_set = false;
		xfwk_path bkp_path = path;

		if (i != state.captures.end()) {
			was_set = true;
			org = std::move(i->second);
		}

		state.captures.emplace(name, path.get_name_at(0));
		path.pop_front();

		if (!xfwk_route::route(state, path)) {
			if (!was_set) {
				i = state.captures.find(name);

				if (i != state.captures.end())
					state.captures.erase(i);

				path = std::move(bkp_path);
				return false;
			}

			state.captures.emplace(name, std::move(org));
			path = std::move(bkp_path);
			return false;
		}

		return true;
	}

	bool xfwk_wildcard_route::route(xfwk_route_state& state, xfwk_path& path) const {
		state.route = std::const_pointer_cast<xfwk_route>(shared_from_this());
		state.path_remainder = path.get_subpath(0).get_path();
		return true;
	}

}
}
}