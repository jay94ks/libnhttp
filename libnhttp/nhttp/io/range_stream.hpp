#pragma once
#include "stream.hpp"

namespace nhttp {

	class NHTTP_API range_stream : public stream {
	private:
		std::shared_ptr<stream> inner;
		ssize_t range_begin, range_offset, range_end;
		bool eos;

	public:
		range_stream(const std::shared_ptr<stream>& inner, ssize_t range_begin, ssize_t range_end)
			: inner(inner), range_begin(range_begin), range_offset(range_begin), range_end(range_end), eos(false)
		{
			ssize_t length = inner->get_length();

			/* specified stream doesn't support `seek`. */
			if (length < 0) {
				/* fall back to full response. */
				this->range_offset = this->range_begin = this->range_end = -1;
				return;
			}

			if (range_begin > length)
				range_begin = this->range_begin = length;

			if (range_end <= range_begin)
				range_end = this->range_end = range_begin;

			if (range_end > length)
				range_end = this->range_end = length;

			inner->seek(range_offset = range_begin, SEEK_SET);
			this->eos = range_begin == range_end;
		}


	public:
		/* determines validity of this stream. */
		virtual bool is_valid() const override { return inner && inner->is_valid(); }

		/* determines currently at end of stream. */
		virtual bool is_end_of() const override { return range_end >= 0 ? range_offset == range_end : eos; }

		/**
		 * get total length of this stream.
		 * @warn default-impl uses `tell()` and `seek()` to provide length!
		 * @returns:
		 *	>= 0: length.
		 *  <  0: not supported.
		 */
		virtual ssize_t get_length() const override { return range_end >= 0 ? range_end - range_begin : -1; }

		/**
		 * get current position of file cursor.
		 * @returns:
		 *	>= 0: position.
		 *  <  0: not supported.
		 */
		virtual ssize_t tell() const { return range_offset; }

		/**
		 * set current position of file cursor.
		 * @params: one of SEEK_SET, SEEK_CUR, SEEK_END
		 * @returns false if not supported.
		 */
		virtual bool seek(ssize_t off, int32_t orig) override {
			if (range_end < 0)
				return false;

			switch (orig) {
				case SEEK_SET: break;
				case SEEK_CUR: off += range_offset; break;
				case SEEK_END: off += range_end; break;
			}

			if (off < range_begin)
				off = range_begin;

			if (off > range_end)
				off = range_end;

			range_offset = off;
			inner->seek(range_offset, SEEK_SET);
			return true;
		}

		/**
		 * read bytes from stream.
		 * @returns:
		 *	> 0: read size.
		 *  = 0: end of stream.
		 *  < 0: not supported.
		 */
		virtual int32_t read(void* buf, size_t len) override {
			if (!inner)
				return 0;

			int32_t bytes;
			if (range_end < 0) {
				eos = (bytes = inner->read(buf, len)) <= 0;
				return bytes;
			}

			size_t avail = range_end - range_offset;
			len = len > avail ? avail : len;

			if (!len)
				return 0;

			if ((bytes = inner->read(buf, len)) <= 0) {
				eos = true;
				return 0;
			}

			range_offset += bytes;
			return bytes;
		}

		/**
		 * wrtie bytes into stream.
		 * @returns:
		 *	> 0: read size.
		 *  = 0: end of stream.
		 *  < 0: not supported.
		 */
		virtual int32_t write(const void* buf, size_t len) override {
			if (!inner)
				return 0;

			int32_t bytes;
			if (range_end < 0) {
				eos = (bytes = inner->write(buf, len)) <= 0;
				return bytes;
			}

			size_t avail = range_end - range_offset;
			len = len > avail ? avail : len;

			if (!len)
				return 0;

			if ((bytes = inner->write(buf, len)) <= 0) {
				eos = true;
				return 0;
			}

			range_offset += bytes;
			return bytes;
		}

		/* flush internal buffers. */
		virtual void flush() { 
			if (inner)
				inner->flush();
		}

		/* close stream. */
		virtual void close() {
			if (inner) {
				inner->close();
				inner = nullptr;
			}
		}
	};
}