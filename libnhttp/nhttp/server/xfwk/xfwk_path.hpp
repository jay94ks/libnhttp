#pragma once
#include "../../types.hpp"
#include "../../utils/path.hpp"

namespace nhttp {
namespace server {
namespace xfwk {

	/**
	 * class xfwk_path.
	 * path object which stores each names splitten by slash.
	 */
	class NHTTP_API xfwk_path {
	private:
		struct substr_t {
			size_t offset;
			size_t length;
		};

		std::string original;
		std::vector<substr_t> names;

	public:
		xfwk_path() { }
		xfwk_path(const std::string& path)
			: original(qualify_path(path))
		{
			const char* path_b = original.c_str(),
					  * path_e = path_b + original.size(),
					  * path_c = path_b;

			while (path_c < path_e) {
				const char* slash = (const char*)memchr(
					path_c, '/', size_t(path_e - path_c));

				if (!slash) {
					slash = path_e;
				}

				names.push_back({
					size_t(path_c - path_b),
					size_t(slash - path_c)
				});

				path_c = slash + 1;
			}
		}

	public:
		/* get count of names. */
		inline size_t get_size() const { return names.size(); }

		/* get oritinal path string. */
		inline const std::string& get_path() const { return original; }

		/* pop name from front. */
		xfwk_path& pop_front() {
			if (names.size()) {
				substr_t s = names[0];

				char* src_b = &original[0];
				char* src_m = src_b + s.length;
				char* src_e = src_b + original.size();

				if (src_m == src_e) {
					original.clear();
					names.clear();
					return *this;
				}

				if (names.size() > 1)
					++src_m;

				size_t diff_len = size_t(src_m - src_b);
				size_t new_len = size_t(src_e - src_m);

				memmove(src_b, src_m, new_len);
				original.erase(original.begin() + new_len, original.end());

				for (size_t i = 0; i < names.size(); ++i) {
					names[i].offset -= diff_len;
				}

				names.erase(names.begin());
			}

			return *this;
		}

		/* pop name from back. */
		inline xfwk_path& pop_back() {
			if (names.size()) {
				size_t offset = names.back().offset;
				names.pop_back();

				if (offset)
					offset--;

				original.erase(original.begin() + offset, original.end());
			}

			return *this;
		}

		/* get sub-path after index. */
		inline xfwk_path get_subpath(size_t i, ssize_t n = -1) const {
			xfwk_path subpath;
			get_subpath(subpath, i, n);
			return subpath;
		}

		/* get sub-path after index. */
		inline bool get_subpath(xfwk_path& out, size_t i, ssize_t n = -1) const {
			if (names.size() <= i) {
				return false;
			}

			if (n < 0)
				n = names.size() - i;

			xfwk_path new_one;
			size_t len = n - 1;

			for (size_t c = i; i < i + n; ++c) {
				len += names[i].length;
			}

			out.original.reserve(out.original.size() + len);
			for (size_t c = i; i < i + n; ++c) {
				const auto& each = names[i];

				if (out.original.size())
					out.original.push_back('/');

				size_t offset = out.original.size();
				out.original.append(original.c_str() + each.offset, each.length);
				out.names.push_back({ offset, each.length });
			}

			return true;
		}

		/* get name at index. */
		inline std::string get_name_at(size_t i) const {
			if (names.size() <= i)
				return std::string();

			return std::string(original.c_str() + names[i].offset, names[i].length);
		}

		/* get name at index. */
		inline bool get_name_at(std::string& out, size_t i) const {
			if (names.size() <= i)
				return false;
			
			out.append(original.c_str() + names[i].offset, names[i].length);
			return true;
		}
	};

}
}
}