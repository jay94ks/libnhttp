#pragma once

#include "stream.hpp"

#include <cstdint>
#include <vector>

namespace nhttp::io {

	/**
	 * class memory_stream.
	 * an in-memory, always-ready byte buffer stream. reads/writes never
	 * genuinely suspend; the coroutine interface is honored uniformly anyway
	 * so callers never need to special-case the backing store.
	 */
	class memory_stream final : public stream {
	public:
		memory_stream() = default;
		explicit memory_stream(std::vector<std::uint8_t> initial);

	public:
		std::int64_t get_length() const override { return static_cast<std::int64_t>(data_.size()); }
		bool can_seek() const noexcept override { return true; }

		async::task<std::int64_t> seek(std::int64_t offset, seek_origin origin) override;
		async::task<std::size_t> read(void* buf, std::size_t n) override;
		async::task<std::size_t> write(const void* buf, std::size_t n) override;
		async::task<void> flush() override;
		async::task<void> close() override;

	public:
		const std::vector<std::uint8_t>& data() const noexcept { return data_; }

	private:
		std::vector<std::uint8_t> data_;
		std::size_t position_ = 0;
	};

}
