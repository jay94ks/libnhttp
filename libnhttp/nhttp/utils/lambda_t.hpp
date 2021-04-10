#pragma once
#include "../types.hpp"
#include "../assert.hpp"

namespace nhttp {

	template<typename signature>
	struct lambda_t;

	template<typename ret_type, typename ... types>
	struct lambda_t<ret_type(types ...)> {
		void* callable;
		void*(*clone)(void*);
		void(*dtor)(void*);
		ret_type(*call)(void*, types&& ...);

		lambda_t() : callable(nullptr), dtor(nullptr), call(nullptr) { }
		lambda_t(ret_type(*c_style)(types ...)) : lambda_t() {
			typedef ret_type(*c_style_fptr)(types ...);
			callable = c_style;
			call = [](void* p, types&& ... args) { return ((c_style_fptr)p)(std::forward<types>(args) ...); };
			clone = [](void* p) { return p; };
			dtor = [](void* p) { };
		}

		template<typename lambda_type>
		lambda_t(lambda_type&& lambda) {
			using l_ret_type = decltype(std::declval<lambda_type>()(std::declval<types>() ...));
			static_assert(
				std::is_convertible<l_ret_type, ret_type>::value,
				"incompatible lambda expression."
			);

			callable = new lambda_type(std::move(lambda));
			call = [](void* p, types&& ... args) { return (*(lambda_type*)p)(std::forward<types>(args) ...); };
			clone = [](void* p) { return new lambda_type(*(lambda_type*)p); };
			dtor = [](void* p) { delete (lambda_type*)p; };
		}

		lambda_t(const lambda_t& o) : lambda_t() {
			if (o.callable) {
				callable = o.clone(o.callable);
				call = o.call;
				clone = o.clone;
				dtor = o.dtor;
			}
		}

		lambda_t(lambda_t&& o) : lambda_t() {
			std::swap(callable, o.callable);
			std::swap(call, o.call);
			std::swap(clone, o.clone);
			std::swap(dtor, o.dtor);
		}

		~lambda_t() {
			if (dtor) {
				dtor(callable);
				dtor = nullptr;
			}
		}

		inline operator bool() const { return callable; }
		inline bool operator !() const { return !callable; }

		inline lambda_t& operator =(const lambda_t& o) {
			lambda_t temp(o);

			std::swap(temp.callable, callable);
			std::swap(temp.call, call);
			std::swap(temp.clone, clone);
			std::swap(temp.dtor, dtor);

			return *this;
		}

		inline lambda_t& operator =(lambda_t&& o) {
			std::swap(o.callable, callable);
			std::swap(o.call, call);
			std::swap(o.clone, clone);
			std::swap(o.dtor, dtor);
			return *this;
		}

		/**
		 * call assigned predicate.
		 */
		inline ret_type operator()(types&& ... args) {
			NHTTP_INIT_ASSERT(callable,
				"tried to call uninitialized predicate!");

			return call(callable, std::forward<types>(args) ...);
		}
	};

}