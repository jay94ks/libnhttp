#pragma once
#include "../types.hpp"
#include "../hal/spinlock_t.hpp"

namespace nhttp {
namespace asyncs {
	class context;

	enum task_state {
		NTASK_READY = 0,
		NTASK_RUNNING,
		NTASK_COMPLETION
	};

	/**
	 * class task.
	 * task interface.
	 */
	class NHTTP_API task {
		friend class context;

	private:
		hal::spinlock_t spinlock;
		std::atomic<int32_t> state;
		
	public:
		task() : state(NTASK_READY) { }
		virtual ~task() { }
	
	public:
		inline int32_t get_state() const { return int32_t(state); }

	protected:
		/* called by context. */
		void run();

	protected:
		/* execute runnable. */
		virtual void on_run() = 0;
	};

	/**
	 * class lambda_task.
	 * lambda task.
	 */
	template<typename lambda_type>
	class lambda_task : public task {
	private:
		lambda_type lambda;

	public:
		lambda_task(lambda_type&& lambda) 
			: lambda(std::move(lambda))
		{
		}

	protected:
		virtual void on_run() override { lambda(); }
	};
}
}