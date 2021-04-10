#pragma once
#include "../types.hpp"
#include "../hal/barrior_t.hpp"
#include "../hal/event_t.hpp"
#include "future.hpp"

namespace nhttp {
namespace asyncs {

	/**
	 * class context.
	 * context for executing async tasks.
	 */
	class NHTTP_API context : public std::enable_shared_from_this<context> {
	private:
		hal::barrior_t barrior;
		hal::event_t event;

		std::atomic<int32_t> dtor;
		std::atomic<uint64_t> tasks;

		std::queue<std::thread> workers;
		std::queue<std::shared_ptr<task>> runnables;

		std::queue<hal::event_t> pooled_events;

	public:
		context(int32_t worker_count);
		~context();

	public:
		inline uint64_t get_tasks() const { return tasks; }

	private:
		void on_each_thread();

	public:
		/* push runnable task. */
		void push(const std::shared_ptr<task>& runnable);

		template<typename lambda_type>
		inline auto future_of(lambda_type&& lambda) {
			using return_type = decltype(lambda());
			using handle_type = _::future_task<typename std::decay<return_type>::type, lambda_type>;
			using future_type = future<typename std::decay<return_type>::type>;

			utils::instrusive<hal::event_t, true> event;


			barrior.lock();
			if (!pooled_events.size()) 
				event = hal::event_t(false, true);
			
			else {
				event = pooled_events.front();
				pooled_events.pop();
			}

			(*event).unsignal();
			barrior.unlock();

			auto runnable = std::make_shared<handle_type>(std::move(lambda));
			auto future_is = future_type(runnable);

			if (dtor) {
				return future_type();
			}

			push(runnable);
			return future_is;
		}
	};

}
}