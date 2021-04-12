#include "http_raw_request_content.hpp"
#include "http_chunked_buffer.hpp"

namespace nhttp {
namespace server {

	http_raw_request_content::http_raw_request_content(std::shared_ptr<http_chunked_buffer> buffer, ssize_t total_bytes)
		: waiter(false, true), buffer(buffer), total_bytes(total_bytes), read_requested(false),
		  avail_bytes(0), is_end(false), non_block(false)
	{
	}

	/* determines validity of this stream. */

	bool http_raw_request_content::is_valid() const {
		std::lock_guard<decltype(spinlock)> guard(spinlock);
		return buffer != nullptr;
	}

	/* determines currently at end of stream. */

	bool http_raw_request_content::is_end_of() const {
		std::lock_guard<decltype(spinlock)> guard(spinlock);
		return buffer == nullptr || (avail_bytes <= 0 && is_end) || !total_bytes;
	}

	/* determines this stream is based on non-blocking or not. */

	bool http_raw_request_content::is_nonblock() const {
		std::lock_guard<decltype(spinlock)> guard(spinlock);
		return buffer == nullptr || non_block;
	}

	bool http_raw_request_content::set_nonblock(bool value) {
		std::lock_guard<decltype(spinlock)> guard(spinlock);
		non_block = value;
		return true;
	}

	/* determines this stream can be read immediately or not. */

	bool http_raw_request_content::can_read() const {
		std::lock_guard<decltype(spinlock)> guard(spinlock);
		return buffer == nullptr || avail_bytes > 0;
	}

	bool http_raw_request_content::wanna_read(size_t spins) const {
		while (spins--) {
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

	void http_raw_request_content::notify(size_t bytes, bool is_end) {
		std::lock_guard<decltype(spinlock)> guard(spinlock);

		avail_bytes += bytes;
		this->is_end = is_end;

		waiter.signal();
	}

	/* link will call this before terminating the request. */

	void http_raw_request_content::disconnect() {
		std::lock_guard<decltype(spinlock)> guard(spinlock);

		buffer = nullptr;
		read_requested = false;
		avail_bytes = 0;
		waiter.signal();
	}

	/**
	* get total length of this stream.
	* @warn default-impl uses `tell()` and `seek()` to provide length!
	* @returns:
	*	>= 0: length.
	*  <  0: not supported.
	*/

	ssize_t http_raw_request_content::get_length() const {
		std::lock_guard<decltype(spinlock)> guard(spinlock);
		if (buffer != nullptr) {
			set_errno_c(0);

			if (total_bytes < 0)
				set_errno_c(ENOTSUP);

			return total_bytes;
		}

		set_errno_c(ENOENT);
		return 0;
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

	int32_t http_raw_request_content::read(void* buf, size_t len) {
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

	int32_t http_raw_request_content::read_locked(void* buf, size_t len) {
		size_t read_bytes;
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
	}

}
}