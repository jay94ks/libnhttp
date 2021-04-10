#include "socket_watcher.hpp"

namespace nhttp {
	bool socket_watcher::watch_state_t::watch(const socket_t& sock, void(*on_event)(socket_t)) {
		socket_t target = sock;

		if (auto handle = target.handle) {
			if (!handle->raw.is_valid())
				return false;

			if (handle->flags.io_mode)
				return false;

			handle->flags.closed = 0;
			handle->flags.io_mode = 1;
			handle->flags.can_read = 0;
			handle->flags.can_write = 0;

			handle->on_event = on_event;
			handle->data_ptr = new socket_t(target);

			++sockets;

			epoll.add(handle->raw.get_fd(), EPOLLIN |
				EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP,
				handle->data_ptr);

			return true;
		}

		return false;
	}
	
	bool socket_watcher::watch_state_t::unwatch(const socket_t& sock) {
		socket_t target = sock;

		if (auto handle = target.handle) {
			if (!handle->raw.is_valid())
				return false;

			if (!handle->flags.io_mode)
				return false;

			void* data_ptr = handle->data_ptr;
			epoll.remove(handle->raw.get_fd());

			--sockets;

			handle->flags.io_mode = 0;
			handle->on_event = nullptr;
			handle->data_ptr = nullptr;
			

			delete (socket_t*)data_ptr;
			return true;
		}

		return false;
	}

	int32_t socket_watcher::watch_state_t::wait(socket_event* out_events, int32_t max_events, int32_t timeout) {
		int slice = 0, skips = 0;

		if (max_events <= 0)
			return 0;

		/* prevent max_events overflow than size. */
		max_events = max_events > _size ? _size : max_events;

		/* if there are stored events, pop them first. */
		if (count <= 0) {
			++waiters;

			count = epoll.wait(events, max_events, timeout);
			index = 0;

			--waiters;
			if (count <= 0) {
				count = 0;
				return 0;
			}

			slice = count;
		}

		slice = count < max_events ? count : max_events;
		for (int i = index; i < index + slice; i++) {
			auto& event = events[i];
			socket_t* sock = ((socket_t*)event.data.ptr);
			auto& flags = sock->handle->flags;

			out_events->sock = sock;
			out_events->on_event = sock->handle->on_event;
			
			if ((event.events & (EPOLLERR | EPOLLHUP)) != 0) {
				out_events->flags.can_read = flags.can_read = 1;
				out_events->flags.can_write = flags.can_write = 1;
			}

			else {
				if ((event.events & EPOLLIN) != 0)
					out_events->flags.can_read = flags.can_read = 1;

				if ((event.events & EPOLLOUT) != 0)
					out_events->flags.can_write = flags.can_write = 1;

				if ((event.events & EPOLLRDHUP) != 0)
					out_events->flags.closed = flags.closed = 1;
			}

			if (out_events->on_event) {
				out_events->on_event(*out_events->sock);
				++skips;
				continue;
			}

			++out_events;
		}

		index += slice;
		count -= slice;
		return slice - skips;
	}
}