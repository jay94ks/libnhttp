#include <catch2/catch_test_macros.hpp>

#include "nhttp/version.hpp"

TEST_CASE("library_version matches the string form", "[version]") {
	const nhttp::version v = nhttp::library_version();
	const std::string_view s = nhttp::library_version_string();

	const std::string expected =
		std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);

	REQUIRE(s == expected);
}
