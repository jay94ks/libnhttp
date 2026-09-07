#include "nhttp/protocol/http_mime_type.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace nhttp::protocol {

	namespace {

		std::string_view trim(std::string_view s) noexcept {
			while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
				s.remove_prefix(1);

			while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
				s.remove_suffix(1);

			return s;
		}

		char to_lower_char(char c) noexcept {
			return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

	}

	std::map<std::string, std::string, std::less<>> parse_header_parameters(std::string_view value) {
		std::map<std::string, std::string, std::less<>> out;

		std::size_t pos = 0;

		while (pos < value.size()) {
			const std::size_t semi = value.find(';', pos);
			const std::string_view segment = trim(value.substr(pos, semi == std::string_view::npos ? std::string_view::npos : semi - pos));
			pos = (semi == std::string_view::npos) ? value.size() : semi + 1;

			if (segment.empty())
				continue;

			const std::size_t eq = segment.find('=');

			if (eq == std::string_view::npos)
				continue;

			const std::string_view key = trim(segment.substr(0, eq));
			std::string_view val = trim(segment.substr(eq + 1));

			if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
				val = val.substr(1, val.size() - 2);

			if (!key.empty())
				out.emplace(std::string(key), std::string(val));
		}

		return out;
	}

	const std::string* mime_type::parameter(std::string_view name) const noexcept {
		const auto it = parameters.find(name);
		return it == parameters.end() ? nullptr : &it->second;
	}

	mime_type mime_type::parse(std::string_view value) {
		mime_type out;

		const std::size_t semi = value.find(';');
		const std::string_view essence_part = trim(value.substr(0, semi));
		const std::string_view params_part = (semi == std::string_view::npos) ? std::string_view() : value.substr(semi + 1);

		const std::size_t slash = essence_part.find('/');

		if (slash == std::string_view::npos) {
			out.type = std::string(essence_part);
		}
		else {
			out.type = std::string(essence_part.substr(0, slash));
			out.subtype = std::string(essence_part.substr(slash + 1));
		}

		out.parameters = parse_header_parameters(params_part);
		return out;
	}

	std::string mime_type::to_string() const {
		std::string out = essence();

		for (const auto& [key, val] : parameters) {
			out += "; ";
			out += key;
			out += "=";
			out += val;
		}

		return out;
	}

	std::string_view mime_type_from_extension(std::string_view path) noexcept {
		struct entry { std::string_view ext; std::string_view mime; };

		static constexpr std::array<entry, 24> table{ {
			{ "html", mime_types::TEXT_HTML },
			{ "htm", mime_types::TEXT_HTML },
			{ "txt", mime_types::TEXT_PLAIN },
			{ "css", mime_types::TEXT_CSS },
			{ "csv", mime_types::TEXT_CSV },
			{ "json", mime_types::APPLICATION_JSON },
			{ "js", mime_types::APPLICATION_JAVASCRIPT },
			{ "mjs", mime_types::APPLICATION_JAVASCRIPT },
			{ "xml", mime_types::APPLICATION_XML },
			{ "pdf", mime_types::APPLICATION_PDF },
			{ "png", mime_types::IMAGE_PNG },
			{ "jpg", mime_types::IMAGE_JPEG },
			{ "jpeg", mime_types::IMAGE_JPEG },
			{ "gif", mime_types::IMAGE_GIF },
			{ "svg", mime_types::IMAGE_SVG },
			{ "ico", mime_types::IMAGE_ICON },
			{ "bin", mime_types::APPLICATION_OCTET_STREAM },
			{ "wasm", "application/wasm" },
			{ "woff", "font/woff" },
			{ "woff2", "font/woff2" },
			{ "mp4", "video/mp4" },
			{ "mp3", "audio/mpeg" },
			{ "wav", "audio/wav" },
			{ "zip", "application/zip" },
		} };

		const std::size_t last_slash = path.find_last_of('/');
		const std::string_view filename = last_slash == std::string_view::npos ? path : path.substr(last_slash + 1);
		const std::size_t dot = filename.find_last_of('.');

		if (dot == std::string_view::npos)
			return mime_types::APPLICATION_OCTET_STREAM;

		std::string ext(filename.substr(dot + 1));
		std::transform(ext.begin(), ext.end(), ext.begin(), to_lower_char);

		for (const entry& e : table) {
			if (e.ext == ext)
				return e.mime;
		}

		return mime_types::APPLICATION_OCTET_STREAM;
	}

}
