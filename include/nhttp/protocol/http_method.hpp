#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nhttp::protocol {

	enum class http_method_flags : std::uint32_t {
		none = 0,
		request_content = 1u << 0,   // conventionally may carry a request body
		response_content = 1u << 1,  // conventionally has a response body
		alter_state = 1u << 2,       // conventionally not idempotent (POST, PATCH)
		idempotent = 1u << 3,
		cacheable = 1u << 4,
		conditional_cacheable = 1u << 5,
	};

	constexpr http_method_flags operator|(http_method_flags a, http_method_flags b) noexcept {
		return static_cast<http_method_flags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
	}

	constexpr http_method_flags operator&(http_method_flags a, http_method_flags b) noexcept {
		return static_cast<http_method_flags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
	}

	/**
	 * class http_method.
	 * an HTTP method name plus its conventional semantic flags, looked up from a
	 * small table rather than switched on per call site — see CONCEPTS.md's
	 * "method semantics as data" principle. unrecognized names are still valid
	 * (custom/WebDAV-style methods), just with no flags set.
	 */
	class http_method {
	public:
		http_method() : http_method(std::string()) { }
		explicit http_method(std::string name);

	public:
		const std::string& name() const noexcept { return name_; }
		http_method_flags flags() const noexcept { return flags_; }
		bool is(http_method_flags flag) const noexcept { return (flags_ & flag) != http_method_flags::none; }

		bool operator==(const http_method& other) const noexcept { return name_ == other.name_; }
		bool operator!=(const http_method& other) const noexcept { return !(*this == other); }
		bool operator<(const http_method& other) const noexcept { return name_ < other.name_; }

	public:
		static const http_method& GET();
		static const http_method& HEAD();
		static const http_method& POST();
		static const http_method& PUT();
		static const http_method& DELETE();
		static const http_method& PATCH();
		static const http_method& OPTIONS();
		static const http_method& TRACE();
		static const http_method& CONNECT();

	private:
		std::string name_;
		http_method_flags flags_ = http_method_flags::none;
	};

}
