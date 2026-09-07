#include "nhttp/platform/reactor.hpp"
#include "wsa_init.hpp"
#include "overlapped_op.hpp"

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace nhttp::platform {

	namespace {

		/**
		 * class iocp_reactor.
		 * emulates epoll's readiness-notification model on top of IOCP's
		 * completion model — a standard technique (see CLAUDE.md's Windows-
		 * support notes) — so io_context/async_socket's retry-on-would_block
		 * design works unchanged on Windows.
		 *
		 * unlike an earlier version of this file, every registration (whether
		 * a listening socket or a connected stream socket) uses the *same*
		 * mechanism: WSAEventSelect + a threadpool wait (RegisterWaitForSingleObject)
		 * bridging the Win32 event-object signal into this completion port via
		 * a plain PostQueuedCompletionStatus, purely observational — the real
		 * work (accept()/recv()/send()) still happens in the generic,
		 * already-portable retry loops in async_socket/socket_handle.
		 *
		 * two earlier designs were tried and rejected while bringing this up
		 * (see CLAUDE.md's progress log for the full story):
		 *  - a zero-byte overlapped WSARecv for read-readiness: works for
		 *    connected sockets, but Winsock rejects it outright on a listening
		 *    socket (not a data channel), so accept() readiness needs a
		 *    different mechanism anyway.
		 *  - synthesizing write-readiness as always-immediately-ready: fine for
		 *    ordinary send() backpressure (the caller just retries), but wrong
		 *    for `async_socket::connect()`'s completion check specifically —
		 *    it reads SO_ERROR exactly once right after waking, and waking
		 *    before the TCP handshake genuinely finishes makes that read of
		 *    SO_ERROR meaningless (usually still 0), so connect() reported
		 *    "connected" before the connection existed.
		 * Using FD_CONNECT (fires exactly once, when the handshake genuinely
		 * completes) and FD_WRITE (fires when send-buffer space frees up —
		 * reliably, because `write_some`'s retry loop only ever arms this
		 * right after a real send() returned WSAEWOULDBLOCK, never
		 * speculatively, which is exactly the transition FD_WRITE promises to
		 * report) fixes both problems with one mechanism, and removes the
		 * busy-spin-under-backpressure the synthesize-immediately design had.
		 *
		 * single-threaded by the same contract as epoll_reactor for add/
		 * modify/remove/wait (io_context only ever calls those from its own
		 * run() thread) — the one cross-thread caller is the Win32 threadpool
		 * itself invoking on_ready, which only ever calls the inherently
		 * thread-safe PostQueuedCompletionStatus.
		 */
		struct socket_state {
			native_socket_t fd = invalid_native_socket;
			HANDLE port = nullptr;
			OVERLAPPED op{}; // never a real overlapped I/O — just an opaque completion-identity token
			void* user_data = nullptr;
			bool want_read = false;
			bool want_write = false;
			bool retired = false;
			WSAEVENT event = WSA_INVALID_EVENT;
			HANDLE wait_handle = nullptr; // from the legacy RegisterWaitForSingleObject API, not PTP_WAIT
		};

		void CALLBACK on_ready(PVOID context, BOOLEAN) {
			auto* st = static_cast<socket_state*>(context);
			::PostQueuedCompletionStatus(st->port, 0, reinterpret_cast<ULONG_PTR>(st), &st->op);
		}

		class iocp_reactor final : public reactor {
		public:
			iocp_reactor() {
				win32_detail::ensure_wsa_started();
				port_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

				if (!port_)
					throw std::runtime_error("CreateIoCompletionPort failed");
			}

			~iocp_reactor() override {
				if (port_)
					::CloseHandle(port_);
			}

			bool add(native_socket_t fd, bool want_read, bool want_write, void* user_data) noexcept override {
				auto owned = std::make_unique<socket_state>();
				socket_state* st = owned.get();
				st->fd = fd;
				st->port = port_;
				st->user_data = user_data;
				st->event = ::WSACreateEvent();

				if (st->event == WSA_INVALID_EVENT)
					return false;

				states_.emplace(fd, std::move(owned));
				arm(st, want_read, want_write);
				return true;
			}

			bool modify(native_socket_t fd, bool want_read, bool want_write, void* user_data) noexcept override {
				const auto it = states_.find(fd);

				if (it == states_.end())
					return false;

				it->second->user_data = user_data;
				arm(it->second.get(), want_read, want_write);
				return true;
			}

			void remove(native_socket_t fd) noexcept override {
				const auto it = states_.find(fd);

				if (it == states_.end())
					return;

				socket_state* st = it->second.get();
				st->retired = true;

				// blocks until any in-flight on_ready callback for this socket
				// finishes, so it's safe to free st right after.
				if (st->wait_handle)
					::UnregisterWaitEx(st->wait_handle, INVALID_HANDLE_VALUE);

				// WSAEventSelect(..., NULL, 0) must undo the association
				// *before* the event handle is closed — closing it first
				// leaves the socket itself in a broken state (subsequent
				// accept()/recv()/send() on it fails with WSAENOTSOCK). Found
				// the hard way bringing this reactor up — see CLAUDE.md.
				::WSAEventSelect(static_cast<SOCKET>(fd), nullptr, 0);
				::WSACloseEvent(st->event);

				states_.erase(it);
			}

			int wait(ready_event* out_events, int max_events, int timeout_ms) noexcept override {
				int produced = 0;
				DWORD wait_ms = timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms);

				// genuine overlapped completions (TransmitFile) dequeued below are
				// collected here and resumed only after the loop below returns —
				// same deferred-resume shape process_ready_events() already gives
				// the ordinary readiness path, so a resumed coroutine registering
				// new interest (or closing its socket) can never observe this
				// loop, `states_`, or `produced` in a half-updated state.
				std::vector<win32_detail::overlapped_op*> to_resume;

				while (produced < max_events) {
					DWORD bytes = 0;
					ULONG_PTR key = 0;
					OVERLAPPED* ov = nullptr;

					const BOOL ok = ::GetQueuedCompletionStatus(port_, &bytes, &key, &ov, wait_ms);
					wait_ms = 0; // only the first iteration may block; the rest just drain.

					if (ov == nullptr)
						break; // genuine timeout; nothing else queued right now.

					if (ov == &wake_ov_)
						continue; // wake sentinel, never surfaced as a ready_event.

					if (key == 0) {
						// a real overlapped completion, not one of our own
						// synthetic readiness signals — see overlapped_op.hpp.
						auto* op = static_cast<win32_detail::overlapped_op*>(ov);
						op->bytes_transferred = bytes;
						op->error = ok ? 0 : ::GetLastError();
						to_resume.push_back(op);
						continue;
					}

					auto* st = reinterpret_cast<socket_state*>(key);

					if (st->retired)
						continue; // remove() already tore this down; a stale completion.

					WSANETWORKEVENTS net_events{};
					::WSAEnumNetworkEvents(static_cast<SOCKET>(st->fd), st->event, &net_events);

					const long bits = net_events.lNetworkEvents;
					const bool readable = st->want_read && (bits & (FD_READ | FD_ACCEPT | FD_CLOSE)) != 0;
					const bool writable = st->want_write && (bits & (FD_WRITE | FD_CONNECT | FD_CLOSE)) != 0;

					if (!readable && !writable)
						continue; // a bit we're not currently interested in fired; ignore.

					ready_event& out = out_events[produced++];
					out.user_data = st->user_data;
					out.readable = readable;
					out.writable = writable;
				}

				for (win32_detail::overlapped_op* op : to_resume) {
					if (op->waiter)
						op->waiter.resume();
				}

				return produced;
			}

			void wake() noexcept override {
				::PostQueuedCompletionStatus(port_, 0, 0, &wake_ov_);
			}

			void* native_completion_port() noexcept override {
				return port_;
			}

		private:
			void arm(socket_state* st, bool want_read, bool want_write) noexcept {
				st->want_read = want_read;
				st->want_write = want_write;

				if (!want_read && !want_write)
					return;

				long mask = 0;

				if (want_read)
					mask |= FD_READ | FD_ACCEPT | FD_CLOSE;

				if (want_write)
					mask |= FD_WRITE | FD_CONNECT | FD_CLOSE;

				if (st->wait_handle) {
					::UnregisterWaitEx(st->wait_handle, INVALID_HANDLE_VALUE);
					st->wait_handle = nullptr;
				}

				::WSAEventSelect(static_cast<SOCKET>(st->fd), st->event, mask);
				::RegisterWaitForSingleObject(&st->wait_handle, st->event, &on_ready, st, INFINITE, WT_EXECUTEONLYONCE);
			}

			HANDLE port_ = nullptr;
			std::unordered_map<native_socket_t, std::unique_ptr<socket_state>> states_;
			OVERLAPPED wake_ov_{};
		};

	}

	std::unique_ptr<reactor> make_reactor() {
		return std::make_unique<iocp_reactor>();
	}

}
