#include "nhttp/version.hpp"

namespace nhttp {

	version library_version() noexcept {
		return version{
			NHTTP_VERSION_MAJOR,
			NHTTP_VERSION_MINOR,
			NHTTP_VERSION_PATCH
		};
	}

	std::string_view library_version_string() noexcept {
		return NHTTP_VERSION_STRING;
	}

}
