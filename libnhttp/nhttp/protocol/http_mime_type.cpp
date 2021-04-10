#include "http_mime_type.hpp"

namespace nhttp {
	struct hardcoded_ext_mime_t {
		const char*						ext;
		size_t							len;
		http_mime_type::well_known_t	mime;

		template<size_t size>
		constexpr hardcoded_ext_mime_t(const char(&ext)[size], http_mime_type::well_known_t mime)
			: ext(ext), len(size - 1), mime(mime) { }
	};

	constexpr hardcoded_ext_mime_t HARDCODED_EXT_MIMES[] = {
		hardcoded_ext_mime_t("htm",	 http_mime_type::TEXT_HTML),
		hardcoded_ext_mime_t("html", http_mime_type::TEXT_HTML),
		hardcoded_ext_mime_t("js",	 http_mime_type::TEXT_JS),
		hardcoded_ext_mime_t("css",	 http_mime_type::TEXT_CSS),
		hardcoded_ext_mime_t("txt",	 http_mime_type::TEXT_PLAIN),

		hardcoded_ext_mime_t("c",	 http_mime_type::TEXT_PLAIN),
		hardcoded_ext_mime_t("cpp",	 http_mime_type::TEXT_PLAIN),
		hardcoded_ext_mime_t("hpp",	 http_mime_type::TEXT_PLAIN),
		hardcoded_ext_mime_t("h",	 http_mime_type::TEXT_PLAIN),

		hardcoded_ext_mime_t("json", http_mime_type::APPLICATION_JSON),
		hardcoded_ext_mime_t("xml",	 http_mime_type::APPLICATION_XML),

		hardcoded_ext_mime_t("jpg",  http_mime_type::IMAGE_JPG),
		hardcoded_ext_mime_t("jpeg", http_mime_type::IMAGE_JPG),
		hardcoded_ext_mime_t("png",  http_mime_type::IMAGE_PNG),
		hardcoded_ext_mime_t("gif",  http_mime_type::IMAGE_GIF),
		hardcoded_ext_mime_t("bmp",  http_mime_type::IMAGE_BMP),
		hardcoded_ext_mime_t("ico",  http_mime_type::IMAGE_ICO),

		hardcoded_ext_mime_t("m3u",  http_mime_type::APPLICATION_X_MPEGURL),
		hardcoded_ext_mime_t("m3u8", http_mime_type::APPLICATION_X_MPEGURL),

		hardcoded_ext_mime_t("3gp",  http_mime_type::VIDEO_3GPP),
		hardcoded_ext_mime_t("m1v",  http_mime_type::VIDEO_MPEG),

		hardcoded_ext_mime_t("ogg",  http_mime_type::VIDEO_OGG),

		hardcoded_ext_mime_t("mp4",  http_mime_type::VIDEO_MP4),
		hardcoded_ext_mime_t("m4a",  http_mime_type::VIDEO_MP4),
		hardcoded_ext_mime_t("m4p",  http_mime_type::VIDEO_MP4),
		hardcoded_ext_mime_t("m4b",  http_mime_type::VIDEO_MP4),
		hardcoded_ext_mime_t("m4r",  http_mime_type::VIDEO_MP4),
		hardcoded_ext_mime_t("m4v",  http_mime_type::VIDEO_MP4),

		hardcoded_ext_mime_t("asf",  http_mime_type::VIDEO_MS_ASF),
		hardcoded_ext_mime_t("wma",  http_mime_type::VIDEO_MS_ASF),

		hardcoded_ext_mime_t("wmv",  http_mime_type::VIDEO_X_MS_WMV),
		hardcoded_ext_mime_t("avi",  http_mime_type::VIDEO_X_MSVIDEO),

		hardcoded_ext_mime_t("mov",  http_mime_type::VIDEO_QUICKTIME),
		hardcoded_ext_mime_t("qt",   http_mime_type::VIDEO_QUICKTIME),
		hardcoded_ext_mime_t("webm", http_mime_type::VIDEO_WEBM)
	};

	constexpr size_t HARDCODED_EXT_MIMES_MAX = sizeof(HARDCODED_EXT_MIMES) / sizeof(hardcoded_ext_mime_t);

	bool http_mime_type::get_from_extension(http_mime_type& out, const char* path, ssize_t len) {
		if (len < 0) {
			len = strlen(path);
		}

		const char* path_s = path;
		const char* path_e = path + len;

		if (memchr(path, '.', len)) {
			while (path_s < path_e && *path_e != '.') { --path_e; }
			++path_e;
		}

		if (path_s >= path_e) 
			return false;

		for (size_t i = 0; i < HARDCODED_EXT_MIMES_MAX; ++i) {
			if (!strnicmp(path_e, HARDCODED_EXT_MIMES[i].ext, HARDCODED_EXT_MIMES[i].len)) {
				out = HARDCODED_EXT_MIMES[i].mime;
				return true;
			}
		}

		return false;
	}

