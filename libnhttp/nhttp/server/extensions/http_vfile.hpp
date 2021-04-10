#pragma once
#include "http_extension.hpp"
#include "../http_context.hpp"
#include "../../protocol/http_mime_type.hpp"

namespace nhttp {
	class stream;

namespace server {
	class http_overlay;

	/**
	 * class http_vfile.
	 * provides a static file content for all access to vpath/vhost.
	 */
	class NHTTP_API http_vfile : public http_extension {
		friend class http_overlay;

	private:
		hal::spinlock_t spinlock;

		std::string path_to;
		std::string mime_type;

		/**
		 * file size:
		 *   <  0: non-exists or permission denied.
		 *   >= 0: exists and accessible.
		 */
		ssize_t file_size;
		time_t file_mtime;

		std::string http_mtime;
		std::string http_etag;

	public:
		http_vfile(std::string path_to, const http_mime_type& mime);
		virtual ~http_vfile() { }

	protected:
		/* generate http etag and mtime from given name and mtime. */
		static void generate_http_values(std::string& out_etag,
			std::string& out_mtime, const std::string& name, time_t mtime);

	private:
		ssize_t refresh();

	protected:
		virtual bool on_collect(std::shared_ptr<http_context> context) { return true; }

	private:
		virtual bool on_handle(std::shared_ptr<http_context> context);

	protected:
		/* generates http response which support range and cache headers. */
		static void on_handle(http_request_ptr& request, http_response_ptr& response, 
			const std::shared_ptr<stream>& stream, ssize_t file_size, time_t file_mtime, 
			const std::string& http_etag, const std::string& http_mtime, const std::string& mime_type);
	};

}
}