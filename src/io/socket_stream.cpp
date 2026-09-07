#include "nhttp/io/socket_stream.hpp"

namespace nhttp::io {

	async::task<std::int64_t> socket_stream::seek(std::int64_t, seek_origin) {
		co_return -1;
	}

	async::task<void> socket_stream::flush() {
		co_return;
	}

	async::task<void> socket_stream::close() {
		socket_.close();
		co_return;
	}

}
