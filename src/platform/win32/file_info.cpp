#include "nhttp/platform/file_info.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nhttp::platform {

	namespace {
		/* combines a FILETIME's two 32-bit halves into a Windows "100ns ticks
		 * since 1601-01-01" value, then converts to a Unix std::time_t. */
		std::time_t filetime_to_time_t(const FILETIME& ft) noexcept {
			ULARGE_INTEGER u;
			u.LowPart = ft.dwLowDateTime;
			u.HighPart = ft.dwHighDateTime;

			constexpr std::uint64_t epoch_diff_100ns = 116444736000000000ULL; // 1601->1970
			const std::uint64_t ticks = u.QuadPart > epoch_diff_100ns ? u.QuadPart - epoch_diff_100ns : 0;
			return static_cast<std::time_t>(ticks / 10000000ULL);
		}
	}

	file_info stat_file(const std::string& path) noexcept {
		WIN32_FILE_ATTRIBUTE_DATA data{};

		if (!::GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
			return file_info{};

		file_info info;

		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			info.kind = file_kind::directory;
		}
		else {
			info.kind = file_kind::regular_file;
			info.size = (static_cast<std::int64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
			info.mtime = filetime_to_time_t(data.ftLastWriteTime);
		}

		return info;
	}

}
