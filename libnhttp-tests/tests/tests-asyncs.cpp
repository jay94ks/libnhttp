#include "tests.hpp"
#include <nhttp/asyncs/context.hpp>
#include <nhttp/asyncs/future.hpp>

void test_async() {
	test_case label("asyncs/context.hpp, asyncs/future.hpp");

	std::cout << " - creating async context with 2 workers...\n";
	nhttp::asyncs::context context(2);

	nhttp::future<void> test1 = context.future_of([]() {
		std::cout << " : plain async-task.\n";
	});

	nhttp::future<int> test2 = context.future_of([]() {
		std::cout << " : value returnning task.\n";
		return 100;
		});

	nhttp::future<void> test3 = context.future_of([]() {
		std::cout << " : sleeping, 1000 ms task.\n";
		std::this_thread::sleep_for(
			std::chrono::microseconds(1000)
		);
	});

	/* waiting result. */
	test1.wait(-1); /* infinite wait. */
	std::cout << " : value returning: " << test2.get_result() << "\n";

	test3.wait(1000); /* milliseconds wait. */
	std::cout << " : and cleaning async context ...\n";
}