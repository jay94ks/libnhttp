#include "tests/tests.hpp"

#include "nhttp/server/http_listener.hpp"
#include "nhttp/server/http_context.hpp"
#include "nhttp/server/extensions/http_vhost.hpp"
#include "nhttp/server/extensions/http_overlay.hpp"

#include "nhttp/server/xfwk/xfwk.hpp"

#ifdef _MSC_VER
#	pragma comment(lib, "libnhttp.lib")
#endif

int test_operations();
int main(int argc, char** argv) {
	test_urlencode();
	test_this_ptr();
	test_strings();
	test_paths();
	test_lambda();
	test_instrusive();

	test_async();
	test_hal();
	test_protocol();
	test_net();

	test_operations();
}

using namespace nhttp;
using namespace nhttp::server;
using namespace nhttp::server::xfwk;

class my_controller {
public:
	http_response_ptr hello(http_request_ptr p) {
		return make_response("hello world!");
	}
};

int test_operations() {
	socket_watcher watcher(1024);
	http_listener listener(watcher, http_params());

	ipv4::resolve("google.com", 443);
	ipv6::resolve("google.com", 443);

	ipv4::resolve_all("google.com");
	ipv6::resolve_all("google.com");

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

	auto router = std::make_shared<xfwk_router>();
	listener.extends(router);

	auto my_ctrl = std::make_shared<my_controller>();

	std::atomic<int32_t> exit(0);

	router /* */
		//->get(target_by(my_ctrl, &my_controller::hello))
		->post("exit", target_by([&](auto) {
			++exit;
			return make_response("server exiting...");
		}))
		->get("whoami", target_by([](http_request_ptr) {
			return make_response("I'm jay!");
		}))
		->group([](xfwk_facade_ptr inner) {
			inner
				->get(":user/profile", target_by([](http_request_ptr req) {
					std::string user = route_of(req).captures[":user"];

					return make_response(user + " is ...");
				}))
				->get(":user/greetings", target_by([](http_request_ptr req) {
					std::string user = route_of(req).captures[":user"];

					return make_response(user + " says hi!");
				}))
				->post(":user/set", target_by([](http_request_ptr req) {
					std::string body;

					if (!req->get_request_body()->read_all(body))
						return make_response(400);

					return make_response(body);
				}));

			inner->put(":user/set", target_by([](http_request_ptr req) {
				std::string body;

				if (!req->get_request_body()->read_all(body))
					return make_response(400);

				return make_response(body);
			}));

			inner->delet(":user", target_by([](http_request_ptr req) {
				std::string user = route_of(req).captures[":user"];

				return make_response(user + " deleted!");
			}));

			inner->param(":user", [](const std::string& name) {
				return name == "jay" || name == "kay";
			});
		});

	time_t tt = time(nullptr);

	/* Main thread as event thread. */
	listener.run([&](auto) {
		return !exit;
	});

	return 0;
}