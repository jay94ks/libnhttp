#pragma once

#include "stream.hpp"
#include "../async/socket.hpp"

namespace nhttp::io {

	/**
	 * class socket_stream.
	 * presents a live async_socket connection as a stream, so protocol drivers
	 * can talk to "the wire" through the same abstraction used for files and
	 * memory — the transport-agnostic seam future HTTP/2 and QUIC drivers rely
	 * on (see CONCEPTS.md / CLAUDE.md's architecture decisions).
	 */
	class socket_stream final : public stream {
	public:
		explicit socket_stream(async::async_socket socket) noexcept : socket_(std::move(socket)) { }

	public:
		std::int64_t get_length() const override { return -1; }
		bool can_seek() const noexcept override { return false; }

		async::task<std::int64_t> seek(std::int64_t offset, seek_origin origin) override;
		async::task<std::size_t> read(void* buf, std::size_t n) override { return socket_.read_some(buf, n); }
		async::task<std::size_t> write(const void* buf, std::size_t n) override { return socket_.write_some(buf, n); }
		async::task<void> flush() override;
		async::task<void> close() override;

	public:
		async::async_socket& socket() noexcept { return socket_; }

	private:
		async::async_socket socket_;
	};

}
