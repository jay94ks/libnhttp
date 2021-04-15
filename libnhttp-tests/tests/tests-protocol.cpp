#include "tests.hpp"
#include <nhttp/protocol/http_date.hpp>
#include <nhttp/protocol/http_header.hpp>
#include <nhttp/protocol/http_headerset.hpp>
#include <nhttp/protocol/http_method.hpp>
#include <nhttp/protocol/http_mime_type.hpp>
#include <nhttp/protocol/http_query_string.hpp>
#include <nhttp/protocol/http_resource.hpp>
#include <nhttp/protocol/http_status.hpp>

using namespace nhttp;

void test_protocol() {
	test_case label("protocol/*.hpp");
	
	http_date now;

	std::string s = now.stringify();

	if (http_date::try_parse(now, s.c_str(), s.size(), false) <= 0 ||
		s != now.stringify()) 
	{
		std::cout << " : failed to parse: " << s << "\n";
	}

	http_header header;
	std::string tmp;

	s.insert(0, "Date: ");
	s.append("\r\n");

	if (http_header::try_parse(header, s.c_str(), s.size()) <= 0 ||
		!header.stringify(tmp) || s != tmp)
	{
		std::cout << " : failed to parse: " << s << "\n";
	}

	http_headers headers;
	headers.vec.push_back(header);
	tmp.clear();

	headers.stringify(tmp);
	std::cout << tmp << "\n";

	headers.unset(http_header::DATE);
	if (headers.find_one(http_header::DATE) != headers.vec.end()) {
		std::cout << " : failed to unset: " << s << "\n";
	}

	headers.set(http_header::DATE, now.stringify());
	const auto* p = headers.get(http_header::DATE);

	if (!p || nhttp::stricmp(p, now.stringify().c_str())) {
		std::cout << " : failed to set: " << s << "\n";
	}

	http_resource res;
	tmp = "GET /something HTTP/1.1\r\n";

	if (http_resource::try_parse(res, tmp.c_str(), tmp.size()) <= 0 ||
		res.get_method() != http_method::GET || res.get_path() != "/something" ||
		res.get_major_ver() != 1 || res.get_minor_ver() != 1 ||
		res.stringify() != tmp)
	{
		std::cout << " : failed to parse: " << tmp << "\n";
	}

	http_mime_type mime;

	if (http_mime_type::try_parse(mime, "text/html; charset=UTF-8") < 0 ||
		mime.stringify() != "text/html; charset=UTF-8")
	{
		std::cout << " : failed to parse: text/html; charset=UTF-8\n";
	}

	http_query_string qs;
	http_query_string::try_parse(qs, "a=b&c=d");

	if (!qs.has("a") || !qs.has("c") ||
		nhttp::strnicmp(qs.get("a"), "b", 1) ||
		nhttp::strnicmp(qs.get("a"), "b", 1) ||
		qs.stringify() != "a=b&c=d")
	{
		std::cout << " : failed to parse: a=b&c=d\n";
	}
	
	qs.set("e", "f");
	if (qs.stringify() != "a=b&c=d&e=f") {
		std::cout << " : failed to set: e=f, " << qs.stringify() << "\n";
	}

	http_status stats = http_status::_200;
	tmp.clear();

	if (!stats.stringify(tmp) ||
		tmp != "HTTP/1.1 200 OK\r\n" ||
		stats.try_parse(stats, tmp.c_str(), tmp.size()) <= 0)
	{
		std::cout << " : failed to parse: " << tmp << "\n";
	}
}