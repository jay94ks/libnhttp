#include "nhttp/platform/file_mapping.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <utility>

namespace nhttp::platform {

	namespace {

		/* MapViewOfFile's offset must be a multiple of this — typically
		 * 64 KiB, not the 4 KiB page size (unlike POSIX mmap's page-size
		 * alignment requirement). */
		std::int64_t allocation_granularity() noexcept {
			static const std::int64_t granularity = [] {
				SYSTEM_INFO info{};
				::GetSystemInfo(&info);
				return static_cast<std::int64_t>(info.dwAllocationGranularity);
			}();

			return granularity;
		}

	}

	file_mapping::~file_mapping() {
		reset();
	}

	file_mapping::file_mapping(file_mapping&& other) noexcept
		: window_data_(std::exchange(other.window_data_, nullptr)),
		window_start_(std::exchange(other.window_start_, 0)),
		window_size_(std::exchange(other.window_size_, 0)),
		total_size_(std::exchange(other.total_size_, 0)),
		file_handle_(std::exchange(other.file_handle_, nullptr)),
		mapping_handle_(std::exchange(other.mapping_handle_, nullptr))
	{
	}

	file_mapping& file_mapping::operator=(file_mapping&& other) noexcept {
		if (this != &other) {
			reset();

			window_data_ = std::exchange(other.window_data_, nullptr);
			window_start_ = std::exchange(other.window_start_, 0);
			window_size_ = std::exchange(other.window_size_, 0);
			total_size_ = std::exchange(other.total_size_, 0);
			file_handle_ = std::exchange(other.file_handle_, nullptr);
			mapping_handle_ = std::exchange(other.mapping_handle_, nullptr);
		}

		return *this;
	}

	void file_mapping::reset() noexcept {
		if (window_data_)
			::UnmapViewOfFile(window_data_);

		if (mapping_handle_)
			::CloseHandle(static_cast<HANDLE>(mapping_handle_));

		if (file_handle_ && file_handle_ != INVALID_HANDLE_VALUE)
			::CloseHandle(static_cast<HANDLE>(file_handle_));

		window_data_ = nullptr;
		window_start_ = 0;
		window_size_ = 0;
		total_size_ = 0;
		file_handle_ = nullptr;
		mapping_handle_ = nullptr;
	}

	file_mapping file_mapping::open(const std::string& path) noexcept {
		file_mapping result;

		const HANDLE file = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (file == INVALID_HANDLE_VALUE)
			return result;

		LARGE_INTEGER size{};

		if (!::GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
			::CloseHandle(file);
			return result;
		}

		// covers the whole file conceptually but doesn't commit memory —
		// MapViewOfFile below is what actually maps a (bounded) window.
		const HANDLE mapping = ::CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

		if (!mapping) {
			::CloseHandle(file);
			return result;
		}

		result.file_handle_ = file;
		result.mapping_handle_ = mapping;
		result.total_size_ = static_cast<std::int64_t>(size.QuadPart);
		return result;
	}

	bool file_mapping::remap(std::int64_t aligned_start, std::int64_t size) noexcept {
		ULARGE_INTEGER offset;
		offset.QuadPart = static_cast<ULONGLONG>(aligned_start);

		void* addr = ::MapViewOfFile(static_cast<HANDLE>(mapping_handle_), FILE_MAP_READ,
			offset.HighPart, offset.LowPart, static_cast<SIZE_T>(size));

		if (!addr)
			return false;

		if (window_data_)
			::UnmapViewOfFile(window_data_);

		window_data_ = addr;
		window_start_ = aligned_start;
		window_size_ = size;
		return true;
	}

	const void* file_mapping::ensure_window(std::int64_t offset, std::int64_t length) noexcept {
		if (!mapping_handle_ || offset < 0 || length < 0 || offset + length > total_size_)
			return nullptr;

		if (window_data_ && offset >= window_start_ && offset + length <= window_start_ + window_size_)
			return static_cast<const char*>(window_data_) + (offset - window_start_);

		const std::int64_t align = allocation_granularity();
		const std::int64_t aligned_start = (offset / align) * align;

		std::int64_t want_size = std::max<std::int64_t>(window_capacity, length + (offset - aligned_start));
		want_size = std::min<std::int64_t>(want_size, total_size_ - aligned_start);

		if (!remap(aligned_start, want_size))
			return nullptr;

		return static_cast<const char*>(window_data_) + (offset - window_start_);
	}

}
