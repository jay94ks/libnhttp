#pragma once
#include "http_raw_listener.hpp"
#include "../hal/rwlock_t.hpp"

namespace nhttp {
namespace server {

	class http_extension;
	class http_context;
	struct http_extension_registry;

	/**
	 * class http_listener.
	 * listens a port and provides HTTP services.
	 */
	class NHTTP_API http_listener : public http_raw_listener {
	private:
		std::atomic<bool> flag_dtor;
		std::shared_ptr<http_extension_registry> registry;

		hal::rwlock_t rwlock;
		hal::event_lite_t event;
		std::atomic<int64_t> waiters;
		std::queue<std::shared_ptr<http_context>> queue;

	public:
		http_listener(const socket_watcher& watcher, const http_params& params);
		virtual ~http_listener() { flag_dtor.store(true); terminate(); }

	public:
		/* add module on the listener. */
		void extends(std::shared_ptr<http_extension> extension);

	private:
		/* handle raw contexts. */
		virtual void on_raw_context(const std::shared_ptr<http_raw_context>& raw_context, bool has_error) override;

	protected:
		/* called for encapsulating raw context to http_context. */
		virtual bool on_newbie(std::shared_ptr<http_context>& out, const std::shared_ptr<http_raw_context>& context);

		/* called for handling a context. */
		virtual void on_handle(const std::shared_ptr<http_context>& context);

	public:
		/* range waiter. */
		class range_waiter {
			friend class http_listener;

		private:
			struct state {
				mutable http_listener* listener = nullptr;
				mutable std::shared_ptr<http_context> context;
				mutable utils::instrusive<std::thread, true> thread;
				mutable std::atomic<int32_t> terminating = 0;

				~state() {
					if (listener) {
						--listener->waiters;
					}

					if (thread) {
						++terminating;

						if ((*thread).joinable())
							(*thread).join();
					}

					thread.unset();
				}
			};

			std::shared_ptr<state> _state;

		public:
			range_waiter(http_listener* listener);
			~range_waiter();

		public:
			bool operator !=(const range_waiter& r) const { return !(*this == r); }
			bool operator ==(const range_waiter& r) const;

		public:
			inline std::shared_ptr<http_context> operator *() { return _state ? _state->context : nullptr; }
			inline range_waiter& operator ++() {
				if (_state) {
					_state->context = nullptr;
				}

				return *this;
			}
		};

	public:
		inline range_waiter begin() {
			return range_waiter(this);
		}

		inline range_waiter end() {
			return range_waiter(nullptr);
		}
	};
}
}