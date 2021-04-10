#include "http_vfile.hpp"
#include "http_overlay.hpp"
#include "http_vpath.hpp"
#include "../http_context.hpp"
#include "../../utils/strings.hpp"
#include "../../io/file_stream.hpp"
#include "../../io/range_stream.hpp"
#include "../../protocol/http_mime_type.hpp"
#include <sys/stat.h>

namespace nhttp {
namespace server {
    http_vfile::http_vfile(std::string path_to, const http_mime_type& mime)
	    : http_extension(0xf0001001), path_to(path_to)
    {
        mime_type = mime.stringify();
    }

    void http_vfile::generate_http_values(std::string& out_etag, std::string& out_mtime, const std::string& name, time_t mtime) {
        uint32_t fn_hash = hash_djb(name.c_str(), name.size());

        out_etag.clear();
        out_etag.reserve(21);
        out_etag.append("\"NH-");

        to_hex32(out_etag, fn_hash, true);
        to_hex32(out_etag, uint32_t(mtime), true);
        out_etag.push_back('\"');

        out_mtime = http_date(mtime).stringify();
    }

    ssize_t http_vfile::refresh() {
        struct stat _stat_f;

        if (stat(path_to.c_str(), &_stat_f) ||
            (_stat_f.st_mode & S_IFREG) != S_IFREG)
        {
            file_size = -1;
            return -1;
        }

        if (file_mtime != _stat_f.st_mtime) {
            /* generate etag and mtime header. */
            generate_http_values(http_etag, http_mtime, 
                path_to, file_mtime = _stat_f.st_mtime);
        }

        return file_size = _stat_f.st_size;
    }

    bool http_vfile::on_handle(std::shared_ptr<http_context> context) {
        spinlock.lock();
        ssize_t file_size = refresh();
        time_t file_mtime = this->file_mtime;
        std::string http_etag = this->http_etag;
        std::string http_mtime = this->http_mtime;
        spinlock.unlock();

        /* reset response object. */
        context->response = make_response(200);

        if (file_size < 0) {
            context->response->status.set(404);
            context->close();
            return true;
        }

        std::shared_ptr<file_stream> stream
            = std::make_shared<file_stream>(path_to.c_str(), "rb");

        if (!stream->is_valid()) {
            context->response->status.set(403);
            context->close();
            return true;
        }

        context->response = make_response(200);
        on_handle(context->request, context->response,
            stream, file_size, file_mtime, http_etag, http_mtime, mime_type);

        context->close();
        return true;
    }

