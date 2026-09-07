#include "nhttp/io/range_stream.hpp"

#include <algorithm>

namespace nhttp::io {

	range_stream::range_stream(std::shared_ptr<stream> inner, std::int64_t begin, std::int64_t end)
		: inner_(std::move(inner))
	{
		// bounded_ reflects the CALLER's intent (did they pass a real [begin,end)?),
		// not whether the inner stream happens to know its own length. those are
		// different questions: a range_stream is also used to impose an exact
		// byte-count limit (e.g. an HTTP Content-Length body) over an inner
		// stream whose own length is genuinely unknowable (a live connection) —
		// clamping against inner_->get_length() only makes sense when that
		// length is actually known, and must never silently discard a
		// caller-specified bound just because it isn't.
		if (begin < 0 || end < 0) {
			bounded_ = false;
			return;
		}

		const std::int64_t length = inner_->get_length();

		begin_ = (length >= 0) ? std::min(begin, length) : begin;
		end_ = (length >= 0) ? std::min(std::max(end, begin_), length) : std::max(end, begin_);
		bounded_ = true;
	}

	std::int64_t range_stream::get_length() const {
		return bounded_ ? (end_ - begin_) : inner_->get_length();
	}

	async::task<void> range_stream::ensure_positioned() {
		if (!positioned_) {
			// note: if the inner stream isn't seekable (e.g. a live connection,
			// can_seek()==false) this seek() is a no-op that returns -1, and we
			// deliberately ignore that — it's only correct because every current
			// caller of a non-seekable inner stream uses begin_==0 (the stream is
			// already positioned at its own start). a nonzero begin_ over a
			// non-seekable inner stream would silently read from the wrong offset.
			if (bounded_)
				co_await inner_->seek(begin_, seek_origin::begin);

			positioned_ = true;
		}
	}

	async::task<std::int64_t> range_stream::seek(std::int64_t offset, seek_origin origin) {
		std::int64_t target = 0;

		switch (origin) {
		case seek_origin::begin:
			target = offset;
			break;
		case seek_origin::current:
			target = position_ + offset;
			break;
		case seek_origin::end:
			target = (bounded_ ? (end_ - begin_) : inner_->get_length()) + offset;
			break;
		}

		if (target < 0)
			co_return -1;

		const std::int64_t inner_target = bounded_ ? (begin_ + target) : target;
		const std::int64_t result = co_await inner_->seek(inner_target, seek_origin::begin);

		if (result < 0)
			co_return -1;

		position_ = bounded_ ? (result - begin_) : result;
		positioned_ = true;
		co_return position_;
	}

	async::task<std::size_t> range_stream::read(void* buf, std::size_t n) {
		co_await ensure_positioned();

		if (bounded_) {
			const std::int64_t remaining = (end_ - begin_) - position_;

			if (remaining <= 0)
				co_return 0;

			if (static_cast<std::int64_t>(n) > remaining)
				n = static_cast<std::size_t>(remaining);
		}

		const std::size_t got = co_await inner_->read(buf, n);
		position_ += static_cast<std::int64_t>(got);
		co_return got;
	}

	async::task<std::size_t> range_stream::write(const void* buf, std::size_t n) {
		co_await ensure_positioned();

		if (bounded_) {
			const std::int64_t remaining = (end_ - begin_) - position_;

			if (remaining <= 0)
				co_return 0;

			if (static_cast<std::int64_t>(n) > remaining)
				n = static_cast<std::size_t>(remaining);
		}

		const std::size_t written = co_await inner_->write(buf, n);
		position_ += static_cast<std::int64_t>(written);
		co_return written;
	}

}
