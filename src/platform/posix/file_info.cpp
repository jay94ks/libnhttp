#include "nhttp/platform/file_info.hpp"

#include <sys/stat.h>

namespace nhttp::platform {

	file_info stat_file(const std::string& path) noexcept {
		struct stat st{};

		if (::stat(path.c_str(), &st) != 0)
			return file_info{};

		file_info info;

		if (S_ISREG(st.st_mode)) {
			info.kind = file_kind::regular_file;
			info.size = static_cast<std::int64_t>(st.st_size);
			info.mtime = st.st_mtime;
		}
		else if (S_ISDIR(st.st_mode)) {
			info.kind = file_kind::directory;
		}
		else {
			info.kind = file_kind::other;
		}

		return info;
	}

}
