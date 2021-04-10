#pragma once
#include "../types.hpp"
#include <type_traits>

namespace nhttp {
namespace utils {

	/**
	 * struct has_default_object_t<object>.
	 * Determines the object has default or not.
	 */
	template<typename object>
	struct has_default_object_t : std::is_default_constructible<object> { };

	template<typename object>
	constexpr bool has_default_object = has_default_object_t<object>::value;

	/**
	 * Re-initialize an object to default.
	 */
	template<typename object, bool = true, bool = has_default_object<object>>
	struct defaultify_t {
		static_assert(
			has_default_object<object>,
			"defaultify_t<object> requires default constructor!"
		);
	};

	/**
	 * struct defaultify_t<object, bool strict>.
	 * Re-initialize an object to default or,
	 * Construct default object at uninitialized memory pointer.
	 */
	template<typename object>
	struct defaultify_t<object, false, false> {
		inline static bool initialize(object* _where) { return false; }
		inline static bool defaultify(object& _where) { return false; }
	};

	template<typename object, bool any>
	struct defaultify_t<object, any, true> {
		inline static bool initialize(object* _where) {
			new (_where) object();
			return true;
		}

		inline static bool defaultify(object& _where) {
			_where.~object();
			new (&_where) object();
			return true;
		}
	};

	template<typename object, bool with_assert = true>
	inline bool initialize_to_default(object* _where) {
		return defaultify_t<object, with_assert>::initialize(_where);
	}

	template<typename object, bool with_assert = true>
	inline bool defaultify(object& _where) {
		return defaultify_t<object, with_assert>::defaultify(_where);
	}

}
}