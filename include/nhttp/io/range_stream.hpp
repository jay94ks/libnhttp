#pragma once

#include "stream.hpp"

#include <memory>

namespace nhttp::io {

	/**
	 * class range_stream.
	 * a decorator presenting a [begin, end) byte window of an inner stream as
	 * its own, independently-positioned stream. this is the general-purpose
	 * "sub-view of a stream" primitive (used for HTTP Range requests, but not
	 * specific to them) — see CONCEPTS.md's protocol-layer conventions.
	 *
	 * if begin/end are negative, this falls back to an unbounded pass-through
	 * over the entire inner stream. otherwise the requested [begin, end) is
	 * honored exactly, clamped against the inner stream's own length only when
	 * that length is actually known — so this also works to impose an exact
	 * byte-count limit (e.g. an HTTP Content-Length body) over an inner stream
	 * whose length can't be known up front, such as a live connection.
	 */
	class range_stream final : public stream {
	public:
		range_stream(std::shared_ptr<stream> inner, std::int64_t begin, std::int64_t end);

	public:
		std::int64_t get_length() const override;
		bool can_seek() const noexcept override { return inner_->can_seek(); }

		async::task<std::int64_t> seek(std::int64_t offset, seek_origin origin) override;
		async::task<std::size_t> read(void* buf, std::size_t n) override;
		async::task<std::size_t> write(const void* buf, std::size_t n) override;
		async::task<void> flush() override { return inner_->flush(); }
		async::task<void> close() override { return inner_->close(); }

	private:
		async::task<void> ensure_positioned();

		std::shared_ptr<stream> inner_;
		bool bounded_ = false;
		std::int64_t begin_ = 0;
		std::int64_t end_ = 0; // exclusive; only meaningful when bounded_
		std::int64_t position_ = 0; // 0-based, within this range's own coordinate space
		bool positioned_ = false;
	};

}
