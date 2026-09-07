#pragma once

#include "socket.hpp"

#include <memory>

namespace nhttp::platform {

	/* one readiness notification. `user_data` is whatever pointer was passed to
	 * reactor::add()/modify() for this fd — the reactor never interprets it. */
	struct ready_event {
		void* user_data = nullptr;
		bool readable = false;
		bool writable = false;
	};

	/**
	 * class reactor.
	 * a portable readiness-multiplexing interface — one instance per io_context.
	 * POSIX implements this directly over epoll; Windows emulates the same
	 * readiness model over IOCP (zero-byte overlapped reads/writes just to
	 * detect readiness, a standard technique — see CLAUDE.md's Windows-support
	 * notes). Either way, `io_context` only ever sees this interface, never an
	 * OS-specific handle or event type.
	 *
	 * usage contract (matches what io_context already does): add() is called
	 * exactly once per fd, the first time it gains any interest; modify() for
	 * every subsequent interest change; remove() once, when interest drops to
	 * zero or the fd is about to be closed. wake() is thread-safe and may be
	 * called from any thread to interrupt a blocked wait().
	 */
	class reactor {
	public:
		virtual ~reactor() = default;

		virtual bool add(native_socket_t fd, bool want_read, bool want_write, void* user_data) noexcept = 0;
		virtual bool modify(native_socket_t fd, bool want_read, bool want_write, void* user_data) noexcept = 0;
		virtual void remove(native_socket_t fd) noexcept = 0;

		/* blocks up to timeout_ms (-1 = forever, 0 = poll), or until wake() is
		 * called. returns the number of events written to out_events (never
		 * includes a synthetic "woken up" event — callers just re-check their
		 * own state after any wait() call). */
		virtual int wait(ready_event* out_events, int max_events, int timeout_ms) noexcept = 0;

		virtual void wake() noexcept = 0;
	};

	/* constructs the platform's reactor implementation. */
	std::unique_ptr<reactor> make_reactor();

}
