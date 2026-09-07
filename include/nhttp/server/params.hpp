#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

namespace nhttp::server {

	/* reserved for a future round — present in the config shape so enabling TLS
	 * later doesn't change the params API (see CONCEPTS.md/CLAUDE.md). */
	struct tls_params {
		bool enabled = false;
	};

	struct params {
		std::size_t io_worker_count = std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 2;
		std::size_t blocking_pool_size = 4;
		std::size_t max_connections = 10000;
		std::size_t per_connection_buffer_size = 8192;

		std::chrono::milliseconds header_timeout{ 10000 };
		std::chrono::milliseconds idle_timeout{ 60000 };
		std::chrono::milliseconds keep_alive_timeout{ 15000 };

		std::size_t max_header_size = 16 * 1024;
		std::size_t max_request_line_size = 8 * 1024;

		bool tcp_nodelay = true;
		bool tcp_keepalive = true;
		bool tcp_linger = false;
		int tcp_linger_seconds = 0;

		std::string server_header_value = "nhttp/2.0";

		tls_params tls;
	};

}
