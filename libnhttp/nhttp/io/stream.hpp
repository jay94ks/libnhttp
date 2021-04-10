#pragma once
#include "../types.hpp"

namespace nhttp {

	/* abstraction of data-stream. */
	class NHTTP_API stream {
	private:
		mutable int32_t err = 0;

	public:
		virtual ~stream() { }

	protected:
		inline void set_errno(int32_t err) { this->err = err; }
		inline void set_errno_c(int32_t err) const { this->err = err; }

	public:
		/* determines validity of this stream. */
		virtual bool is_valid() const = 0;

		/* determines currently at end of stream. */
		virtual bool is_end_of() const = 0;

		/* determines this stream is based on non-blocking or not. */
		virtual bool is_nonblock() const { return false; }

		/* get error no. */
		inline int32_t get_errno() const { return err;}

		/**
		 * set non-blocking.
		 * @returns:
		 *	= true : supported and set.
		 *  = false: not supported.
		 */
		virtual bool set_nonblock(bool value) { return false; }

		/* determines this stream can be read immediately or not. */
		virtual bool can_read() const { return true; }

		/* determines this stream can be written immediately or not. */
		virtual bool can_write() const { return true; }

		/**
		 * get total length of this stream.
		 * @warn default-impl uses `tell()` and `seek()` to provide length!
		 * @returns:
		 *	>= 0: length.
		 *  <  0: not supported.
		 */
		virtual ssize_t get_length() const {
			ssize_t pos = tell();

			if (pos >= 0) {
				stream* m_this = (stream*)this;
				if (!m_this->seek(0, SEEK_END))
					return -1;

				size_t len = tell();
				m_this->seek(pos, SEEK_SET);

				return len;
			}

			return -1;
		}

		/**
		 * get current position of file cursor.
		 * @returns:
		 *	>= 0: position.
		 *  <  0: not supported.
		 */
		virtual ssize_t tell() const = 0;

		/**
		 * set current position of file cursor.
		 * @params: one of SEEK_SET, SEEK_CUR, SEEK_END
		 * @returns false if not supported.
		 */
		virtual bool seek(ssize_t off, int32_t orig) = 0;

		/**
		 * read bytes from stream.
		 * @returns:
		 *	> 0: read size.
		 *  = 0: end of stream.
		 *  < 0: not supported or not available if non-block.
		 * @note:
		 *	errno == EWOULDBLOCK: for non-block mode.
		 */
		virtual int32_t read(void* buf, size_t len) = 0;

		/**
		 * wrtie bytes into stream.
		 * @returns:
		 *	> 0: read size.
		 *  = 0: end of stream.
		 *  < 0: not supported or not available if non-block.
		 * @note:
		 *	errno == EWOULDBLOCK: for non-block mode.
		 */
		virtual int32_t write(const void* buf, size_t len) = 0;

		/* flush internal buffers. */
		virtual void flush() { }

		/* close stream. */
		virtual void close() { }
	};
}