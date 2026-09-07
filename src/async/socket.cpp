#include "nhttp/async/socket.hpp"

#include <cerrno>
#include <system_error>
#include <utility>

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
			const ssize_t r = handle_.read(buf, n);

			if (r >= 0)
				co_return static_cast<std::size_t>(r);

			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				co_await wait_readable();
				continue;
			}

			if (errno == EINTR)
				continue;

			throw std::system_error(errno, std::generic_category(), "read");
		}
	}

	task<std::size_t> async_socket::write_some(const void* buf, std::size_t n) {
		for (;;) {
			const ssize_t r = handle_.write(buf, n);

			if (r >= 0)
				co_return static_cast<std::size_t>(r);

			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				co_await wait_writable();
				continue;
			}

			if (errno == EINTR)
				continue;

			throw std::system_error(errno, std::generic_category(), "write");
		}
	}

	task<async_socket> async_socket::accept() {
		for (;;) {
			sockaddr_storage addr{};
			socklen_t len = 0;
			const int fd = handle_.accept_raw(addr, len);

			if (fd >= 0)
				co_return async_socket(*ctx_, platform::socket_handle(fd));

			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				co_await wait_readable();
				continue;
			}

			if (errno == EINTR)
				continue;

			throw std::system_error(errno, std::generic_category(), "accept");
		}
	}

	task<bool> async_socket::connect(const platform::endpoint& ep) {
		const platform::connect_result r = handle_.connect_raw(ep);

		if (r == platform::connect_result::failed)
			co_return false;

		if (r == platform::connect_result::connected)
			co_return true;

		co_await wait_writable();
		co_return handle_.socket_error() == 0;
	}

}
