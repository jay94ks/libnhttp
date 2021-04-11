#pragma once
#include "../types.hpp"
#include "../utils/strings.hpp"

namespace nhttp {

	inline std::string basepath(const std::string& in) {
		const char* in_b = in.c_str();
		const char* in_e = in_b + in.size();

		if (in_b == in_e)
			return std::string();

		/* rtrim '/'. */
		while(in_b < in_e && *(--in_e) == '/');
		
		++in_e;

		while (in_b < in_e && *in_e != '/')	--in_e;
		if (*in_e == '/')					--in_e;

		if (in_b < in_e)
			return std::string(in_b, size_t(in_e - in_b));

		return std::string();
	}

	inline std::string basename(const std::string& in) {
		const char* in_b = in.c_str();
		const char* in_e = in_b + in.size();

		if (in_b == in_e)
			return std::string();

		const char* in_c = in_e - 1;
		while (in_b < in_c && in_c < in_e) {
			if (*in_c != '/')
			   --in_c;

			else break;
		}

		if (in_c < in_e)
			return std::string(in_c, size_t(in_e - in_c));

		return std::string();
	}

	inline std::string qualify_path(const std::string& in) {
		std::vector<nstring> names;

		const char* in_b = in.c_str();
		const char* in_e = in_b + in.size();
		size_t slash = 0, uses = 0;

		while(in_b < in_e && *in_b == '/') ++in_b;
		while(in_b < in_e && *(in_e - 1) == '/') --in_e;

		if (in_b >= in_e)
			return std::string();

		for (auto i = in_b; i < in_e; ++i) {
			if (*i == '/') ++slash;
		}

		if (!slash)
			return std::string(in_b, size_t(in_e - in_b));

		names.resize(slash + 1);
		while (in_b < in_e) {
			const char* sep = (const char*) memchr(in_b, '/', size_t(in_e - in_b));

			if (!sep) {
				if (in_b == in_e)
					break;

				names[uses].string = in_b;
				names[uses].length = size_t(in_e - in_b);
				++uses;
				break;
			}

			/* back to prev: destack. */
			if (!strncmp(in_b, "..", size_t(sep - in_b))) {
				uses = uses > 0 ? uses - 1 : 0;
				in_b = sep + 1;
				continue;
			}

			/* empty or self: skip. */
			else if (in_b == sep - 1 || !strncmp(in_b, ".", size_t(sep - in_b))) {
				in_b = sep + 1;
				continue;
			}

			size_t i = uses++;
			names[i].string = in_b;
			names[i].length = size_t(sep - in_b);

			in_b = sep + 1;
		}

		if (!uses)
			return std::string();
		
		std::string out;
		size_t total = 0;

		for (size_t i = 0; i < uses; ++i) {
			total += names[i].length;
		}

		out.reserve(total + (uses - 1));
		for (size_t i = 0; i < uses; ++i) {
			if (i) out.push_back('/');
			out.append(names[i].string, names[i].length);
		}

		return out;
	}

}