	int32_t http_mime_type::try_parse(http_mime_type& mime_type, const std::string& src) {
		/**
		* category/sub-type; key=value; key=value...
		*/

		struct nstring {
			const char* begin;
			size_t		len;
		};

		size_t buf_i = 0;
		nstring buf[64] = { 0, };

		const char* src_b = src.c_str();
		const char* src_e = src_b + src.size();

		while (src_b < src_e) {
			const char* semi = (const char*)memchr(src_b, ';', size_t(src_e - src_b));

			if (!semi) {
				if (buf_i >= 64) break;
				buf[buf_i].begin = src_b;
				buf[buf_i].len = size_t(src_e - src_b);

				buf_i++;
				break;
			}

			if (buf_i >= 64) break;
			buf[buf_i].begin = src_b;
			buf[buf_i].len = size_t(semi - src_b);
			buf_i++;

			src_b = semi + 1;
		}

		for (size_t i = 0; i < buf_i; ++i) {
			auto& item = buf[i];

			while (item.len && (
				*item.begin == ' ' ||
				*item.begin == '\t'))
			{
				++item.begin;
				--item.len;
			}
		}

		if (buf_i <= 0 || !buf[0].len)
			return -1;

		const char* mime_b = buf[0].begin;
		const char* mime_s = (const char*)memchr(mime_b, '/', buf[0].len);

		/**
		* parse mime-type.
		* 1. type, type/   -> type only.
		* 2. type/sub-type -> both.
		* 3. /sub-type     -> sub-type as type.
		*/
		if (!mime_s || mime_b == mime_s) {
			if (mime_b == mime_s) {
				++mime_b; --buf[0].len;
			}

			if (!buf[0].len) return -1;
			mime_type.category.append(mime_b, buf[0].len);
		}

		else {
			const char* mime_n = mime_s + 1;
			const char* mime_e = mime_b + buf[0].len;

			while (mime_b <  mime_s && (*(mime_s - 1) == ' ' || *(mime_s - 1) == '\t')) { --mime_s; }
			while (mime_n <  mime_e && (*(mime_n) == ' ' || *(mime_n) == '\t')) { ++mime_n; }
			if (mime_b == mime_s && mime_n == mime_e) return -1;

			if (mime_b == mime_s) {
				mime_b = mime_n; mime_s = mime_e;
				mime_n = mime_e = nullptr;
			}

			mime_type.category.append(mime_b, size_t(mime_s - mime_b));
			if (mime_n < mime_e) {
				mime_type.detail.append(mime_n, size_t(mime_e - mime_n));
			}
		}

		for (size_t i = 1; i < buf_i; ++i) {
			if (!buf[i].begin || !buf[i].len)
				continue;

			const char* kv_b = buf[i].begin;
			const char* kv_e = kv_b + buf[i].len;

			const char* eq = (const char*)memchr(kv_b, '=', size_t(kv_e - kv_b));

			if (!eq) {
				mime_type.options.emplace(
					std::string(kv_b, buf[i].len),
					std::string());
			}

			else {
				const char* kv_k = eq, * kv_v = eq + 1;

				while (kv_b <  kv_k && (*(kv_k - 1) == ' ' || *(kv_k - 1) == '\t')) { --kv_k; }
				if (kv_b == kv_k) continue;
				while (kv_v <  kv_e && (*kv_v == ' ' || *kv_v == '\t')) { ++kv_v; }

				std::string key(kv_b, size_t(kv_k - kv_b));

				if (kv_v == kv_e)
					mime_type.options.emplace(std::move(key), std::string());
				else mime_type.options.emplace(std::move(key), std::string(kv_v, size_t(kv_e - kv_v)));
			}
		}

		return 0;
	}

	bool http_mime_type::stringify(std::string& out_string) const {
		if (!category.size() && !detail.size())
			return false;

		if (category.size()) {
			out_string.append(category);

			if (detail.size())
				out_string.push_back('/');
		}

		if (detail.size())
			out_string.append(detail);

		for (const auto& kv : options) {
			if (!kv.first.size())
				continue;

			out_string.append("; ", 2);
			out_string.append(kv.first);
			if (kv.second.size()) {
				out_string.push_back('=');
				out_string.append(kv.second);
			}
		}

		return true;
	}

}