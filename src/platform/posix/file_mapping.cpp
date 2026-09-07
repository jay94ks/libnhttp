#include "nhttp/platform/file_mapping.hpp"

#include <algorithm>
#include <cstddef>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace nhttp::platform {

	namespace {

		std::int64_t page_size() noexcept {
			static const std::int64_t sz = static_cast<std::int64_t>(::sysconf(_SC_PAGESIZE));
			return sz;
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
		fd_(std::exchange(other.fd_, -1))
	{
	}

	file_mapping& file_mapping::operator=(file_mapping&& other) noexcept {
		if (this != &other) {
			reset();

			window_data_ = std::exchange(other.window_data_, nullptr);
			window_start_ = std::exchange(other.window_start_, 0);
			window_size_ = std::exchange(other.window_size_, 0);
			total_size_ = std::exchange(other.total_size_, 0);
			fd_ = std::exchange(other.fd_, -1);
		}

		return *this;
	}

	void file_mapping::reset() noexcept {
		if (window_data_)
			::munmap(window_data_, static_cast<std::size_t>(window_size_));

		if (fd_ >= 0)
			::close(fd_);

		window_data_ = nullptr;
		window_start_ = 0;
		window_size_ = 0;
		total_size_ = 0;
		fd_ = -1;
	}

	file_mapping file_mapping::open(const std::string& path) noexcept {
		file_mapping result;

		const int fd = ::open(path.c_str(), O_RDONLY);

		if (fd < 0)
			return result;

		struct stat st{};

		if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
			::close(fd);
			return result;
		}

		result.fd_ = fd;
		result.total_size_ = static_cast<std::int64_t>(st.st_size);
		return result;
	}

	bool file_mapping::remap(std::int64_t aligned_start, std::int64_t size) noexcept {
		void* addr = ::mmap(nullptr, static_cast<std::size_t>(size), PROT_READ, MAP_PRIVATE,
			fd_, static_cast<off_t>(aligned_start));

		if (addr == MAP_FAILED)
			return false;

		if (window_data_)
			::munmap(window_data_, static_cast<std::size_t>(window_size_));

		window_data_ = addr;
		window_start_ = aligned_start;
		window_size_ = size;
		return true;
	}

	const void* file_mapping::ensure_window(std::int64_t offset, std::int64_t length) noexcept {
		if (fd_ < 0 || offset < 0 || length < 0 || offset + length > total_size_)
			return nullptr;

		if (window_data_ && offset >= window_start_ && offset + length <= window_start_ + window_size_)
			return static_cast<const char*>(window_data_) + (offset - window_start_);

		const std::int64_t align = page_size();
		const std::int64_t aligned_start = (offset / align) * align;

		std::int64_t want_size = std::max<std::int64_t>(window_capacity, length + (offset - aligned_start));
		want_size = std::min<std::int64_t>(want_size, total_size_ - aligned_start);

		if (!remap(aligned_start, want_size))
			return nullptr;

		return static_cast<const char*>(window_data_) + (offset - window_start_);
	}

}
