#include "nhttp/io/memory_stream.hpp"

#include <algorithm>
#include <cstring>

namespace nhttp::io {

	memory_stream::memory_stream(std::vector<std::uint8_t> initial)
		: data_(std::move(initial))
	{
	}

	async::task<std::int64_t> memory_stream::seek(std::int64_t offset, seek_origin origin) {
		std::int64_t target = 0;

		switch (origin) {
		case seek_origin::begin:
			target = offset;
			break;
		case seek_origin::current:
			target = static_cast<std::int64_t>(position_) + offset;
			break;
		case seek_origin::end:
			target = static_cast<std::int64_t>(data_.size()) + offset;
			break;
		}

		if (target < 0 || target > static_cast<std::int64_t>(data_.size()))
			co_return -1;

		position_ = static_cast<std::size_t>(target);
		co_return target;
	}

	async::task<std::size_t> memory_stream::read(void* buf, std::size_t n) {
		const std::size_t remaining = data_.size() - position_;
		const std::size_t to_copy = std::min(n, remaining);

		if (to_copy > 0) {
			std::memcpy(buf, data_.data() + position_, to_copy);
			position_ += to_copy;
		}

		co_return to_copy;
	}

	async::task<std::size_t> memory_stream::write(const void* buf, std::size_t n) {
		if (position_ + n > data_.size())
			data_.resize(position_ + n);

		std::memcpy(data_.data() + position_, buf, n);
		position_ += n;

		co_return n;
	}

	async::task<void> memory_stream::flush() {
		co_return;
	}

	async::task<void> memory_stream::close() {
		co_return;
	}

}
