#include "nhttp/async/socket.hpp"

#include <system_error>
#include <utility>

#if defined(_WIN32)
#include "../platform/win32/overlapped_op.hpp"

#include <mswsock.h>
#include <io.h>
#include <algorithm>
#include <atomic>
#include <limits>
#endif

namespace nhttp::async {

	async_socket::async_socket(io_context& ctx, platform::socket_handle handle)
		: ctx_(&ctx), handle_(std::move(handle)), reg_(std::make_unique<io_registration>())
	{
		handle_.set_nonblocking(true);
		reg_->fd = handle_.native_handle();
	}

	async_socket::~async_socket() {
		close();
	}

	async_socket::async_socket(async_socket&& other) noexcept
		: ctx_(other.ctx_), handle_(std::move(other.handle_)), reg_(std::move(other.reg_))
	{
		other.ctx_ = nullptr;
	}

	async_socket& async_socket::operator=(async_socket&& other) noexcept {
		if (this != &other) {
			close();

			ctx_ = other.ctx_;
			handle_ = std::move(other.handle_);
			reg_ = std::move(other.reg_);
			other.ctx_ = nullptr;
		}

		return *this;
	}

	void async_socket::close() {
		if (reg_ && ctx_)
			ctx_->forget(*reg_);

		handle_.close();
		reg_.reset();
	}

	task<std::size_t> async_socket::read_some(void* buf, std::size_t n) {
		for (;;) {
			const std::int64_t r = handle_.read(buf, n);

			if (r >= 0)
				co_return static_cast<std::size_t>(r);

			if (platform::would_block()) {
				co_await wait_readable();
				continue;
			}

			if (platform::was_interrupted())
				continue;

			throw std::system_error(platform::last_socket_error(), std::generic_category(), "read: " + platform::describe_socket_error(platform::last_socket_error()));
		}
	}

	task<std::size_t> async_socket::write_some(const void* buf, std::size_t n) {
		for (;;) {
			const std::int64_t r = handle_.write(buf, n);

			if (r >= 0)
				co_return static_cast<std::size_t>(r);

			if (platform::would_block()) {
				co_await wait_writable();
				continue;
			}

			if (platform::was_interrupted())
				continue;

			throw std::system_error(platform::last_socket_error(), std::generic_category(), "write: " + platform::describe_socket_error(platform::last_socket_error()));
		}
	}

#if defined(_WIN32)

	namespace {

		/* the function pointer itself is only obtainable via a per-socket
		 * WSAIoctl call, but its value is process-stable in practice (every
		 * real-world caller of this pattern — nginx, IIS's own networking
		 * layer — caches it the same way); std::atomic instead of a plain
		 * static avoids a benign-but-real data race on the (idempotent)
		 * write. */
		LPFN_TRANSMITFILE get_transmitfile_fn(SOCKET s) noexcept {
			static std::atomic<LPFN_TRANSMITFILE> cached{ nullptr };

			if (LPFN_TRANSMITFILE fn = cached.load(std::memory_order_acquire))
				return fn;

			GUID guid = WSAID_TRANSMITFILE;
			LPFN_TRANSMITFILE fn = nullptr;
			DWORD bytes = 0;

			if (::WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
					&fn, sizeof(fn), &bytes, nullptr, nullptr) != 0) {
				return nullptr;
			}

			cached.store(fn, std::memory_order_release);
			return fn;
		}

		/**
		 * the Windows fast path: TransmitFile over a genuine overlapped
		 * completion (see platform::socket_handle::supports_send_file()'s
		 * comment for why this can't be the same would-block/retry shape
		 * write_some() uses). `op`'s completion is dequeued and resumed by
		 * iocp_reactor::wait() (src/platform/win32/reactor.cpp) — this
		 * awaiter and that loop agree on the contract via overlapped_op.hpp.
		 */
		struct send_file_awaiter {
			async_socket& self;
			int in_fd;
			std::int64_t& offset;
			std::size_t count;
			platform::win32_detail::overlapped_op op{};

