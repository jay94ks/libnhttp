#include "nhttp/protocol/http_method.hpp"

#include <array>

namespace nhttp::protocol {

	namespace {

		struct known_entry {
			std::string_view name;
			http_method_flags flags;
			http_method_id id;
		};

		constexpr std::array<known_entry, 9> known_methods{ {
			{ "GET", http_method_flags::response_content | http_method_flags::idempotent | http_method_flags::cacheable, http_method_id::get },
			{ "HEAD", http_method_flags::idempotent | http_method_flags::cacheable, http_method_id::head },
			{ "POST", http_method_flags::request_content | http_method_flags::response_content | http_method_flags::alter_state | http_method_flags::conditional_cacheable, http_method_id::post },
			{ "PUT", http_method_flags::request_content | http_method_flags::alter_state | http_method_flags::idempotent, http_method_id::put },
			{ "DELETE", http_method_flags::alter_state | http_method_flags::idempotent, http_method_id::del },
			{ "PATCH", http_method_flags::request_content | http_method_flags::alter_state, http_method_id::patch },
			{ "OPTIONS", http_method_flags::idempotent, http_method_id::options },
			{ "TRACE", http_method_flags::idempotent, http_method_id::trace },
			{ "CONNECT", http_method_flags::alter_state, http_method_id::connect },
		} };

		const known_entry* find_known(std::string_view name) noexcept {
			for (const known_entry& e : known_methods) {
				if (e.name == name)
					return &e;
			}

			return nullptr;
		}

	}

	http_method::http_method(std::string name)
		: name_(std::move(name))
	{
		if (const known_entry* e = find_known(name_)) {
			flags_ = e->flags;
			id_ = e->id;
		}
	}

#define NHTTP_DEFINE_METHOD(fn, literal) \
	const http_method& http_method::fn() { \
		static const http_method instance{ std::string(literal) }; \
		return instance; \
	}

	NHTTP_DEFINE_METHOD(GET, "GET")
	NHTTP_DEFINE_METHOD(HEAD, "HEAD")
	NHTTP_DEFINE_METHOD(POST, "POST")
	NHTTP_DEFINE_METHOD(PUT, "PUT")
	NHTTP_DEFINE_METHOD(DELETE, "DELETE")
	NHTTP_DEFINE_METHOD(PATCH, "PATCH")
	NHTTP_DEFINE_METHOD(OPTIONS, "OPTIONS")
	NHTTP_DEFINE_METHOD(TRACE, "TRACE")
	NHTTP_DEFINE_METHOD(CONNECT, "CONNECT")

#undef NHTTP_DEFINE_METHOD

}
