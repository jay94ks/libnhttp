#pragma once
#include "../../types.hpp"
#include "../../hal/barrior_t.hpp"

namespace nhttp {
namespace server {

	struct http_chunked_bytes {
		http_chunked_bytes* next;

		uint8_t* head;
		size_t size;

		size_t left, right;
	};

	/**
	 * class http_chunked_alloc.
	 * allocate a http_chunked_bytes struct pointer.
	 */
	class NHTTP_API http_chunked_alloc {
	private:
		hal::barrior_t barrior;
		http_chunked_bytes* pool;

		size_t max_chunks, chunk_size;
		size_t active_chunks;

	public:
		http_chunked_alloc(size_t max_chunks, size_t chunk_size)
			: pool(nullptr), max_chunks(max_chunks), chunk_size(chunk_size), active_chunks(0)
		{
		}

		~http_chunked_alloc() {
			while (pool) {
				auto* cur = pool;
				pool = pool->next;

				delete[] (uint8_t*)cur;
			}
		}

	public:
		/**
		 * allocate a chunk.
		 */
		inline http_chunked_bytes* alloc() {
			barrior.lock();
			
			if (pool) {
				auto* chunk = pool;
				pool = pool->next;
				barrior.unlock();

				chunk->head = (uint8_t*)(chunk + 1);
				chunk->size = chunk_size;
				chunk->next = nullptr;
				chunk->left = chunk->right = 0;

				return chunk;
			}

			if (active_chunks < max_chunks) {
				++active_chunks;
				barrior.unlock();

				http_chunked_bytes* chunk = (http_chunked_bytes*) new uint8_t[
									  sizeof(http_chunked_bytes) + chunk_size];

				chunk->head = (uint8_t*)(chunk + 1);
				chunk->size = chunk_size;
				chunk->next = nullptr;
				chunk->left = chunk->right = 0;

				return chunk;
			}

			barrior.unlock();
			return nullptr;
		}

		/**
		 * deallocate a chunk.
		 */
		inline void dealloc(http_chunked_bytes* chunk) {
			barrior.lock();
			chunk->next = pool;
			pool = chunk;
			barrior.unlock();
		}
	};

}
}