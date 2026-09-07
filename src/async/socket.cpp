#include "nhttp/async/socket.hpp"

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
