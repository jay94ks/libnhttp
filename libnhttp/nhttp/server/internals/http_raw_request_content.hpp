#pragma once
#include "../../io/stream.hpp"
#include "../../hal/event_t.hpp"

namespace nhttp {
namespace server {
	class http_raw_link;
	class http_chunked_buffer;

	/**
	 * class http_raw_request_content.
	 * safe and fast bridge between link and context.
	 */
	class NHTTP_API http_raw_request_content : public stream {
		friend class http_raw_link;

	protected:
		mutable hal::spinlock_t spinlock;

	private:
		mutable hal::event_lite_t waiter;
		
		ssize_t total_bytes;
		size_t avail_bytes;

		bool read_requested, is_end, non_block;
		std::shared_ptr<http_chunked_buffer> buffer;
		
	public:
		http_raw_request_content(std::shared_ptr<http_chunked_buffer> buffer, ssize_t total_bytes);

	protected:
		inline bool is_disconnected() const { 
			std::lock_guard<decltype(spinlock)> guard(spinlock); 
			return !buffer;
		}

	public:
		/* determines validity of this stream. */
		virtual bool is_valid() const override;

		/* determines currently at end of stream. */
		virtual bool is_end_of() const override;

		/* determines this stream is based on non-blocking or not. */
		virtual bool is_nonblock() const override;

		/**
		 * set non-blocking.
		 * @returns:
		 *	= true : supported and set.
		 *  = false: not supported.
		 */
		virtual bool set_nonblock(bool value) override;

		/* determines this stream can be read immediately or not. */
		virtual bool can_read() const override;

	protected:
		bool wanna_read(size_t spins = 5) const;

		/* notify given bytes ready to provide. */
		void notify(size_t bytes, bool is_end);

		/* link will call this before terminating the request. */
		void disconnect();

	public:
		/**
		 * get total length of this stream.
		 * @warn default-impl uses `tell()` and `seek()` to provide length!
		 * @returns:
		 *	>= 0: length.
		 *  <  0: not supported.
		 */
		virtual ssize_t get_length() const override;

		/**
		 * get current position of file cursor.
		 * @returns:
		 *	>= 0: position.
		 *  <  0: not supported.
		 */
		virtual ssize_t tell() const override {
			set_errno_c(ENOTSUP);
			return -1;
		}

		/**
		 * set current position of file cursor.
		 * @params: one of SEEK_SET, SEEK_CUR, SEEK_END
		 * @returns false if not supported.
		 */
		virtual bool seek(ssize_t off, int32_t orig) override {
			set_errno(ENOTSUP);
			return false;
		}

		/**
		 * read bytes from stream.
		 * @returns:
		 *	> 0: read size.
		 *  = 0: end of stream.
		 *  < 0: not supported or not available if non-block.
		 * @note:
		 *	errno == EWOULDBLOCK: for non-block mode.
		 */
		virtual int32_t read(void* buf, size_t len) override;

		/**
		 * wrtie bytes into stream.
		 * @returns:
		 *	> 0: read size.
		 *  = 0: end of stream.
		 *  < 0: not supported or not available if non-block.
		 * @note:
		 *	errno == EWOULDBLOCK: for non-block mode.
		 */
		virtual int32_t write(const void* buf, size_t len) override { 
			set_errno(ENOTSUP);
			return -1;
		}

		/* close stream. */
		virtual void close() {
			disconnect();
			std::this_thread::yield();
		}

	protected:
		/**
		 * read bytes from stream with already locked.
		 * @returns:
		 *	> 0: read size.
		 *  = 0: end of stream.
		 *  < 0: not supported or not available if non-block.
		 * @note:
		 *	errno == EWOULDBLOCK: for non-block mode.
		 */
		virtual int32_t read_locked(void* buf, size_t len);
	};

}
}