#pragma once

#include <cstdint>
#include <string_view>

namespace nhttp {

	struct version {
		std::uint32_t major;
		std::uint32_t minor;
		std::uint32_t patch;
	};

	/* the nhttp library version, as configured by CMake at build time. */
	version library_version() noexcept;

	/* "MAJOR.MINOR.PATCH" formatted version string. */
	std::string_view library_version_string() noexcept;

}
