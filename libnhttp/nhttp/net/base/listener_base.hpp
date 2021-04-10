#pragma once
#include "../socket.hpp"
#include "../socket_watcher.hpp"
#include "../../asyncs/context.hpp"

namespace nhttp {
namespace base {

	class session_base;

	/**
	 * class listener_base.
	 * base class of a listener.
	 */
	class NHTTP_API listener_base {
	private:
		struct socket_tag {
			session_base* session;
			listener_base* listener;
			future<void> initiator;
		};

	protected:
		socket_watcher watcher;
		std::atomic<bool> terminating;
		std::shared_ptr<asyncs::context> workers;

	private:
		std::vector<socket_t> sources;
		std::atomic<int32_t> alives;
		std::queue<socket_tag*> pooled_tags;

	public:
		/**
		 * initialize a self-hosted listener.
		 */
		listener_base(int32_t watcher_size, int32_t workers)
			: watcher(watcher_size), terminating(false),
			  workers(std::make_shared<asyncs::context>(workers)),
			  alives(0) { }
			  
		/**
		 * initialize a self-hosted listener.
		 */
		listener_base(int32_t watcher_size, std::shared_ptr<asyncs::context> workers)
			: watcher(watcher_size), terminating(false), workers(workers), alives(0) { }

		/**
		 * initialize a co-hosted listener.
		 */
		listener_base(const socket_watcher& watcher, int32_t workers) 
			: watcher(watcher), terminating(false),
			  workers(std::make_shared<asyncs::context>(workers)),
			  alives(0) { }
			  
		/**
		 * initialize a co-hosted listener.
		 */
		listener_base(const socket_watcher& watcher, std::shared_ptr<asyncs::context> workers)
			: watcher(watcher), terminating(false), workers(workers), alives(0) { }

		virtual ~listener_base() { terminate(); }

	protected:
		void terminate();

	public:
		/* register new listening IPv4 address and ports. */
		bool with(const ipv4& ep);

		/* register new listening IPv6 address and ports. */
		bool with(const ipv6& ep);

		/* run the event loop with co-loop. */
		template<typename coloop_type>
		inline void run(coloop_type&& coloop) {
			std::queue<socket_event> events;

			while (coloop(nullptr)) {
				watcher.wait(events, 100);

				if (events.empty())
					continue;

				if (!coloop(&events.front())) {
					events.pop();
					break;
				}

				events.pop();
			}
		}

		/* run the event loop. */
		inline void run() {
			run([this](socket_event* event) {
				if (event) {
					socket_t sock = *event->sock;

					/**
					 * kill non-evented socket.
					 * because this listener don't know how to handle it.
					 */
					watcher.unwatch(sock);
					sock.close();
				}

				return true;
			});
		}

	private:
		void on_newbie(socket_t sock);
		void on_dead(socket_tag* tag, socket_t& sock);

	protected:
		/**
		 * called when link should be created.
		 * @returns:
		 *	=  null: no capacity, keep socket unhandled.
		 *  != null: accept socket and initiate.
		 */
		virtual session_base* on_enter() = 0;

		/* called when link should be destroyed. (override if required) */
		virtual void on_leave(session_base* session) { }
	};

}
}