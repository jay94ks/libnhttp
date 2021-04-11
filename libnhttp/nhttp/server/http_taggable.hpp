#pragma once
#include "../types.hpp"
#include "../hal/rwlock_t.hpp"

namespace nhttp {
namespace server {

	class http_context;
	class http_raw_link;
	class http_request;
	class http_response;

	/**
	 * class http_hook_tag.
	 * hook interface for tags. 
	 * these hooks can have a chance to modify response at closing the response.
	 * and, http link tags will never get hook called.
	 */
	class NHTTP_API http_hook_tag {
		friend class http_context;
		friend class http_raw_link;

	private:
		uint32_t priority;

	public:
		http_hook_tag(uint32_t priority = 0xfffffffful) : priority(priority) { }
		virtual ~http_hook_tag() { }

	public:
		/* get priority of this hook. */
		inline uint32_t get_priority() const { return priority; }

	protected:
		/* hook the response at closing. */
		virtual void on_response(http_request& request, http_response& response) { };
	};

namespace _ {
	template<typename type, bool = std::is_base_of<http_hook_tag, type>::value>
	struct to_http_tag {
		inline static http_hook_tag* cast(type*) { return nullptr; }
	};

	template<typename type>
	struct to_http_tag<type, true> {
		inline static http_hook_tag* cast(type* ptr) { return dynamic_cast<http_hook_tag*>(ptr); }
	};
}

	template<typename type>
	inline http_hook_tag* to_http_tag(type* ptr) {
		return _::to_http_tag<type>::cast(ptr);
	}

	/**
	 * class http_tag.
	 * taggable interface for HTTP objects.
	 */
	class NHTTP_API http_taggable {
		friend class http_context;
		friend class http_raw_link;

	private:
		struct tag_type {
			void* data;
			void(*dtor)(void*);
			http_hook_tag* hook;
		};

	protected:
		mutable hal::rwlock_t lock;
		std::map<size_t, tag_type> tags;
		std::vector<http_hook_tag*> hooks;

	public:
		http_taggable() { }
		virtual ~http_taggable() {
			for (auto& each : tags) {
				each.second.dtor(each.second.data);
			}

			tags.clear();
		}

	public:
		/**
		 * get tag object pointer.
		 * if no tag set, new-tag set using default ctor.
		 */
		template<typename type>
		inline type* ensured_tag() {
			type* tag_ptr = nullptr;
			lock.lock_write();

			const auto& place = tags.find(typeid(type).hash_code());

			if (place == tags.end()) {
				tag_type tag;

				tag.data = tag_ptr = new type();
				tag.dtor = [](void* p) { delete (type*)p; };
				tag.hook = to_http_tag((type*)tag.data);

				set_tag_void(typeid(type).hash_code(), tag);
			}

			else tag_ptr = (type*) place->second.data;

			lock.unlock_write();
			return tag_ptr;
		}

		/* get tag object pointer. */
		template<typename type>
		inline type* get_tag_ptr() const {
			lock.lock_read();
			const auto& place = tags.find(typeid(type).hash_code());

			if (place != tags.end()) {
				auto* tag = (type*)place->second.data;
				lock.unlock_read();

				return tag;
			}

			lock.unlock_read();
			return nullptr;
		}

		/* set tag object by default ctor. */
		template<typename type>
		inline type* set_tag() {
			tag_type tag;

			tag.data = new type();
			tag.dtor = [](void* p) { delete (type*)p; };
			tag.hook = to_http_tag((type*)tag.data);

			lock.lock_write();
			set_tag_void(typeid(type).hash_code(), tag);
			lock.unlock_write();

			return (type*)tag.data;
		}

		/* set tag object by copy ctor. */
		template<typename type>
		inline type* set_tag(const type& value) {
			tag_type tag;

			tag.data = new type(value);
			tag.dtor = [](void* p) { delete (type*)p; };
			tag.hook = to_http_tag((type*)tag.data);

			lock.lock_write();
			set_tag_void(typeid(type).hash_code(), tag);
			lock.unlock_write();

			return (type*)tag.data;
		}

		/* set tag object by move ctor. */
		template<typename type>
		inline type* set_tag(type&& value) {
			tag_type tag;

			tag.data = new type(std::move(value));
			tag.dtor = [](void* p) { delete (type*)p; };
			tag.hook = to_http_tag((type*)tag.data);

			lock.lock_write();
			set_tag_void(typeid(type).hash_code(), tag);
			lock.unlock_write();

			return (type*)tag.data;
		}

		template<typename type>
		inline bool unset() {
			lock.lock_write();
			bool ret = unset_tag(typeid(type).hash_code());
			lock.unlock_write();

			return ret;
		}

	private:
		inline bool unset_tag(size_t id) {
			const auto& place = tags.find(id);

			if (place != tags.end()) {
				auto* hook = place->second.hook;
				place->second.dtor(place->second.data);

				for (auto i = hooks.begin(); i != hooks.end(); ++i) {
					if (*i == hook) {
						hooks.erase(i);
						break;
					}
				}

				tags.erase(place);
				return true;
			}

			return false;
		}

		inline void set_tag_void(size_t id, tag_type& tag) {
			unset_tag(id);
			tags.emplace(id, tag);

			if (tag.hook) {
				hooks.push_back(tag.hook);

				std::sort(hooks.begin(), hooks.end(),
					[](http_hook_tag* left, http_hook_tag* right) {
						return left->get_priority() < right->get_priority();
					});
			}

		}
	};

}
}