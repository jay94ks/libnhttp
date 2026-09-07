#include "nhttp/server/extensions/reverse_proxy.hpp"
#include "nhttp/server/http1_io.hpp"
#include "nhttp/protocol/urlencode.hpp"
#include "nhttp/async/socket.hpp"
#include "nhttp/io/socket_stream.hpp"

#ifdef NHTTP_HAVE_TLS
#include "nhttp/tls/stream.hpp"
#endif

namespace nhttp::server {

	namespace {

		bool is_hop_by_hop(std::string_view name) noexcept {
			static constexpr std::string_view hop_by_hop[] = {
				"Connection", "Keep-Alive", "Proxy-Authenticate", "Proxy-Authorization",
				"TE", "Trailer", "Transfer-Encoding", "Upgrade",
			};

			for (const std::string_view h : hop_by_hop) {
				if (protocol::header_name_equals(name, h))
					return true;
			}

			return false;
		}

		void copy_relayable_headers(const protocol::http_headers& from, protocol::http_headers& to) {
			for (const protocol::http_header& h : from) {
				if (!is_hop_by_hop(h.name))
					to.add(h.name, h.value);
			}
		}

		bool wants_upgrade(const protocol::http_headers& headers) noexcept {
			if (!headers.isset(protocol::header_names::UPGRADE))
				return false;

			const std::string* conn = headers.get(protocol::header_names::CONNECTION);
			return conn && protocol::header_value_contains_token(*conn, "upgrade");
		}

		/* splices `from` -> `to`, forwarding `leftover` first. holds shared_ptr
		 * copies of both streams so they outlive the connection object that
		 * kicked this off (see response::upgrade_handler's contract) — this and
		 * its mirror-direction sibling are launched as two independent
		 * detached_tasks, deliberately not joined: `active_connections_`
		 * under-counts a connection mid-splice as a result, a known,
		 * acceptable simplification (see CLAUDE.md) rather than adding a
		 * bespoke two-completion join for this one call site. */
		async::detached_task pump(std::shared_ptr<io::stream> from, std::shared_ptr<io::stream> to, std::string leftover) {
			try {
				if (!leftover.empty())
					co_await http1_io::write_all(*to, leftover.data(), leftover.size());

				char buf[4096];

				for (;;) {
					const std::size_t n = co_await from->read(buf, sizeof(buf));

					if (n == 0)
						break;

					co_await http1_io::write_all(*to, buf, n);
				}
			}
			catch (...) {
				// either side closing/erroring just ends this direction of the splice.
			}

			co_await to->close();
		}

		std::string build_request_target(std::string_view path, const protocol::http_query_string& query) {
			std::string target;

			if (path.empty())
				target = "/";
			else if (path.front() != '/')
				target = "/" + std::string(path);
			else
				target = std::string(path);

			bool first = true;

			for (const auto& [key, value] : query) {
				target += first ? '?' : '&';
				first = false;
				target += protocol::url_encode(key);
				target += '=';
				target += protocol::url_encode(value);
			}

			return target;
		}

	}

	reverse_proxy::reverse_proxy(std::string mount_path, std::vector<upstream> upstreams, std::uint32_t prio)
		: vpath(std::move(mount_path), prio), upstreams_(std::move(upstreams))
	{
	}

	const upstream& reverse_proxy::pick_upstream() noexcept {
		const std::size_t i = next_upstream_.fetch_add(1, std::memory_order_relaxed) % upstreams_.size();
		return upstreams_[i];
	}