			bool await_ready() const noexcept { return false; }

			bool await_suspend(std::coroutine_handle<> h) {
				op.waiter = h;
				op.Offset = static_cast<DWORD>(static_cast<std::uint64_t>(offset) & 0xFFFFFFFFu);
				op.OffsetHigh = static_cast<DWORD>(static_cast<std::uint64_t>(offset) >> 32);

				const SOCKET s = static_cast<SOCKET>(self.native().native_handle());

				// idempotent: a socket can only ever be associated with a port
				// once, so every call after the first fails harmlessly (and
				// its result is never checked) — see CLAUDE.md's phase log for
				// why this is simpler and just as correct as tracking
				// "already associated" per-socket.
				::CreateIoCompletionPort(reinterpret_cast<HANDLE>(s),
					static_cast<HANDLE>(self.context().native_completion_port()), 0, 0);

				const HANDLE file_handle = reinterpret_cast<HANDLE>(::_get_osfhandle(in_fd));
				const LPFN_TRANSMITFILE fn = get_transmitfile_fn(s);

				if (!fn || file_handle == INVALID_HANDLE_VALUE) {
					op.error = WSAEOPNOTSUPP;
					return false; // resume immediately; nothing was started.
				}

				const DWORD to_write = static_cast<DWORD>((std::min<std::size_t>)(count, 0x7FFFFFFEu));

				if (!fn(s, file_handle, to_write, 0, &op, nullptr, 0)) {
					const int err = ::WSAGetLastError();

					if (err != WSA_IO_PENDING) {
						op.error = static_cast<DWORD>(err);
						return false; // genuine failure; nothing was started.
					}
				}

				// either already TRUE (completes synchronously) or pending —
				// either way a completion packet is still queued to the port
				// (nothing here calls SetFileCompletionNotificationModes'
				// FILE_SKIP_COMPLETION_PORT_ON_SUCCESS), so always suspend and
				// let iocp_reactor::wait() deliver it.
				return true;
			}

			std::size_t await_resume() const {
				if (op.error != 0) {
					throw std::system_error(static_cast<int>(op.error), std::system_category(),
						"send_file (TransmitFile): " + platform::describe_socket_error(static_cast<int>(op.error)));
				}

				offset += static_cast<std::int64_t>(op.bytes_transferred);
				return op.bytes_transferred;
			}
		};

	}

	task<std::size_t> async_socket::send_file(int in_fd, std::int64_t& offset, std::size_t count) {
		co_return co_await send_file_awaiter{ *this, in_fd, offset, count };
	}

#else

	task<std::size_t> async_socket::send_file(int in_fd, std::int64_t& offset, std::size_t count) {
		for (;;) {
			const std::int64_t r = handle_.send_file(in_fd, offset, count);

			if (r >= 0)
				co_return static_cast<std::size_t>(r);

			if (platform::would_block()) {
				co_await wait_writable();
				continue;
			}

			if (platform::was_interrupted())
				continue;

			throw std::system_error(platform::last_socket_error(), std::generic_category(), "send_file: " + platform::describe_socket_error(platform::last_socket_error()));
		}
	}

#endif

	task<async_socket> async_socket::accept() {
		for (;;) {
			auto accepted = handle_.accept();

			if (accepted)
				co_return async_socket(*ctx_, std::move(accepted->first));

			if (platform::would_block()) {
				co_await wait_readable();
				continue;
			}

			if (platform::was_interrupted())
				continue;

			throw std::system_error(platform::last_socket_error(), std::generic_category(), "accept: " + platform::describe_socket_error(platform::last_socket_error()));
		}
	}

	task<bool> async_socket::connect(const platform::endpoint& ep) {
		const platform::connect_result r = handle_.connect(ep);

		if (r == platform::connect_result::failed)
			co_return false;

		if (r == platform::connect_result::connected)
			co_return true;

		co_await wait_writable();
		co_return handle_.socket_error() == 0;
	}

}
