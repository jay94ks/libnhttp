#include "http_listener.hpp"
#include "http_link.hpp"
#include "http_raw_context.hpp"
#include "http_context.hpp"
#include "extensions/http_extension.hpp"
#include <stack>

namespace nhttp {
namespace server {

	struct http_extension_registry {
		hal::rwlock_t rwlock;
		std::vector<http_extension_ptr> extensions;
	};

	class http_global_tags : public http_taggable { };

	http_listener::http_listener(const socket_watcher& watcher, const http_params& params)
		: http_raw_listener(watcher, params), flag_dtor(false), event(false, false)
	{
		registry = std::make_shared<http_extension_registry>();
		global_tags = std::make_shared<http_global_tags>();
	}

	void http_listener::extends(std::shared_ptr<http_extension> extension) {
		registry->rwlock.lock_write();

		registry->extensions.push_back(extension);
		std::sort(registry->extensions.begin(), registry->extensions.end(),
			[](const http_extension_ptr& left, const http_extension_ptr& right) {
				return left->get_priority() < right->get_priority();
			});

		registry->rwlock.unlock_write();
	}

	void http_listener::on_raw_context(const std::shared_ptr<http_raw_context>& raw_context, bool has_error) {
		std::shared_ptr<http_context> context;

		if (flag_dtor || !on_newbie(context, raw_context) || !context) {
			/* response: 503 service unavailable. */
			raw_context->response.status.set(503);
			raw_context->close();
			return;
		}

		/* set global tag. */
		context->global = global_tags;
		workers->future_of([this, context]() {
			std::queue<std::shared_ptr<http_extension>> order;

			/* collect handler extensions. */
			registry->rwlock.lock_read();
			for (auto module : registry->extensions) {
				if (!module->on_collect(context)) {
					registry->rwlock.unlock_read();

					context->response->status.set(403);
					context->close();
					return;
				}

				order.push(module);
			}
			registry->rwlock.unlock_read();

			/* execute extensions. */
			while (order.size()) {
				auto extension = order.front();
				order.pop();

				if (extension->on_handle(context))
					return;
			}

			/* handle context really. */
			on_handle(context);
		});
	}

	bool http_listener::on_newbie(std::shared_ptr<http_context>& out, const std::shared_ptr<http_raw_context>& context) {
		return (out = std::make_shared<http_context>(context)) != nullptr;
	}

	void http_listener::on_handle(const std::shared_ptr<http_context>& context) {
		if (!waiters) {
			/* 501 Not Implemented. */
			if (!context->response)
				 context->response = make_response(501);
			else context->response->status.set(501);

			/* Close and send response immediately. */
			context->close();
		}

		/* if range loop is running, push context to it. */
		else {
			rwlock.lock_write();
			queue.push(context);
			rwlock.unlock_write();

			event.signal();
		}
	}

	http_listener::range_waiter::range_waiter(http_listener* listener) {
		if (listener) {
			++listener->waiters;

			_state = std::make_shared<state>();
			_state->listener = listener;
		}
	}

	http_listener::range_waiter::~range_waiter() {
		
	}

	bool http_listener::range_waiter::operator ==(const range_waiter& r) const {
		if (_state == r._state)
			return true;

		if (r._state)
			return false;

		if (!_state->listener->watcher.is_watching() && !_state->thread) {
			/* terminate watcher loop if range waiter destructed. */
			_state->thread = std::move(std::thread([this]() {
				_state->listener->run([this](socket_event* event) {
					if (event) {
						socket_t sock = *event->sock;

						/**
						 * kill non-evented socket.
						 * because this listener don't know how to handle it.
						 */
						_state->listener->watcher.unwatch(sock);
						sock.close();
					}

					return !_state->terminating;
				});
			}));
		}

		while (!_state->context && !_state->listener->terminating) {
			_state->listener->rwlock.lock_read();
			if (!_state->listener->queue.empty()) {
				_state->context = _state->listener->queue.front();
				_state->listener->queue.pop();
			}

			if (!_state->listener->queue.empty())
				_state->listener->event.signal();
			_state->listener->rwlock.unlock_read();

			if (_state->context)
				break;

			_state->listener->event.wait();
		}

		return false;
	}
}
}