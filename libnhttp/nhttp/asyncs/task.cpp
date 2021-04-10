#include "task.hpp"

namespace nhttp {
namespace asyncs {

	void task::run() {
		spinlock.lock();

		if (state != NTASK_READY) {
			spinlock.unlock();
			return;
		}

		state = NTASK_RUNNING;
		spinlock.unlock();

		on_run();

		spinlock.lock();
		state = NTASK_COMPLETION;
		spinlock.unlock();
	}

}
}