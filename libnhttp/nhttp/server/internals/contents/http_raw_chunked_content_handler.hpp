#pragma once
#include "../http_raw_content_handler.hpp"

namespace nhttp {
namespace server {

	class NHTTP_API http_raw_chunked_content_handler
		: public http_raw_content_handler
	{
	public:
		std::vector<char> line_buf;

		struct {
			int8_t read_more : 1;
			int8_t cont_phase : 3; /* 0: header, 1: body, 2: body-end */
			uint8_t cont_end : 1;
			ssize_t found_lf;

			uint64_t cont_left;
			uint64_t cont_read;
			uint64_t cont_notf;
			int64_t cont_mark;
		} state = { 0, };

	public:
		/* initiate content handler. */
		virtual void on_initiate() override;

		/* finalize and cleanup content handler. */
		virtual void on_finalize() override;

		/* handle socket events for feeding content. */
		virtual int32_t on_event(socket_t& socket) override;
	};

}
}