#pragma once
#include "../hal/epoll_raw_t.hpp"
#include "endpoint.hpp"
#include "socket.hpp"

namespace nhttp {
	class socket_watcher;
	struct socket_event {
		socket_t* sock;
		void (*on_event)(socket_t);

		struct {
			int8_t can_read : 1;  /* from watcher, 0: avail, 1: unavail */
			int8_t can_write : 1; /* from watcher, 0: avail, 1: unavail */
			int8_t closed : 1;	  /* from watcher, 0: none,  1: closed. */
		} flags = { 0, };
	};

	class socket_watcher {
	private:
		struct watch_state_t {
			hal::epoll_raw_t epoll;
			std::atomic<int32_t> sockets;
			std::atomic<int64_t> waiters;

			epoll_event* events;
			int32_t index, count, _size;

			watch_state_t(int32_t size)
				: epoll(size), events(new epoll_event[size]),
				index(0), count(0), _size(size)
			{
				memset(events, 0, sizeof(epoll_event) * size);
			}

			~watch_state_t() {
				delete[] events;
			}

			/* add watch target or remove watch target. */
			bool watch(const socket_t& sock, void (*on_event)(socket_t));
			bool unwatch(const socket_t& sock);

			/* returns count of events. */
			int32_t wait(socket_event* out_events, int32_t max_events, int32_t timeout);
		};

		std::shared_ptr<watch_state_t> state;

	public:
		socket_watcher(int32_t size)
			: state(std::make_shared<watch_state_t>(size))
		{
		}

		/* determines the watcher watching sockets or not. */
		inline bool is_watching() const { return state && state->waiters; }

	public:
		inline bool watch(const socket_t& sock, void (*on_event)(socket_t)) {
			NHTTP_INIT_ASSERT(state, "tried to use uninitialized socket_watcher!");
			return state->watch(sock, on_event);
		}

		inline bool unwatch(const socket_t& sock) {
			NHTTP_INIT_ASSERT(state, "tried to use uninitialized socket_watcher!");
			return state->unwatch(sock);
		}

		inline int32_t wait(std::queue<socket_event>& out_events, int32_t timeout) {
			NHTTP_INIT_ASSERT(state, "tried to use uninitialized socket_watcher!");
			socket_event e[256];
			int32_t n = state->wait(e, 256, timeout);

			for (int32_t i = 0; i < n; ++i) {
				out_events.push(e[i]);
			}

			return n;
		}

		inline int32_t wait(int32_t timeout) {
			NHTTP_INIT_ASSERT(state, "tried to use uninitialized socket_watcher!");
			++state->waiters;

			socket_event e[256];
			int32_t n = state->wait(e, 256, timeout);

			for (int32_t i = 0; i < n; ++i) {
				unwatch(*e[i].sock);
				e[i].sock->close();
			}

			--state->waiters;
			return n;
		}

	public:
		/**
		 * class iterator.
		 * iterator for getting socket events using range loop.
		 */
		class iterator {
			friend class socket_watcher;

		private:
			std::shared_ptr<watch_state_t> state;
			mutable std::queue<socket_event> queue;

		public:
			iterator() { }

		protected:
			iterator(std::shared_ptr<watch_state_t> state)
				: state (state) { }

		public:
			inline bool operator !=(const iterator& o) const { return !(*this == o); }
			inline bool operator ==(const iterator& o) const {
				if (state == o.state)
					return true;

				if (o.state)
					return false;

				while(!queue.size()) {
					socket_event e[256];
					int32_t n = state->wait(e, 256, -1);

					if (n) {
						for (int i = 0; i < n; i++)
							queue.push(e[i]);
					}
				}

				return queue.size() <= 0;
			}

			inline socket_event& operator *() {
				return queue.front();
			}

			inline iterator& operator ++() {
				if (queue.size())
					queue.pop();

				return *this;
			}
		};

	public:
		inline iterator begin() const { return iterator(state); }
		inline iterator end()   const { return iterator(); }
	};
}