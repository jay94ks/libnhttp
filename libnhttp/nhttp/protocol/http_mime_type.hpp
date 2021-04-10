#pragma once
#include "../types.hpp"
#include "../utils/strings.hpp"

namespace nhttp {
namespace _ {
	struct well_known_mime_type_t {
		const char* _1, *_2;
		size_t _len_1, _len_2;

		template<size_t size_1, size_t size_2>
		constexpr well_known_mime_type_t(const char(&cat)[size_1], const char (&det)[size_2])
			: _1(cat), _2(det), _len_1(size_1 - 1), _len_2(size_2 - 1) { }
			/*
		inline http_mime_type option(std::string k, std::string v) const {
			return http_mime_type(*this).option(std::move(k), std::move(v));
		}*/
	};
}

	/**
	 * class http_mime_type.
	 * represents http mime type: category/detail[; options...].
	 */
	class NHTTP_API http_mime_type {
	private:
		std::string category;
		std::string detail;

	public:
		std::map<std::string, std::string> options;

	public:
		using well_known_t = _::well_known_mime_type_t;

		/* hard-coded text/* types. */
		static constexpr well_known_t TEXT_PLAIN				= well_known_t("text", "plain");
		static constexpr well_known_t TEXT_HTML					= well_known_t("text", "html");
		static constexpr well_known_t TEXT_JS					= well_known_t("text", "js");
		static constexpr well_known_t TEXT_CSS					= well_known_t("text", "css");
		
		/* hard-coded image/* types. */
		static constexpr well_known_t IMAGE_ICO					= well_known_t("image", "ico");
		static constexpr well_known_t IMAGE_BMP					= well_known_t("image", "bmp");
		static constexpr well_known_t IMAGE_JPG					= well_known_t("image", "jpg");
		static constexpr well_known_t IMAGE_PNG					= well_known_t("image", "png");
		static constexpr well_known_t IMAGE_GIF					= well_known_t("image", "gif");

		/* hard-coded video/* types. */
		static constexpr well_known_t VIDEO_MP4					= well_known_t("video", "mp4");
		static constexpr well_known_t VIDEO_OGG					= well_known_t("video", "ogg");
		static constexpr well_known_t VIDEO_3GPP				= well_known_t("video", "3gpp");
		static constexpr well_known_t VIDEO_MPEG				= well_known_t("video", "mpeg");
		static constexpr well_known_t VIDEO_WEBM				= well_known_t("video", "webm");
		static constexpr well_known_t VIDEO_QUICKTIME			= well_known_t("video", "quicktime");
		
		static constexpr well_known_t VIDEO_MS_ASF				= well_known_t("video", "ms-asf");
		static constexpr well_known_t VIDEO_X_MS_WMV			= well_known_t("video", "x-ms-wmv");
		static constexpr well_known_t VIDEO_X_MSVIDEO			= well_known_t("video", "x-msvideo");

		/* hard-coded application/* types. */
		static constexpr well_known_t APPLICATION_OCTET			= well_known_t("application", "octet-stream");
		static constexpr well_known_t APPLICATION_JSON			= well_known_t("application", "json");
		static constexpr well_known_t APPLICATION_XML			= well_known_t("application", "xml");
		static constexpr well_known_t APPLICATION_URLENCODED	= well_known_t("application", "x-www-form-urlencoded");
		static constexpr well_known_t APPLICATION_X_MPEGURL		= well_known_t("application", "x-mpegurl");
		
		/* hard-coded multipart/* types. */
		static constexpr well_known_t MULTIPART_FORMDATA		= well_known_t("multipart", "form-data");
		static constexpr well_known_t MULTIPART_BYTERANGES		= well_known_t("multipart", "byteranges");

	public:
		http_mime_type() : category("application"), detail("octet-stream") { }
		http_mime_type(const well_known_t& w) 
			: category(w._1, w._len_1), detail(w._2, w._len_2) { }

		http_mime_type(const char* cat, const char* det)
			: category(cat), detail(det) { }

		static bool get_from_extension(http_mime_type& out, const char* path, ssize_t len = -1);

	public:
		inline bool operator !=(const well_known_t& w) const { return !(*this == w); }
		inline bool operator ==(const well_known_t& w) const {
			return category.size() == w._len_1 && detail.size() == w._len_2 &&
				   category == w._1 && detail == w._2;
		}

	public:
		inline const std::string& get_category() const { return category; }
		inline const std::string& get_detail() const { return detail; }

		/**
		 * returns charset in string pointer.
		 * if not set, returns nullptr.
		 */
		inline const char* get_charset() const { return get_option("charset"); }
		
		/**
		 * returns option value in string pointer.
		 * if not set, returns nullptr.
		 */
		inline const char* get_option(const char* option) const {
			for (const auto& each : options) {
				if (!stricmp(each.first.c_str(), option))
					return   each.second.c_str();
			}

			return nullptr;
		}

		inline http_mime_type& option(std::string key, std::string value) {
			options.emplace(std::move(key), std::move(value));
			return *this;
		}

	public:
		/**
		 * parse mime-type from http header.
		 * @param by_receiving for parsing resource header in receiving loop.
		 * @returns
		 *  1. =  0: success.
		 *  2. = -1: invalid string.
		 */
		static int32_t try_parse(http_mime_type& mime_type, const std::string& src);
		
		/**
		 * stringify mime-type for http header.
		 * @returns false if method not set.
		 */
		bool stringify(std::string& out_string) const;

		inline std::string stringify() const {
			std::string ret;
			stringify(ret);
			return ret;
		}
	};

}