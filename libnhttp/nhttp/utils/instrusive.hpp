#pragma once
#include "../types.hpp"
#include "../assert.hpp"
#include "defaultify.hpp"
#include <type_traits>

namespace nhttp {
namespace utils {


	/**
	 * struct instrusive<type>.
	 * storing an object which isn't default constructible, or unknown object.
	 */
	template<typename _type, bool no_default = false>
	struct instrusive {
		using type = _type;

		static_assert(
			std::is_move_constructible<type>::value || std::is_copy_constructible<type>::value,
			"instrusive<type> can work with move-constructible or copy-constructible types only!"
		);
		
		/**
		 * byte blobs for storing `type`d data.
		 */
		uint8_t blobs[sizeof(type)] NHTTP_DEBUG( = { 0, });
		uint8_t ready;

#define nhttp_instrusive_init(from) \
		if (ready) { new (blobs) type(from); }

#define nhttp_instrusive_deinit(inst) \
		if ((inst).ready) { (*((type*)((inst).blobs))).~type(); (inst).ready = 0; }
		
		instrusive() : ready(0) { 
			if (!no_default) {
				ready = initialize_to_default<type, false>((type*)blobs);
			}
		}

		instrusive(const type& value) : ready(1) { nhttp_instrusive_init(value); }
		instrusive(type&& value) : ready(1) { nhttp_instrusive_init(std::move(value)); }
		instrusive(const instrusive& other) : ready(other.ready) {
			if (ready) {
				nhttp_instrusive_init(*((type*)other.blobs));
			}
		}
		instrusive(instrusive&& other) : ready(other.ready) {
			if (ready) {
				nhttp_instrusive_init(std::move(*((type*)other.blobs)));
				nhttp_instrusive_deinit(other);
			}

			other.ready = 0;
		}
		instrusive(const instrusive<type, !no_default>& other) : ready(other.ready) {
			if (ready) {
				nhttp_instrusive_init(*((type*)other.blobs));
			}
		}
		instrusive(const instrusive<type, !no_default>&& other) : ready(other.ready) {
			if (ready) {
				nhttp_instrusive_init(std::move(*((type*)other.blobs)));
				nhttp_instrusive_deinit(other);
			}

			other.ready = 0;
		}
		~instrusive() {
			nhttp_instrusive_deinit(*this);
		}

		inline operator bool() const { return ready; }
		inline bool operator !() const { return !ready; }

		inline instrusive& unset() {
			nhttp_instrusive_deinit(*this);
			return *this;
		}

		inline type* as_ptr() {
			NHTTP_INIT_ASSERT(ready, "tried to access uninitialized instrusive<type>!");
			return ((type*)blobs);
		}

		inline type& operator *() {
			NHTTP_INIT_ASSERT(ready, "tried to access uninitialized instrusive<type>!");
			return *((type*)blobs); 
		}

		inline const type& operator *() const {
			NHTTP_INIT_ASSERT(ready, "tried to access uninitialized instrusive<type>!");
			return *((type*)blobs); 
		}

		inline instrusive& operator =(const instrusive& other) {
			nhttp_instrusive_deinit(*this); ready = other.ready;
			nhttp_instrusive_init(*((type*)other.blobs));
			return *this;
		}

		inline instrusive& operator =(instrusive&& other) {
			nhttp_instrusive_deinit(*this); ready = other.ready;
			nhttp_instrusive_init(std::move(*((type*)other.blobs)));
			return *this;
		}

		inline instrusive& operator =(const instrusive<type, !no_default>& other) {
			nhttp_instrusive_deinit(*this); ready = other.ready;
			nhttp_instrusive_init(*((type*)other.blobs));
			return *this;
		}

		inline instrusive& operator =(instrusive<type, !no_default>&& other) {
			nhttp_instrusive_deinit(*this); ready = other.ready;
			nhttp_instrusive_init(std::move(*((type*)other.blobs)));
			return *this;
		}

		inline instrusive& operator =(const type& value) {
			nhttp_instrusive_deinit(*this); ready = 1;
			nhttp_instrusive_init(value);
			return *this;
		}

		inline instrusive& operator =(type&& value) {
			nhttp_instrusive_deinit(*this); ready = 1;
			nhttp_instrusive_init(std::move(value));
			return *this;
		}
	};

#undef nhttp_instrusive_init
#undef nhttp_instrusive_deinit

}
}