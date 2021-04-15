#include "tests.hpp"
#include <nhttp/hal/barrior_t.hpp>
#include <nhttp/hal/event_t.hpp>
#include <nhttp/hal/rwlock_t.hpp>
#include <nhttp/hal/socket_raw_t.hpp>
#include <nhttp/hal/spinlock_t.hpp>

#include <nhttp/asyncs/context.hpp>
#include <nhttp/asyncs/future.hpp>

void test_hal() {
	test_case label("hal/*.hpp with asyncs module.");

	std::cout << " - creating async context with 2 workers...\n";
	nhttp::asyncs::context context(2);

	std::queue<int> queue;
	nhttp::hal::event_t event(false, false);
	nhttp::hal::rwlock_t rwlock;
	nhttp::hal::barrior_t barrior;
	nhttp::hal::spinlock_t spinlock;

	std::cout << " - test 1. event object + rwlock dispatcher usage.\n";

	auto sender = context.future_of([&]() {
		for (int i = 0; i < 5; ++i) {
			rwlock.lock_write();
			queue.push(i);
			rwlock.unlock_write();

			std::this_thread::sleep_for(
				std::chrono::microseconds(10)
			);

			event.signal();
		}
	});

	auto receiver = context.future_of([&]() {
		while (!sender.is_completed()) {
			if (!event.timed_wait(1))
				continue;

			rwlock.lock_read();
			int payload = queue.front();
			queue.pop();
			rwlock.unlock_read();

			std::cout << " : sender sent " << payload << "\n";
		}
	});

	receiver.wait(-1);
	label.print_now();

	std::cout << " - test 2. event object + barrior dispatcher usage.\n";

	auto barrior_sender = context.future_of([&]() {
		for (int i = 0; i < 5; ++i) {
			barrior.lock();
			queue.push(i);
			barrior.unlock();

			std::this_thread::sleep_for(
				std::chrono::microseconds(10)
			);

			event.signal();
		}
	});

	auto barrior_receiver = context.future_of([&]() {
		while (!barrior_sender.is_completed()) {
			if (!event.timed_wait(100))
				continue;

			barrior.lock();
			int payload = queue.front();
			queue.pop();
			barrior.unlock();

			std::cout << " : sender sent " << payload << "\n";
		}
	});

	barrior_receiver.wait(-1);
	label.print_now();
	
	std::cout << " - test 3. event object + spinlock dispatcher usage.\n";

	auto spinlock_sender = context.future_of([&]() {
		for (int i = 0; i < 5; ++i) {
			spinlock.lock();
			queue.push(i);
			spinlock.unlock();

			std::this_thread::sleep_for(
				std::chrono::microseconds(10)
			);

			event.signal();
		}
	});

	auto spinlock_receiver = context.future_of([&]() {
		while (!spinlock_sender.is_completed()) {
			if (!event.timed_wait(100))
				continue;

			spinlock.lock();
			int payload = queue.front();
			queue.pop();
			spinlock.unlock();

			std::cout << " : sender sent " << payload << "\n";
		}
	});

	spinlock_receiver.wait(-1);
	label.print_now();

	std::cout << " : socket_raw_t will be tested with `net::socket_t`. ...\n";
	std::cout << " : and cleaning async context ...\n";
}