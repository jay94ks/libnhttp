#pragma once
#include "../types.hpp"
#include "../hal/event_t.hpp"

#include "task.hpp"
#include "future_task.hpp"

namespace nhttp {
	template<typename type>
	class future {
	private:
		std::shared_ptr<_::future_task_base<type>> task;

	public:
		future(nullptr_t = nullptr) { }
		future(std::shared_ptr<_::future_task_base<type>> task)
			: task(std::move(task)) { }

	public:
		/* wait completion with blocking. */
		inline bool wait(int32_t timeout) { return !task || task->wait(timeout); }
		inline bool is_completed() const { return !task || task->get_state() == asyncs::NTASK_COMPLETION; }
		inline type get_result() const {
			NHTTP_CRITICAL(task, "tried to access uninitialized future.");
			return task->get_result(); 
		}
	};

	template<>
	class future<void> {
	private:
		std::shared_ptr<_::future_task_base<void>> task;

	public:
		future(nullptr_t = nullptr) { }
		future(std::shared_ptr<_::future_task_base<void>> task)
			: task(std::move(task)) { }

	public:
		inline operator bool() const { return !(!task); }
		inline bool operator !() const { return !task; }

	public:
		/* wait completion with blocking. */
		inline bool wait(int32_t timeout) { return !task || task->wait(timeout); }
		inline bool is_completed() const { return !task || task->get_state() == asyncs::NTASK_COMPLETION; }
	};

}