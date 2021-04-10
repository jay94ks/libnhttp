#pragma once
#include "http_chunked_alloc.hpp"

namespace nhttp {
namespace server {

	class NHTTP_API http_chunked_buffer {
	private:
		mutable hal::spinlock_t spinlock;
		std::shared_ptr<http_chunked_alloc> allocator;
		http_chunked_bytes* head, *tail;
		std::atomic<size_t> total, length;

	public:
		http_chunked_buffer(
			const std::shared_ptr<http_chunked_alloc>& allocator, http_chunked_bytes* head)
			: allocator(allocator), head(head), tail(head), total(head->size), length(0)
		{
		}

		~http_chunked_buffer() {
			while (head) {
				auto* cur = head;
				head = head->next;

				allocator->dealloc(cur);
			}

			head = tail = nullptr;
		}

	public:
		inline size_t get_chunk_size() const { return head->size; }

		inline size_t get_capacity() const { return total; }
		inline size_t get_size() const { return length; }

		inline size_t get_left_capacity() const {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			return (tail->size - tail->right) +
				   (tail->next ? tail->next->size : 0);
		}

		/* try pre-allocate. */
		inline bool preallocate() {
			std::lock_guard<decltype(spinlock)> guard(spinlock);

			if (tail->next)
				return true;

			if (!(tail->next = allocator->alloc()))
				return false;

			total += tail->next->size;
			return true;
		}

	public:
		/* find a byte from chunks. */
		inline ssize_t find(uint8_t ch) const {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			size_t offset = 0, skips = 0;
			auto* cursor = head;

			while (cursor) {
				const uint8_t* beg = cursor->head + cursor->left;
				const uint8_t* end = cursor->head + cursor->right;
				const uint8_t* ret = (uint8_t*)memchr(beg, ch, size_t(end - beg));

				if (ret) {
					return ssize_t(size_t(ret - beg) + skips);
				}

				skips += (cursor->right - cursor->left);
				cursor = cursor->next;
			}

			return -1;
		}

		/* skip bytes from buffer. */
		inline size_t skip(size_t len) {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			size_t read_len = 0;

			while (len) {
				size_t avail = head->right - head->left;

				if (!avail) {
					auto* next = head->next;

					if (!next) {
						head->left = head->right = 0;
						break;
					}

					total -= head->size;
					allocator->dealloc(head);

					head = next;
					continue;
				}

				avail = len > avail ? avail : len;
				read_len += avail;
				tail->left += avail;

				len -= avail;
				length -= avail;

			}

			return read_len;
		}

		/* read bytes from buffer. */
		inline size_t read(void* buf, size_t len) {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			size_t read_len = 0;

			while (len) {
				size_t avail = head->right - head->left;

				if (!avail) {
					auto* next = head->next;

					if (!next) {
						head->left = head->right = 0;
						break;
					}

					total -= head->size;
					allocator->dealloc(head);

					head = next;
					continue;
				}

				avail = len > avail ? avail : len;
				memcpy(buf, tail->head + tail->left, avail);
				
				read_len += avail;
				tail->left += avail;

				buf = (uint8_t*)buf + avail;
				len -= avail;

				length -= avail;

			}

			return read_len;
		}

		/* write bytes from buffer. */
		inline size_t write(const void* buf, size_t len) {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			size_t write_len = 0;

			while (len) {
				size_t avail = tail->size - tail->right;

				if (tail->left == tail->right) {
					tail->left = tail->right = 0;
					avail = tail->size;
				}

				if (!avail) {
					/**
					 * if there is pre-allocated tail, 
					 * shift to pre-aloocated tail. 
					 */
					if (tail->next) {
						tail = tail->next;
						continue;
					}

					auto* newthing = allocator->alloc();

					if (!newthing)
						break;

					tail->next = newthing;
					tail = newthing;

					total += newthing->size;
					continue;
				}

				avail = len > avail ? avail : len;
				memcpy(tail->head + tail->right, buf, avail);

				write_len += avail;
				tail->right += avail;

				buf = (const uint8_t*)buf + avail;
				len -= avail;

				length += avail;
			}

			return write_len;
		}
	};

}
}