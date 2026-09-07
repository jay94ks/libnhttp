#pragma once

#include <map>
#include <string>
#include <string_view>

namespace nhttp::protocol {

	namespace mime_types {
		inline constexpr std::string_view TEXT_HTML = "text/html";
		inline constexpr std::string_view TEXT_PLAIN = "text/plain";
		inline constexpr std::string_view TEXT_CSS = "text/css";
		inline constexpr std::string_view TEXT_CSV = "text/csv";
		inline constexpr std::string_view APPLICATION_JSON = "application/json";
		inline constexpr std::string_view APPLICATION_JAVASCRIPT = "application/javascript";
		inline constexpr std::string_view APPLICATION_OCTET_STREAM = "application/octet-stream";
		inline constexpr std::string_view APPLICATION_XML = "application/xml";
		inline constexpr std::string_view APPLICATION_X_WWW_FORM_URLENCODED = "application/x-www-form-urlencoded";
		inline constexpr std::string_view APPLICATION_PDF = "application/pdf";
		inline constexpr std::string_view MULTIPART_FORM_DATA = "multipart/form-data";
		inline constexpr std::string_view IMAGE_PNG = "image/png";
		inline constexpr std::string_view IMAGE_JPEG = "image/jpeg";
		inline constexpr std::string_view IMAGE_GIF = "image/gif";
		inline constexpr std::string_view IMAGE_SVG = "image/svg+xml";
		inline constexpr std::string_view IMAGE_ICON = "image/x-icon";
	}

	/**
	 * parses "; key=value; key2=\"quoted value\"" style parameters (the part of a
	 * header value after its first semicolon) into a map. shared by mime_type and
	 * by multipart_part's Content-Disposition parsing, since both use this exact
	 * grammar (RFC 2045 §5.1 parameter syntax).
	 */
	std::map<std::string, std::string, std::less<>> parse_header_parameters(std::string_view value);

	/**
	 * class mime_type.
	 * a parsed "type/subtype; param=value" header value (Content-Type).
	 */
	class mime_type {
	public:
		std::string type;
		std::string subtype;
		std::map<std::string, std::string, std::less<>> parameters;

	public:
		std::string essence() const { return type + "/" + subtype; }
		const std::string* parameter(std::string_view name) const noexcept;

		static mime_type parse(std::string_view value);
		std::string to_string() const;
	};

	/* maps a file extension (from a path, e.g. ".html" or "html") to a MIME essence;
	 * falls back to application/octet-stream when unknown. */
	std::string_view mime_type_from_extension(std::string_view path) noexcept;

}
