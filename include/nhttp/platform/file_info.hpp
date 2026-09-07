#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace nhttp::platform {

	enum class file_kind { not_found, regular_file, directory, other };

	/* the only facts static_content.cpp/overlay/single_file actually need about
	 * a path on disk — kept portable (no <sys/stat.h>/struct stat in a public
	 * header) so overlay.hpp/single_file.hpp compile unchanged on Windows.
	 * size/mtime are only meaningful when kind == regular_file. */
	struct file_info {
		file_kind kind = file_kind::not_found;
		std::int64_t size = 0;
		std::time_t mtime = 0;
	};

	file_info stat_file(const std::string& path) noexcept;

}
