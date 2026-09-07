#pragma once

#include <string>
#include <string_view>

namespace nhttp::protocol {

	/* application/x-www-form-urlencoded decode: '+' -> space, "%XX" -> byte. */
	std::string url_decode(std::string_view input);

	/* application/x-www-form-urlencoded encode: space -> '+', reserved bytes -> "%XX". */
	std::string url_encode(std::string_view input);

}