    void http_vfile::on_handle(http_request_ptr& request, http_response_ptr& response,
        const std::shared_ptr<stream>& stream, ssize_t file_size, time_t file_mtime,
        const std::string& http_etag, const std::string& http_mtime, const std::string& mime_type)
    {
        time_t c_mtime = 0; // --> client side modified time.
        bool range_miss = false;

        const auto& request_headers = request->get_headers();
        const auto& content_type = request_headers.find_one(http_header::CONTENT_TYPE);
        auto& response_headers = response->headers;

        if (content_type != request_headers.vec.end()) {
            http_mime_type mime_type;

            if (!http_mime_type::try_parse(mime_type, content_type->get_value())) {
                if (mime_type == http_mime_type::MULTIPART_BYTERANGES) {
                    response->status.set(400);
                    return;
                }
            }
        }

        const char* if_match = ltrim(request_headers.get(http_header::IF_MATCH));
        const char* if_none_match = ltrim(request_headers.get(http_header::IF_NONE_MATCH));
        const char* if_modified_since = ltrim(request_headers.get(http_header::IF_MODIFIED_SINCE));
        const char* if_range = ltrim(request_headers.get(http_header::IF_RANGE));

        /* set necessary headers. */
        response_headers.set(http_header::ACCEPT_RANGES, "bytes");
        response_headers.set(http_header::ETAG, http_etag);
        response_headers.set(http_header::LAST_MODIFIED, http_mtime);

		/* set cache-control. */
		if ( request_headers.isset(http_header::AUTHORIZATION))
			 response_headers.set(http_header::CACHE_CONTROL, "private, must-revalidate");
		else response_headers.set(http_header::CACHE_CONTROL, "public, must-revalidate");

        /* test If-Match header. */
        if (if_match && strnicmp(if_match, http_etag.c_str(), http_etag.size())) {
            response->status.set(412); // 412 Precondition Failed.
            return;
        }

        /* parse If-Modified-Since header. */
        if (if_modified_since) {
            http_date temp;

            if (http_date::try_parse(temp, if_modified_since, -1, false))
                c_mtime = temp.get_timestamp();
        }

        /* check client side E-Tag and m-time. */
        if ((if_none_match && !strnicmp(if_none_match, http_etag.c_str(), http_etag.size())) ||
            (if_modified_since && c_mtime >= file_mtime))
        {
            response->status.set(304); // 304 Not Modified.
            return;
        }

        /* check range request's E-Tag. */
        if (if_range && strnicmp(if_range, http_etag.c_str(), http_etag.size())) {
            size_t ir_len = strlen(if_range);
            range_miss = true;

            /* if timestamp set, compare timestamp instead of ETag. */
            if (!strnicmp(if_range + (ir_len - 3), "GMT", 3)) {
                http_date temp;

                if (http_date::try_parse(temp, if_range, -1, false)) {
                    range_miss = temp.get_timestamp() < file_mtime;
                }
            }
        }

        /* if request is by HEAD method, send no content. */
        if (request->get_target().get_method() == http_method::HEAD) {
            response_headers.set(http_header::CONTENT_LENGTH, std::to_string(file_size));
            return;
        }

        if (request_headers.isset(http_header::RANGE)) {
            const auto& range = request_headers.find_one(http_header::RANGE)->get_value();
            const char* key_e = (const char*)memchr(range.c_str(), '=', range.size());

            if (!key_e) {
                response->status.set(416); // 416 Requested Range Not Satisfied.
                return;
            }

            const char* key_s = ltrim(range.c_str(), range.size());
            const char* val_s = ltrim(key_e + 1, range.size() - size_t(range.c_str() - key_e - 1));
            const char* val_e = range.c_str() + range.size();

            const char* sep_m = (const char*)memchr(val_s, '-', size_t(val_e - val_s));

            if (!sep_m || strnicmp(key_s, "bytes", size_t(key_e - key_s))) {
                response->status.set(416); // 416 Requested Range Not Satisfied.
                return;
            }

            const char* val_m = ltrim(sep_m + 1, size_t(val_e - sep_m - 1));

            ssize_t stream_size = stream->get_length();
            ssize_t range_begin = to_int64(val_s, 10, size_t(sep_m - val_s));
            ssize_t range_end = 0;

            /* if range-end is specified, */
            if (size_t(val_e - sep_m - 1) > 0)
                range_end = to_int64(val_m, 10, size_t(val_e - val_m)) + 1;

            /*else if (stream_size - range_begin >= 64 * 1024 * 1024)
                range_end = range_begin + 64 * 1024 * 1024; */
            else range_end = stream_size;

            /* test range is valid or not. */
            if (range_begin > range_end || range_begin == range_end ||
                range_begin > stream_size || range_end > stream_size)
            {
                response->status.set(416); // 416 Requested Range Not Satisfied.
                return;
            }

            std::string range_is = "bytes ";

            range_is.append(std::to_string(range_begin));
            range_is.push_back('-');
            range_is.append(std::to_string(range_end - 1));
            range_is.push_back('/');
            range_is.append(std::to_string(stream_size));

            response_headers.set(http_header::CONTENT_RANGE, range_is);
            response_headers.set(http_header::CONTENT_TYPE, mime_type);

            response->status.set(206); // 206 Partial Content.
            response->content = std::make_shared<range_stream>(stream, range_begin, range_end);
            return;
        }

        response_headers.set(http_header::CONTENT_TYPE, mime_type);
        response->content = stream;
        return;
    }

}
}