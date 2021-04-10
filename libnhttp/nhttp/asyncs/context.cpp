#include "context.hpp"
#include "task.hpp"

namespace nhttp {
namespace asyncs {

	context::context(int32_t worker_count)
		: event(false, true), dtor(0), tasks(0)
	{
		for (int32_t i = 0; i < worker_count; ++i) {
			workers.push(std::move(std::thread([this]() {
				this->on_each_thread();
			})));
		}
	}

	context::~context() {
		++dtor;
		
		while (workers.size()) {
			if (workers.front().joinable())
				workers.front().join();

			workers.pop();
		}
	}

	void context::on_each_thread() {
		std::shared_ptr<task> runnable;
		bool running = false;

		while (!dtor || running) {
			if (runnable) {
				runnable->run();
				runnable = nullptr;
			}

			if (event.timed_wait(100)) {
				running = true;
				barrior.lock();

				if (runnables.size()) {
					runnable = runnables.front();
					runnables.pop();

					--tasks;
				}

				if (runnables.size())
					event.signal();

				else event.unsignal();

				barrior.unlock();
			}
			
			else running = false;
		}
	}

	void context::push(const std::shared_ptr<task>& runnable) {
		std::lock_guard<decltype(barrior)> guard(barrior);
		runnables.push(runnable);
		event.signal();
	}

}
}