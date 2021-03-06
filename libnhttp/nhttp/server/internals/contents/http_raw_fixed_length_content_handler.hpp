#pragma once
#include "http_raw_content_handler.hpp"

namespace nhttp {
namespace server {

	/**
	 * class http_raw_fixed_len_content_handler.
	 * fixed-length content.
	 */
	class NHTTP_API http_raw_fixed_len_content_handler
		: public http_raw_content_handler
	{
	public:
		struct {
			int8_t read_more : 1;
			
			int64_t cont_left;
			int64_t cont_read;
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