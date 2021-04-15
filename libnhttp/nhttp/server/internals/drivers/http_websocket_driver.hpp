#pragma once
#include "../../http_link.hpp"

namespace nhttp {
	class stream;

namespace server {	
	class http_websocket;

namespace drivers {

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
	 * class http_websocket_driver.
	 * handles websocket connection's activities.
	 */
	class NHTTP_API http_websocket_driver : public http_link_driver {
	private:
		hal::spinlock_safe_t spinlock;

		std::shared_ptr<http_websocket> websocket;
		std::queue<std::tuple<std::shared_ptr<stream>, bool>> queue;

		hws_frame frame_hdr = { 0, }, 
				  frame_cur = { 0, };

		uint16_t  frame_hdr_sz;
		std::atomic<int8_t> wanna_close;

		uint8_t has_open;

	public:
		http_websocket_driver(const std::shared_ptr<http_websocket>& websocket);

	protected:
		virtual void on_initiate(const socket_t& socket,
			const std::shared_ptr<asyncs::context>& asyncs,
			const std::shared_ptr<http_chunked_buffer>& buffer,
			const std::shared_ptr<http_link>& link) override;

		virtual void on_finalize() override;

	protected:
		virtual bool on_event() override;
	};

}
}
}