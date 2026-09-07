#include "nhttp/router/route.hpp"
#include "nhttp/router/group_proxy.hpp"

#include <algorithm>
#include <array>

namespace nhttp::router {

	route::route(route_kind kind, std::string name)
		: kind_(kind), name_(std::move(name))
	{
	}

	route_ptr route::find_static_child(std::string_view name) const {
		const auto it = std::lower_bound(static_children_.begin(), static_children_.end(), name,
			[](const route_ptr& r, std::string_view n) { return r->name_ < n; });

		if (it != static_children_.end() && (*it)->name_ == name)
			return *it;

		return nullptr;
	}

	route_ptr route::find_or_create_static_child(std::string_view name) {
		const auto it = std::lower_bound(static_children_.begin(), static_children_.end(), name,
			[](const route_ptr& r, std::string_view n) { return r->name_ < n; });

		if (it != static_children_.end() && (*it)->name_ == name)
			return *it;

		route_ptr created = std::make_shared<route>(route_kind::static_segment, std::string(name));
		static_children_.insert(it, created);
		return created;
	}

	route_ptr route::find_or_create_param_child(std::string_view name) {
		for (const route_ptr& p : param_children_) {
			if (p->name_ == name)
				return p;
		}

		route_ptr created = std::make_shared<route>(route_kind::param, std::string(name));
		param_children_.push_back(created);
		return created;
	}

	route_ptr route::find_or_create_wildcard_child() {
		if (!wildcard_child_)
			wildcard_child_ = std::make_shared<route>(route_kind::wildcard, std::string("*"));

		return wildcard_child_;
	}

	route_ptr route::child(std::string_view path) {
		route_ptr current = shared_from_this();

		if (!path.empty() && path.front() == '/')
			path.remove_prefix(1);

		while (!path.empty()) {
			const std::size_t slash = path.find('/');
			const std::string_view seg = path.substr(0, slash);

			if (seg.empty() || seg == "*") {
				current = current->find_or_create_wildcard_child();
				break; // a wildcard swallows everything; no segment after it is meaningful.
			}

			if (seg.front() == ':')
				current = current->find_or_create_param_child(seg);
			else
				current = current->find_or_create_static_child(seg);

			if (slash == std::string_view::npos)
				break;

			path = path.substr(slash + 1);
		}

		return current;
	}

	route_ptr route::route_match(route_state& state, std::string_view path) {
		if (!path.empty() && path.front() == '/')
			path.remove_prefix(1);

		if (path.empty())
			return shared_from_this();

		const std::size_t slash = path.find('/');
		const std::string_view segment = path.substr(0, slash);
		const std::string_view rest = (slash == std::string_view::npos) ? std::string_view() : path.substr(slash + 1);

		++state.depth;

		// 1. an exact static child always takes priority over any parameter match.
		if (const route_ptr static_child = find_static_child(segment)) {
			route_state trial = state;

			if (const route_ptr matched = static_child->route_match(trial, rest)) {
				state = trial;
				return matched;
			}
		}

		// 2. among parameter children whose predicate accepts this segment, the
		// one whose subtree reaches the DEEPEST successful match wins — not
		// simply the first one tried. this is the exact tie-break a real,
		// shipped bug got wrong historically (see CONCEPTS.md §5): a naive
		// "first match wins" lets a shallower parameter branch incorrectly
		// displace a deeper, more specific one. `have_best` starting false
		// guarantees the first successful candidate is always recorded as the
		// baseline, so only a genuinely deeper later candidate ever replaces it.
		route_ptr best_match;
		route_state best_state;
		bool have_best = false;

		for (const route_ptr& p : param_children_) {
			if (p->predicate_ && !p->predicate_(segment))
				continue;

			route_state trial = state;
			trial.captures[p->name_] = std::string(segment);

			if (const route_ptr matched = p->route_match(trial, rest)) {
				if (!have_best || trial.depth > best_state.depth) {
					best_match = matched;
					best_state = trial;
					have_best = true;
				}
			}
		}

		if (have_best) {
			state = best_state;
			return best_match;
		}

		// 3. only if neither a static nor any parameter branch reached a full
		// match does the wildcard catch-all apply, swallowing everything left.
		if (wildcard_child_) {
			state.captures[wildcard_child_->name_] = std::string(path);
			return wildcard_child_;
		}

		return nullptr;
	}

	target_ptr route::get_target(const protocol::http_method& m) const {
		const auto it = method_targets_.find(m.name());
		return it == method_targets_.end() ? nullptr : it->second;
	}

	std::shared_ptr<facade> route::any(target_ptr t) {
		using method_fn = const protocol::http_method& (*)();

		static const std::array<method_fn, 7> all_methods{
			&protocol::http_method::GET, &protocol::http_method::HEAD, &protocol::http_method::POST,
			&protocol::http_method::PUT, &protocol::http_method::DELETE, &protocol::http_method::PATCH,
			&protocol::http_method::OPTIONS
		};

		for (const method_fn m : all_methods)
			method_targets_[m().name()] = t;

		return shared_from_this();
	}

	std::shared_ptr<facade> route::any(const std::string& path, target_ptr t) {
		child(path)->any(std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> route::method(const protocol::http_method& m, target_ptr t) {
		method_targets_[m.name()] = std::move(t);
		return shared_from_this();
	}

	std::shared_ptr<facade> route::method(const protocol::http_method& m, const std::string& path, target_ptr t) {
		child(path)->method(m, std::move(t));
		return shared_from_this();
	}

	std::shared_ptr<facade> route::param(const std::string& name, std::function<bool(std::string_view)> predicate) {
		const route_ptr node = child(name);
		node->predicate_ = std::move(predicate);
		return shared_from_this();
	}

	std::shared_ptr<facade> route::prepend(middleware_ptr m) {
		middlewares_.prepend(std::move(m));
		return shared_from_this();
	}

	std::shared_ptr<facade> route::append(middleware_ptr m) {
		middlewares_.append(std::move(m));
		return shared_from_this();
	}

	std::shared_ptr<facade> route::group(std::function<void(std::shared_ptr<facade>)> body) {
		auto proxy = std::make_shared<group_proxy>(shared_from_this());
		body(proxy);
		return proxy;
	}

	std::shared_ptr<facade> route::resolve(const std::string& path) {
		return child(path);
	}

}
