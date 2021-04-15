#include "tests.hpp"
#include <nhttp/net/socket.hpp>
#include <nhttp/net/socket_watcher.hpp>

#include <nhttp/asyncs/context.hpp>
#include <nhttp/asyncs/future.hpp>

void test_net() {
	test_case label("net/*.hpp with asyncs module.");

	std::cout << " - creating async context with 2 workers...\n";
	nhttp::asyncs::context context(2);

	std::cout << " - creating socket_watcher with 128 capacity...\n";
	nhttp::socket_watcher watcher(128);

	std::cout << " - resolving ipv4 and ipv6 endpoints to google.com.\n";
	std::vector<nhttp::ipv4> v4_eps = nhttp::ipv4::resolve_all("google.com");
	std::vector<nhttp::ipv6> v6_eps = nhttp::ipv6::resolve_all("google.com");

	for (nhttp::ipv4& each : v4_eps) {
		std::cout << " v4: " << nhttp::hal::to_string(each.addr) << "\n";
	}

	for (nhttp::ipv6& each : v6_eps) {
		std::cout << " v6: " << nhttp::hal::to_string(each.addr) << "\n";
	}

	nhttp::socket_t listen_v4 = nhttp::socket_t::create<nhttp::ipv4_addr, nhttp::tcp>();
	nhttp::socket_t listen_v6 = nhttp::socket_t::create<nhttp::ipv6_addr, nhttp::tcp>();
	
	nhttp::socket_t client_v4 = nhttp::socket_t::create<nhttp::ipv4_addr, nhttp::tcp>();
	nhttp::socket_t client_v6 = nhttp::socket_t::create<nhttp::ipv6_addr, nhttp::tcp>();
	std::cout << " - creating ipv4 and ipv6 socket listeners...\n";

	if (!listen_v4.bind(nhttp::ipv4::resolve("127.0.0.1", 19999))) {
		std::cout << " : failed to bind `127.0.0.1:19999`\n";
		listen_v4.close();
		listen_v4 = nhttp::socket_t();
	}

	else {
		listen_v4.get_raw()
			.set_reuse_address(true);

		listen_v4.listen();
		watcher.watch(listen_v4, nullptr);
	}

	if (!listen_v6.bind(nhttp::ipv6::resolve("::1", 19999)) || !listen_v6.listen()) {
		std::cout << " : failed to bind `[::1]:19999`\n";
		listen_v6.close();
		listen_v6 = nhttp::socket_t();
	}

	else {
		listen_v6.get_raw()
			.set_reuse_address(true);

		listen_v6.listen();
		watcher.watch(listen_v6, nullptr);
	}

	context.future_of([&]() {
		std::this_thread::sleep_for(
			std::chrono::microseconds(100)
		);

		if (!client_v4.connect(nhttp::ipv4::resolve("127.0.0.1", 19999))) {
			client_v4.close();
			client_v4 = nhttp::socket_t();
		}

		else watcher.watch(client_v4, nullptr);
	});
	

	context.future_of([&]() {
		std::this_thread::sleep_for(
			std::chrono::microseconds(100)
		);

		if (!client_v6.connect(nhttp::ipv6::resolve("127.0.0.1", 19999))) {
			client_v6.close();
			client_v6 = nhttp::socket_t();
		}

		else watcher.watch(client_v6, nullptr);
	});

	std::cout << "waiting socket events...\n";
	time_t now = time(nullptr);
	for(auto& event : watcher) {
		if (*event.sock == listen_v4 || *event.sock == listen_v6) {
			if (event.sock->can_read()) {
				nhttp::socket_t newbie = event.sock->accept();
				newbie.set_tag(&watcher, nullptr);

				watcher.watch(newbie, [](nhttp::socket_t s) {
					auto watcher = (nhttp::socket_watcher*)s.get_tag();

					auto handle_events = [](nhttp::socket_t s) {
						char buf[1024];

						if (s.can_read()) {
							int32_t r = s.read(buf, sizeof(buf) - 1);

							if (r <= 0) {
								int32_t e = s.get_errno();

								if (e == EWOULDBLOCK || e == EAGAIN || e == EINTR)
									return true;

								return false;
							}

							buf[r] = 0;
						}

						return true;
					};

					if (!s.is_alive() || !handle_events(s)) {
						watcher->unwatch(s);
						s.close();
					}
				});
			}
		}

		else {
			nhttp::socket_t current = *event.sock;

			if (current.can_write()) {
				current.write("hello!", 6);
			}

			if (time(nullptr) - now > 5) {
				if (current == client_v4 || current == client_v6) {
					watcher.unwatch(current);
					current.close();

					if (current == client_v4)
						client_v4 = nhttp::socket_t();

					else client_v6 = nhttp::socket_t();
				}
			}
		}


		if (!client_v4.get_raw().is_valid() &&
			!client_v6.get_raw().is_valid())
		{
			if (listen_v4.get_raw().is_valid())
				watcher.unwatch(listen_v4);

			if (listen_v6.get_raw().is_valid())
				watcher.unwatch(listen_v6);

			if (listen_v4.get_raw().is_valid())
				listen_v4.close();

			if (listen_v6.get_raw().is_valid())
				listen_v6.close();

			break;
		}
	}

	label.print_now();
	std::cout << " : and cleaning async context ...\n";
}