#include "http_overlay.hpp"
#include "http_vpath.hpp"
#include "http_vfile.hpp"
#include "../http_context.hpp"
#include "../../utils/path.hpp"
#include "../../io/file_stream.hpp"
#include "../../io/range_stream.hpp"
#include "../../protocol/http_mime_type.hpp"
#include <sys/stat.h>

namespace nhttp {
namespace server {
	http_overlay::http_overlay(std::string base_dir)
		: http_extension(0xf0010000), base_dir(std::move(base_dir))
	{
	}

	http_overlay::http_overlay(std::string base_dir, std::string index_file)
		: http_overlay(base_dir)
	{
		this->index_file = std::move(index_file);
	}

	bool http_overlay::on_handle(std::shared_ptr<http_context> context) {
		const std::string& path_to = subpath_of(context);
		std::string target_path = base_dir + "/" + path_to;
		
		/**
		 * access to base_path itself,
		 * redirect it to index file.
		 */

		if (target_path.size() == base_dir.size() + 1 && index_file.size()) {
			target_path.append(index_file);
		}

		/* verify a target file is existed or not. */
		struct stat _stat_f;
		if (stat(target_path.c_str(), &_stat_f) ||
			(_stat_f.st_mode & S_IFREG) != S_IFREG)
			return false;

		if (predicate && !predicate(context, path_to))
			return false;

		std::shared_ptr<file_stream> stream
			= std::make_shared<file_stream>(target_path.c_str(), "rb");

		if (!stream->is_valid()) {
			return false;
		}

		/* -- then generate http fundamental values... -- */
		http_mime_type mime;

		if (!http_mime_type::get_from_extension(mime, target_path.c_str(), target_path.size()))
			mime = http_mime_type::APPLICATION_OCTET;
			
		std::string http_mtime, http_etag;
		size_t file_size = _stat_f.st_size;
		time_t file_mtime = _stat_f.st_mtime;

		/* reset response object and generate mtime and etag. */
		context->response = make_response(200);
		http_vfile::generate_http_values(http_etag, http_mtime, path_to, file_mtime);

		/* provide file. */
		http_vfile::on_handle(context->request, context->response,
			stream, file_size, file_mtime, http_etag, http_mtime, mime.stringify());

		context->close();
		return true;
	}

}
}