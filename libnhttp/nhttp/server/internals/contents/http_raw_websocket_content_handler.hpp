#pragma once
#include "http_raw_content_handler.hpp"

namespace nhttp {
namespace server {

	/**
	 *	0               1               2               3
	 *	0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
	 *	+-+-+-+-+-------+-+-------------+-------------------------------+
	 *	|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
	 *	|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
	 *	|N|V|V|V|       |S|             |   (if payload len==126/127)   |
	 *	| |1|2|3|       |K|             |                               |
	 *	+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
	 *	4               5               6               7
	 *	+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
	 *	|     Extended payload length continued, if payload len == 127  |
	 *	+ - - - - - - - - - - - - - - - +-------------------------------+
	 *	8               9               10              11
	 *	+ - - - - - - - - - - - - - - - +-------------------------------+
	 *	|                               |Masking-key, if MASK set to 1  |
	 *	+-------------------------------+-------------------------------+
	 * +-------------------------------+-------------------------------+
	 * 12              13              14              15
	 *	+-------------------------------+-------------------------------+
	 *	| Masking-key (continued)       |          Payload Data         |
	 *	+-------------------------------- - - - - - - - - - - - - - - - +
	 *	:                     Payload Data continued ...                :
	 *	+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
	 *	|                     Payload Data continued ...                |
	 *	+---------------------------------------------------------------+
	 */

#pragma pack(push, 1)
	union hws_frame_hdr {
		uint8_t packed_bytes[2];
		struct {
#if NHTTP_LITTLE_ENDIAN
			uint8_t len : 7;
			uint8_t msk : 1;
			uint8_t opc : 4;
			uint8_t rsv : 3;
			uint8_t fin : 1;
#elif NHTTP_BIG_ENDIAN
			uint8_t fin : 1;
			uint8_t rsv : 3;
			uint8_t opc : 4;
			uint8_t msk : 1;
			uint8_t len : 7;
#endif
		};
	};

	union hws_frame {
		hws_frame_hdr	hdr;
		uint8_t packed_bytes[16];
		struct { hws_frame_hdr hdr; uint32_t mkey;					} _n_1;
		struct { hws_frame_hdr hdr; uint16_t plen; uint32_t mkey;	} _n_2;
		struct { hws_frame_hdr hdr; uint64_t plen; uint32_t mkey;	} _n_8;
	};

	/* ping-pong max size buffer. */
	union hws_ping_pong {
		hws_frame		hdr;
		uint8_t			packed_bytes[6 + 125];
	};
#pragma pack(pop)

	static_assert(sizeof(hws_frame_hdr) == 2 && sizeof(hws_frame) == 16,
		"error: compiler can't generate frame header structure packed!" );

	/**
	 * class http_raw_websocket_content_handler.
	 * handles websocket data-frames.
	 */
	class NHTTP_API http_raw_websocket_content_handler
		: public http_raw_content_handler
	{
	private:
		inline void on_reset() { on_finalize(); on_initiate(); }

	private:
		hws_frame frame_hdr;
		uint16_t frame_hdr_sz;

		hws_frame frame_cur;

		struct {
			int8_t read_more : 1;
			int8_t frame_mode : 3; // 0: none, 1: _n_1, 2: _n_2, 3: _n_8. */

			int64_t cont_mark;
			int64_t cont_phase;

			uint32_t mask_key;
			uint64_t cont_plen;
		} state;
	public:
		/* initiate content handler. */
		virtual void on_initiate() override;

		/* finalize and cleanup content handler. */
		virtual void on_finalize() override;

		/* handle socket events for feeding content. */
		virtual int32_t on_event(socket_t& socket) override;

		int32_t on_event_n1(socket_t& socket);
		int32_t on_event_n2(socket_t& socket);
		int32_t on_event_n8(socket_t& socket);
	};

}
}
