#include "nhttp/io/stream.hpp"

namespace nhttp::io {

	async::task<void> stream::read_all(std::string& out) {
		char buf[4096];

		for (;;) {
			const std::size_t n = co_await read(buf, sizeof(buf));

			if (n == 0)
				break;

			out.append(buf, n);
		}
	}

}
