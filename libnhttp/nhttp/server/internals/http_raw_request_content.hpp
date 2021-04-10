#pragma once
#include "../../io/stream.hpp"
#include "../../hal/event_t.hpp"

namespace nhttp {
namespace server {
	class http_raw_link;

	/**
	 * class http_raw_request_content.
	 * safe and fast bridge between link and context.
	 */
	class NHTTP_API http_raw_request_content : public stream {
		friend class http_raw_link;

	private:
		mutable hal::spinlock_t spinlock;
		mutable hal::event_lite_t waiter;
		
		ssize_t total_bytes;
		size_t avail_bytes;

		bool read_requested, is_end, non_block;
		std::shared_ptr<http_chunked_buffer> buffer;
		
	public:
		http_raw_request_content(std::shared_ptr<http_chunked_buffer> buffer, ssize_t total_bytes)
			: waiter(false, true), buffer(buffer), total_bytes(total_bytes), read_requested(false), 
			  avail_bytes(0), is_end(false), non_block(false)
		{
		}

	public:
		/* determines validity of this stream. */
		virtual bool is_valid() const override {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			return buffer != nullptr;
		}

		/* determines currently at end of stream. */
		virtual bool is_end_of() const override {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			return buffer == nullptr || (avail_bytes <= 0 && is_end);
		}

		/* determines this stream is based on non-blocking or not. */
		virtual bool is_nonblock() const override {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			return buffer == nullptr || non_block;
		}

		/**
		 * set non-blocking.
		 * @returns:
		 *	= true : supported and set.
		 *  = false: not supported.
		 */
		virtual bool set_nonblock(bool value) override {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			non_block = value;
			return true;
		}

		/* determines this stream can be read immediately or not. */
		virtual bool can_read() const override {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			return buffer == nullptr || avail_bytes > 0;
		}

	protected:
		inline bool wanna_read(size_t spins = 5) const {
			while(spins--) {
				if (!spinlock.try_lock())
					return false;

				if (buffer != nullptr && read_requested) {
					spinlock.unlock();
					return true;
				}

				spinlock.unlock();
			}

			return false;
		}

		/* notify given bytes ready to provide. */
		inline void notify(size_t bytes, bool is_end) {
			std::lock_guard<decltype(spinlock)> guard(spinlock);

			avail_bytes += bytes;
			this->is_end = is_end;

			waiter.signal();
		}

		/* link will call this before terminating the request. */
		inline void disconnect() {
			std::lock_guard<decltype(spinlock)> guard(spinlock);

			buffer = nullptr;
			read_requested = false;
			avail_bytes = 0;
			waiter.signal();
		}

	public:
		/**
		 * get total length of this stream.
		 * @warn default-impl uses `tell()` and `seek()` to provide length!
		 * @returns:
		 *	>= 0: length.
		 *  <  0: not supported.
		 */
		virtual ssize_t get_length() const override {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			if (buffer != nullptr)  {
				set_errno_c(0);

				if (total_bytes < 0)
					set_errno_c(ENOTSUP);

				return total_bytes;
			}

			set_errno_c(ENOENT);
			return 0;
		}

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
		virtual int32_t read(void* buf, size_t len) override {
			size_t read_bytes;

			spinlock.lock();
			set_errno(0);

			while (buffer != nullptr) {
				if ((read_bytes = len > avail_bytes ? avail_bytes : len) > 0) {
					size_t ret = buffer->read(buf, read_bytes);

					/* keep flag if length is less than read. */
					read_requested = ret < len;
					avail_bytes -= ret;

					if (avail_bytes > 0)
						 waiter.signal();

					else waiter.unsignal();

					spinlock.unlock();
					std::this_thread::yield();
					return int32_t(ret);
				}

				read_requested = true;

				if (non_block) {
					set_errno(EWOULDBLOCK);
					spinlock.unlock();
					return -1;
				}
					
				spinlock.unlock();
				std::this_thread::yield();

				waiter.wait();
				spinlock.lock();
			}

			set_errno(ENOENT);
			spinlock.unlock();
			return 0;
		}

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
	};

}
}