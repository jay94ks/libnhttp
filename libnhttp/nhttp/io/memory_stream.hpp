#pragma once
#include "stream.hpp"

namespace nhttp {

	class NHTTP_API memory_stream : public stream {
	private:
		std::vector<uint8_t> mem;
		bool eof;
		size_t cursor = 0;

	public:
		memory_stream() : eof(true) { }
		memory_stream(const std::vector<uint8_t>& mem)
			: mem(mem) { eof = mem.size() <= 0; }

		memory_stream(std::vector<uint8_t>&& mem)
			: mem(std::move(mem))
		{
			eof = this->mem.size() <= 0;
		}

	public:
		/* determines validity of this stream. */
		virtual bool is_valid() const override { return mem.size() > 0; }

		/* determines this stream is based on non-blocking or not. */
		virtual bool is_nonblock() const { return true; }

		/**
		 * set non-blocking.
		 * @returns:
		 *	= true : supported and set.
		 *  = false: not supported.
		 */
		virtual bool set_nonblock(bool value) { return value; }

		/**
		 * get total length of this stream.
		 * @returns:
		 *	>= 0: length.
		 *  <  0: not supported.
		 */
		virtual ssize_t get_length() const override { return ssize_t(mem.size()); }

		/* determines currently at end of stream. */
		virtual bool is_end_of() const override { return eof; }

		/**
		 * get current position of file cursor.
		 * @returns:
		 *	>= 0: position.
		 *  <  0: not supported.
		 */
		virtual ssize_t tell() const override { return ssize_t(cursor); }

		/**
		 * set current position of file cursor.
		 * @params: one of SEEK_SET, SEEK_CUR, SEEK_END
		 * @returns false if not supported.
		 */
		virtual bool seek(ssize_t off, int32_t orig) override {
			if (mem.size()) {
				switch (off) {
				case SEEK_SET: cursor = off; break;
				case SEEK_CUR: cursor += off; break;
				case SEEK_END: cursor = mem.size() + off; break;
				}

				cursor = cursor > mem.size() ?
						 mem.size() : cursor;

				eof = cursor == mem.size();
				return true;
			}

			eof = true;
			cursor = 0;
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
			if (len > 0x7fffffffu) {
				len = 0x7fffffffu;
			}

			if (mem.size() > cursor) {
				size_t avail = mem.size() - cursor;
				len = len > avail ? avail : len;

				if (len) {
					memcpy(buf, &mem[0] + cursor, len);
					cursor += len;

					return int32_t(len);
				}
			}

			return 0;
		}

		/**
		 * wrtie bytes into stream.
		 * @returns:
		 *	> 0: written size.
		 *  = 0: end of stream.
		 *  < 0: not supported.
		 */
		virtual int32_t write(const void* buf, size_t len) override {
			if (len > 0x7fffffffu) {
				len = 0x7fffffffu;
			}

			if (mem.size() > cursor) {
				size_t overlaps = mem.size() - cursor;

				if (overlaps > len) {
					memcpy(&mem[0] + cursor, buf, len);
					cursor += len;
					return int32_t(len);
				}
			}

			/* append buffer. */
			mem.resize(cursor + len, 0);
			memcpy(&mem[0] + cursor, buf, len);
			cursor += len;

			return int32_t(len);
		}

		virtual void close() {
			eof = true;
			mem.clear();
			cursor = 0;
		}
	};

}