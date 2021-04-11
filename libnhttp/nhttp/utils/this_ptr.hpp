#pragma once
#include "../types.hpp"

namespace nhttp {


	/**
	 * class this_ptr<type>.
	 * semantic for `this` pointer.
	 */
	template<typename type>
	struct this_ptr {
		type* ptr;

		inline this_ptr(type* ptr) : ptr(ptr) { }
		inline this_ptr(std::shared_ptr<type> ptr) : ptr(ptr.get()) { }
		inline this_ptr(const type* ptr) : ptr(const_cast<type*>(ptr)) { }
		inline ~this_ptr() { }

		/* access to. */
		inline type* operator ->() { return ptr; }
		inline const type* operator ->() const { return ptr; }

		inline type& operator *() { return *ptr; }
		inline const type& operator *() const { return *ptr; }
	};


}