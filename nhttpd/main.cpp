
#include "../libnhttp/nhttp/types.hpp"
#include "../libnhttp/nhttp/server/http_listener.hpp"
#include "../libnhttp/nhttp/server/http_context.hpp"
#include "../libnhttp/nhttp/server/extensions/http_vhost.hpp"
#include "../libnhttp/nhttp/server/extensions/http_overlay.hpp"
#include <iostream>

#ifdef _MSC_VER
#	pragma comment(lib, "libnhttp.lib")
#endif

using namespace nhttp;
using namespace nhttp::server;

int main(int argc, char** argv) {
	socket_watcher watcher(1024);
	http_listener listener(watcher, http_params());

	if (!listener.with(ipv4::resolve("0.0.0.0", 8080))) {
		std::cout << "error: can't listen: 0.0.0.0:8080.\n";
		return 1;
	}

	if (!listener.with(ipv6::resolve("::1", 8080))) {
		std::cout << "error: can't listen: 0.0.0.0:8080.\n";
		return 1;
	}

	int32_t c = 0;

	auto vhost = vhost_for(std::regex(".*"));
	listener.extends(vhost);

	listener.extends(overlay_of(".", "index.html"));


	/* Main thread as dispatcher. */
	for(http_context_ptr context : listener) {
		context->response = make_response("hello world");
		context->close();

		if (c++ > 10)
			break;
	}

	/* Main thread as event thread. */
	listener.run();
	
	return 0;
}