	async::task<std::optional<response>> reverse_proxy::on_handle(request& req) {
		if (upstreams_.empty())
			co_return std::nullopt;

		const upstream& up = pick_upstream();

		// a fresh connection per proxied request/upgrade (no pooling in this
		// pass — see the class doc comment); opened on THIS request's own
		// io_context, never one captured at construction time, for the same
		// reason overlay/single_file do (CLAUDE.md's Phase-9 finding).
		async::async_socket sock(*req.io_ctx, platform::socket_handle::create(up.address.address().version(), platform::transport::tcp));

		if (!co_await sock.connect(up.address))
			co_return make_response(502);

		auto raw_wire = std::make_shared<io::socket_stream>(std::move(sock));
		raw_wire->socket().native().set_nodelay(true);

		std::shared_ptr<io::stream> wire = raw_wire;

#ifdef NHTTP_HAVE_TLS
		if (up.use_tls) {
			std::shared_ptr<tls::tls_context> tls_ctx = tls::tls_context::create_client(up.verify_tls_cert);

			if (!tls_ctx)
				co_return make_response(502);

			auto tls_wire = std::make_shared<tls::tls_stream>(raw_wire, tls_ctx);
			const std::string sni = up.tls_sni_hostname.empty() ? up.address.address().to_string() : up.tls_sni_hostname;

			if (!co_await tls_wire->connect(sni))
				co_return make_response(502);

			wire = tls_wire;
		}
#else
		if (up.use_tls)
			co_return make_response(502); // built without NHTTP_ENABLE_TLS.
#endif

		// --- relay the request upstream ---
		const bool upgrade = wants_upgrade(req.headers);
		const bool original_has_body = req.headers.isset(protocol::header_names::CONTENT_LENGTH) ||
			req.headers.isset(protocol::header_names::TRANSFER_ENCODING);

		protocol::http_headers out_headers;
		copy_relayable_headers(req.headers, out_headers);
		out_headers.set(std::string(protocol::header_names::HOST), up.address.to_string());
		out_headers.add("X-Forwarded-For", req.hostname.empty() ? "unknown" : req.hostname);
		out_headers.add("X-Forwarded-Host", req.hostname);
		out_headers.add("X-Forwarded-Proto", "http");

		if (upgrade) {
			out_headers.set(std::string(protocol::header_names::CONNECTION), "Upgrade");
			out_headers.set(std::string(protocol::header_names::UPGRADE), *req.headers.get(protocol::header_names::UPGRADE));
		}
		else {
			out_headers.set(std::string(protocol::header_names::CONNECTION), "close");
		}

		std::int64_t out_content_length = -1; // -1 == chunked

		if (original_has_body) {
			if (const std::string* cl = req.headers.get(protocol::header_names::CONTENT_LENGTH)) {
				out_content_length = 0;

				for (const char c : *cl) {
					if (c < '0' || c > '9') { out_content_length = -1; break; }
					out_content_length = out_content_length * 10 + (c - '0');
				}
			}

			if (out_content_length >= 0)
				out_headers.set(std::string(protocol::header_names::CONTENT_LENGTH), std::to_string(out_content_length));
			else
				out_headers.set(std::string(protocol::header_names::TRANSFER_ENCODING), "chunked");
		}

		std::string head = req.method().name();
		head += ' ';
		head += build_request_target(subpath_of(req), req.resource.query);
		head += " HTTP/1.1\r\n";
		out_headers.write_to(head);
		head += "\r\n";

		co_await http1_io::write_all(*wire, head.data(), head.size());

		if (original_has_body)
			co_await http1_io::write_message_body(*wire, *req.body, out_content_length);

		// --- read the upstream's response ---
		std::string upstream_leftover;
		protocol::http_status upstream_status;
		int upstream_major = 1, upstream_minor = 1;
		char first_chunk[4096];
		std::size_t got = co_await wire->read(first_chunk, sizeof(first_chunk));

		if (got == 0)
			co_return make_response(502);

		upstream_leftover.append(first_chunk, got);

		std::ptrdiff_t consumed;

		for (;;) {
			consumed = protocol::http_status::try_parse(upstream_leftover.data(), upstream_leftover.size(), upstream_status, upstream_major, upstream_minor);

			if (consumed > 0) {
				upstream_leftover.erase(0, static_cast<std::size_t>(consumed));
				break;
			}

			if (consumed < 0)
				co_return make_response(502);

			got = co_await wire->read(first_chunk, sizeof(first_chunk));

			if (got == 0)
				co_return make_response(502);

			upstream_leftover.append(first_chunk, got);
		}

		protocol::http_headers upstream_headers;

		if (!co_await http1_io::read_headers(upstream_leftover, *wire, upstream_headers, 64 * 1024))
			co_return make_response(502);

		response resp;
		resp.status = upstream_status;
		copy_relayable_headers(upstream_headers, resp.headers);

		if (upgrade && upstream_status.code() == 101) {
			// raw byte splice from here on — the same protocol-upgrade
			// mechanism websocket_endpoint uses (response::upgrade_handler).
			resp.headers.set(std::string(protocol::header_names::CONNECTION), "Upgrade");

			if (const std::string* up_upgrade = upstream_headers.get(protocol::header_names::UPGRADE))
				resp.headers.set(std::string(protocol::header_names::UPGRADE), *up_upgrade);

			resp.upgrade_handler = [wire, upstream_leftover](std::shared_ptr<io::stream> downstream, std::string downstream_leftover) -> async::task<void> {
				pump(wire, downstream, upstream_leftover);
				pump(downstream, wire, std::move(downstream_leftover));
				co_return;
			};

			co_return resp;
		}

		resp.body = http1_io::make_owned_body_stream(std::move(upstream_leftover), wire, upstream_headers);

		if (!resp.body)
			co_return make_response(502);

		if (const std::string* cl = upstream_headers.get(protocol::header_names::CONTENT_LENGTH)) {
			std::int64_t length = 0;

			for (const char c : *cl) {
				if (c < '0' || c > '9') { length = -1; break; }
				length = length * 10 + (c - '0');
			}

			resp.content_length = length;
		}
		else if (upstream_headers.isset(protocol::header_names::TRANSFER_ENCODING)) {
			resp.content_length = -1;
		}
		else {
			resp.content_length = 0;
		}

		co_return resp;
	}

}
