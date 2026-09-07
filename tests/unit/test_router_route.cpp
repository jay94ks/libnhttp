#include <catch2/catch_test_macros.hpp>

#include "nhttp/router/route.hpp"

using namespace nhttp::router;

TEST_CASE("route matches a purely static path", "[router][route]") {
	auto root = std::make_shared<route>(route_kind::root);
	root->child("users/list");

	route_state state;
	auto matched = root->route_match(state, "users/list");

	REQUIRE(matched != nullptr);
	REQUIRE(matched->kind() == route_kind::static_segment);
	REQUIRE(matched->name() == "list");
}

TEST_CASE("route captures a parameter segment", "[router][route]") {
	auto root = std::make_shared<route>(route_kind::root);
	root->child("users/:id");

	route_state state;
	auto matched = root->route_match(state, "users/42");

	REQUIRE(matched != nullptr);
	REQUIRE(matched->kind() == route_kind::param);
	REQUIRE(state.captures.at(":id") == "42");
}

TEST_CASE("a static child always wins over a parameter child at the same segment", "[router][route]") {
	auto root = std::make_shared<route>(route_kind::root);
	root->child(":id");     // param branch
	root->child("whoami");  // static branch

	route_state state;
	auto matched = root->route_match(state, "whoami");

	REQUIRE(matched != nullptr);
	REQUIRE(matched->kind() == route_kind::static_segment);
	REQUIRE(state.captures.empty());
}

TEST_CASE("a wildcard only applies when neither static nor parameter children match", "[router][route]") {
	auto root = std::make_shared<route>(route_kind::root);
	root->child("known");
	root->child("*");

	route_state state1;
	auto known = root->route_match(state1, "known");
	REQUIRE(known->kind() == route_kind::static_segment);

	route_state state2;
	auto fallback = root->route_match(state2, "totally/unknown/path");
	REQUIRE(fallback != nullptr);
	REQUIRE(fallback->kind() == route_kind::wildcard);
	REQUIRE(state2.captures.at("*") == "totally/unknown/path");
}

TEST_CASE("a parameter branch that ultimately fails to match does not leak its capture", "[router][route]") {
	auto root = std::make_shared<route>(route_kind::root);
	root->child(":x/foo"); // only accepts a second segment literally "foo"
	root->child(":y/bar"); // only accepts a second segment literally "bar"

	route_state state;
	auto matched = root->route_match(state, "v/bar");

	REQUIRE(matched != nullptr);
	REQUIRE(state.captures.count(":y") == 1);
	REQUIRE(state.captures.at(":y") == "v");
	REQUIRE(state.captures.count(":x") == 0); // the failed :x/foo attempt must not leave a stale capture
}

namespace {

	/**
	 * builds the tree at the center of the historical routing-priority bug
	 * (CONCEPTS.md §5, fixed in the original by commit 61a8fa1): two parameter
	 * branches both accept the same first segment, but reach a full match at
	 * DIFFERENT depths — one via a shallow wildcard, one via two further
	 * static segments. the deeper (":y"/b/c) match must always win, regardless
	 * of which parameter child was registered (and therefore tried) first —
	 * a naive "first match wins" or "last match wins" rule gets this wrong.
	 */
	route_ptr build_priority_tree(bool register_x_first) {
		auto root = std::make_shared<route>(route_kind::root);
		auto a = root->child("a");

		auto add_x = [&] { a->child(":x")->child("*"); };
		auto add_y = [&] { a->child(":y/b/c"); };

		if (register_x_first) {
			add_x();
			add_y();
		}
		else {
			add_y();
			add_x();
		}

		return root;
	}

}

TEST_CASE("routing picks the deepest matching parameter branch, not the first one tried", "[router][route][regression]") {
	SECTION("parameter child :x registered before :y") {
		auto root = build_priority_tree(true);
		route_state state;
		auto matched = root->route_match(state, "a/v/b/c");

		REQUIRE(matched != nullptr);
		REQUIRE(matched->kind() == route_kind::static_segment);
		REQUIRE(matched->name() == "c");
		REQUIRE(state.captures.at(":y") == "v");
		REQUIRE(state.captures.count(":x") == 0);
	}

	SECTION("parameter child :y registered before :x") {
		auto root = build_priority_tree(false);
		route_state state;
		auto matched = root->route_match(state, "a/v/b/c");

		REQUIRE(matched != nullptr);
		REQUIRE(matched->kind() == route_kind::static_segment);
		REQUIRE(matched->name() == "c");
		REQUIRE(state.captures.at(":y") == "v");
		REQUIRE(state.captures.count(":x") == 0);
	}
}

TEST_CASE("a parameter predicate can reject a segment, falling through to another branch", "[router][route]") {
	auto root = std::make_shared<route>(route_kind::root);
	root->param(":user", [](std::string_view v) { return v == "jay" || v == "kay"; });
	root->child("other"); // sibling static branch

	route_state accepted;
	auto ok = root->route_match(accepted, "jay");
	REQUIRE(ok != nullptr);
	REQUIRE(ok->kind() == route_kind::param);

	route_state rejected;
	auto not_ok = root->route_match(rejected, "someone-else");
	REQUIRE(not_ok == nullptr);
}
