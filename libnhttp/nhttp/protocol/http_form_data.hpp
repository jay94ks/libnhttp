#pragma once
#include "../types.hpp"
#include "../nvalue.hpp"
#include "../io/stream.hpp"
#include "http_query_string.hpp"

namespace nhttp {

	class NHTTP_API http_form_file {
	public:
		std::string				name;
		size_t					size;
		std::shared_ptr<stream>	content;
	};

	/**
	 * class http_form_data.
	 * represents form-data from request body.
	 */
	class NHTTP_API http_form_data {
	public:
		nvalue<nval::map> kv;
		std::map<std::string, http_form_file> files;

	public:
		/**
		 * parse `urlencoded` data from http content.
		 * @returns
		 *	1. =  0: success.
		 */
		//static int32_t try_parse_urlencoded(http_form_data& out, 
		//			  const std::shared_ptr<http_content>& content);
	};

}