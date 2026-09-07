#pragma once

// internal-only (never installed), same as sockaddr_convert.hpp in this
// directory — shared between reactor.cpp (which drains completions for
// these) and async/socket.cpp's Windows branch (which issues them).

#include <winsock2.h>
#include <windows.h>
#include <coroutine>

namespace nhttp::platform::win32_detail {

	/**
	 * A genuine overlapped I/O operation (currently only TransmitFile, for
	 * async_socket::send_file's Windows fast path) whose completion should
	 * resume a waiting coroutine once iocp_reactor::wait() dequeues it --
	 * see reactor.cpp. Distinguished there from the reactor's own synthetic
	 * readiness completions (posted via PostQueuedCompletionStatus with a
	 * socket_state* completion key) purely by completion key: a socket
	 * associated for overlapped I/O via CreateIoCompletionPort always uses
	 * key 0 for this purpose (see async/socket.cpp), which the synthetic
	 * path never does (its key is always a live socket_state* heap pointer,
	 * never null).
	 */
	struct overlapped_op : OVERLAPPED {
		std::coroutine_handle<> waiter;
		DWORD bytes_transferred = 0;
		DWORD error = 0; // 0 = success, else a Win32/Winsock error code
	};

}
