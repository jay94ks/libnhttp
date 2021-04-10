#pragma once
#include "../types.hpp"
#include "../hal/event_t.hpp"
#include "task.hpp"

namespace nhttp {
namespace _ {

	template<typename type>
	class future_task_base : public asyncs::task {
	private:
		mutable hal::event_t eve;
		utils::instrusive<type, true> result;

	public:
		future_task_base()
			: eve(false, true)
		{
		}

		virtual ~future_task_base() { }

	public:
		/* wait completion with blocking. */
		inline bool wait(int32_t timeout) {
			return eve.timed_wait(timeout);
		}

		/* get result of this future task. */
		inline type get_result() const {
			while (get_state() == asyncs::NTASK_RUNNING)
				eve.wait();

			return *result;
		}

	protected:
		virtual void on_run() override {
			result = on_run_future();
			eve.signal();
		}

		virtual type on_run_future() = 0;
	};


	template<>
	class future_task_base<void> : public asyncs::task {
	protected:
		mutable hal::event_t eve;
		
	public:
		future_task_base()
			: eve(false, true)
		{
		}

		virtual ~future_task_base() { }

	public:
		/* wait completion with blocking. */
		inline bool wait(int32_t timeout) {
			return eve.timed_wait(timeout);
		}
	};

	template<typename type, typename lambda_type>
	class future_task : public future_task_base<type> {
	private:
		lambda_type lambda;

	public:
		future_task(lambda_type&& lambda)
		{
		}

	protected:
		virtual type on_run_future() override { return lambda(); }
	};


	template<typename lambda_type>
	class future_task<void, lambda_type> : public future_task_base<void> {
	private:
		lambda_type lambda;

	public:
		future_task(lambda_type&& lambda)
			: lambda(std::move(lambda))
		{
		}

	protected:
		virtual void on_run() override { 
			lambda();
			eve.signal();
		}
	};
}
}