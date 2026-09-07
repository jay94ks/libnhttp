#pragma once

#include "facade.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nhttp::router {

	enum class route_kind { root, static_segment, param, wildcard };

	/**
	 * per-match state threaded through route::route_match: captured ":name"
	 * values and how many trie levels were descended to reach the match —
	 * `depth` is what breaks ties between multiple simultaneously-matching
	 * parameter branches (see route.cpp's route_match for the exact rule this
	 * encodes, and CONCEPTS.md §5 for why it matters).
	 */
	struct route_state {
		std::map<std::string, std::string> captures;
		int depth = 0;
	};

	class route;
	using route_ptr = std::shared_ptr<route>;

	/**
	 * class route.
	 * one node of the path trie: static (exact segment), param (":name",
	 * optionally predicate-constrained), wildcard (catch-all remainder, no
	 * children of its own), or root (the synthetic top node).
	 */
	class route final : public facade, public std::enable_shared_from_this<route> {
	public:
		explicit route(route_kind kind, std::string name = std::string());

	public:
		route_kind kind() const noexcept { return kind_; }
		const std::string& name() const noexcept { return name_; }

		/* resolves (creating intermediate nodes as needed) the descendant for a
		 * '/'-separated path relative to this node. node kind per segment is
		 * inferred from its first character: "" or "*" -> wildcard, ":..." ->
		 * param, anything else -> static. */
		route_ptr child(std::string_view path);

		/* attempts to match `path` starting at this node. on success returns the
		 * matched leaf and fills `state`; on failure returns nullptr and leaves
		 * `state` as it was before the call (callers backtrack by copying). */
		route_ptr route_match(route_state& state, std::string_view path);

	public:
		target_ptr get_target(const protocol::http_method& m) const;
		bool has_any_target() const noexcept { return !method_targets_.empty(); }
		middleware_stack& middlewares() noexcept { return middlewares_; }

	public:
		// -- facade --
		std::shared_ptr<facade> any(target_ptr t) override;
		std::shared_ptr<facade> any(const std::string& path, target_ptr t) override;
		std::shared_ptr<facade> method(const protocol::http_method& m, target_ptr t) override;
		std::shared_ptr<facade> method(const protocol::http_method& m, const std::string& path, target_ptr t) override;
		std::shared_ptr<facade> param(const std::string& name, std::function<bool(const std::string&)> predicate) override;
		std::shared_ptr<facade> prepend(middleware_ptr m) override;
		std::shared_ptr<facade> append(middleware_ptr m) override;
		std::shared_ptr<facade> group(std::function<void(std::shared_ptr<facade>)> body) override;
		std::shared_ptr<facade> resolve(const std::string& path) override;

	private:
		route_ptr find_static_child(std::string_view name) const;
		route_ptr find_or_create_static_child(std::string_view name);
		route_ptr find_or_create_param_child(std::string_view name);
		route_ptr find_or_create_wildcard_child();

	private:
		route_kind kind_;
		std::string name_; // for param/wildcard nodes, includes the leading ':' or is "*"
		std::function<bool(const std::string&)> predicate_; // param nodes only

		std::vector<route_ptr> static_children_; // sorted by name for binary search
		std::vector<route_ptr> param_children_;
		route_ptr wildcard_child_;

		std::map<std::string, target_ptr> method_targets_; // key = method name, or "*" for any()
		middleware_stack middlewares_;
	};

}
