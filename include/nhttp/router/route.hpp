#pragma once

#include "facade.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nhttp::router {

	enum class route_kind { root, static_segment, param, wildcard };

	/**
	 * flat map for route captures. a real request has at most a handful of
	 * ":name" captures, and route_match() copies a route_state on every trie
	 * candidate it tries (most of which get discarded on backtrack — see
	 * CONCEPTS.md §5 and PLAN.md's router item) — a std::map<string,string>
	 * paid for a fresh set of red-black-tree node allocations on every one of
	 * those copies, confirmed as a real hotspot via `perf` profiling
	 * (benchmark/router/bench_router_main.cpp). A linearly-scanned
	 * std::vector<pair<string,string>> is both cheaper to copy (one
	 * allocation, not one per entry) and faster to scan at this size than a
	 * tree lookup.
	 */
	class capture_map {
	public:
		using value_type = std::pair<std::string, std::string>;
		using const_iterator = std::vector<value_type>::const_iterator;

		std::string& operator[](std::string_view key) {
			for (value_type& kv : items_) {
				if (kv.first == key)
					return kv.second;
			}

			items_.emplace_back(std::string(key), std::string());
			return items_.back().second;
		}

		const std::string& at(std::string_view key) const {
			for (const value_type& kv : items_) {
				if (kv.first == key)
					return kv.second;
			}

			throw std::out_of_range("capture_map::at: no such key");
		}

		std::size_t count(std::string_view key) const noexcept {
			for (const value_type& kv : items_) {
				if (kv.first == key)
					return 1;
			}

			return 0;
		}

		bool empty() const noexcept { return items_.empty(); }
		std::size_t size() const noexcept { return items_.size(); }
		const_iterator begin() const noexcept { return items_.begin(); }
		const_iterator end() const noexcept { return items_.end(); }

	private:
		std::vector<value_type> items_;
	};

	/**
	 * per-match state threaded through route::route_match: captured ":name"
	 * values and how many trie levels were descended to reach the match —
	 * `depth` is what breaks ties between multiple simultaneously-matching
	 * parameter branches (see route.cpp's route_match for the exact rule this
	 * encodes, and CONCEPTS.md §5 for why it matters).
	 */
	struct route_state {
		capture_map captures;
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
		std::shared_ptr<facade> param(const std::string& name, std::function<bool(std::string_view)> predicate) override;
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
		std::function<bool(std::string_view)> predicate_; // param nodes only

		std::vector<route_ptr> static_children_; // sorted by name for binary search
		std::vector<route_ptr> param_children_;
		route_ptr wildcard_child_;

		std::map<std::string, target_ptr> method_targets_; // key = method name, or "*" for any()
		middleware_stack middlewares_;
	};

}
