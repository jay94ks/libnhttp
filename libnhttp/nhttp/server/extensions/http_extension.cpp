#include "http_extension.hpp"
#include "../../hal/rwlock_t.hpp"

namespace nhttp {
namespace server {

	struct http_extendable_extension_registry {
		hal::rwlock_t rwlock;
		std::vector<http_extension_ptr> extensions;
	};

	http_extendable_extension::http_extendable_extension(uint32_t priority)
		: http_extension(priority) { registry = std::make_shared<http_extendable_extension_registry>(); }

	void http_extendable_extension::extends(http_extension_ptr extension) {
		registry->rwlock.lock_write();

		registry->extensions.push_back(extension);
		std::sort(registry->extensions.begin(), registry->extensions.end(),
			[](const http_extension_ptr& left, const http_extension_ptr& right) {
				return left->get_priority() < right->get_priority();
			});

		registry->rwlock.unlock_write();
	}

	bool http_extendable_extension::on_handle(std::shared_ptr<http_context> context) {
		std::queue<std::shared_ptr<http_extension>> order;

		if (!on_enter(context))
			return false;

		/* collect modules. */
		registry->rwlock.lock_read();
		for (auto module : registry->extensions) {
			if (!module->on_collect(context)) {
				registry->rwlock.unlock_read();
				continue;
			}

			order.push(module);
		}
		registry->rwlock.unlock_read();

		/* execute extensions. */
		while (order.size()) {
			auto extension = order.front();
			order.pop();

			if (extension->on_handle(context))
				return true;
		}

		if (!on_handle(context, by_extended)) {
			on_leave(context);
			return false;
		}

		on_leave(context);
		return true;
	}
}
}