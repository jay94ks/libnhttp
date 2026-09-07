#include <catch2/catch_test_macros.hpp>

#include "nhttp/server/listener.hpp"
#include "nhttp/http2/frame.hpp"
#include "nhttp/http2/hpack.hpp"

#include <cstring>
#include <thread>
#include <unordered_map>

using namespace nhttp::server;
using namespace nhttp::platform;
using namespace nhttp::async;
using namespace nhttp::http2;

namespace {

	params test_params() {
		params p;
		p.io_worker_count = 2;
		p.blocking_pool_size = 1;
		return p;
	}

	struct running_server {
		listener srv;
		std::thread thread;

		running_server() : srv(test_params()) {
			srv.set_handler([](request& req) -> task<response> {
				if (req.path() == "/echo") {
					std::string body;
					co_await req.body->read_all(body);
					co_return make_response(std::move(body));
				}

				co_return make_response("path=" + req.path());
			});

			REQUIRE(srv.listen(endpoint(ip_address::loopback_v4(), 0)));
			thread = std::thread([this] { srv.run(); });
		}

		~running_server() {
			srv.stop();
			thread.join();
		}

		std::uint16_t port() const { return srv.local_endpoint()->port(); }
	};

	/* a minimal blocking HTTP/2 client using this library's own frame/HPACK
	 * codec (already unit-tested against RFC 7541 vectors in test_hpack.cpp)
	 * — reused here deliberately, unlike the HTTP/1.1 test clients'
	 * independent-parser philosophy, since a full independent HTTP/2 client
	 * (its own HPACK+framing) is out of proportion for an integration test
	 * whose actual target is connection_h2's driver logic, not the codec. */
	class raw_h2_client {
	public:
		explicit raw_h2_client(std::uint16_t port) {
			sock_ = socket_handle::create(ip_version::v4, transport::tcp);

			if (sock_.connect(endpoint(ip_address::loopback_v4(), port)) != connect_result::connected)
				return;

			send_all(client_preface);
			send_frame(frame_type::settings, 0, 0, std::string_view()); // empty client SETTINGS

			// the server replies with its own SETTINGS; consume it (and its ACK
			// of ours) before issuing any requests.
			frame_header fh;
			REQUIRE(recv_frame_header(fh));
			REQUIRE(fh.type == frame_type::settings);
			std::string settings_ack_payload;
			send_frame(frame_type::settings, frame_flags::ack, 0, settings_ack_payload);

			connected_ = true;
		}

		~raw_h2_client() { sock_.close(); }

		bool connected() const noexcept { return connected_; }

		/* sends a request (HEADERS[+DATA]) on a fresh client-initiated stream
		 * id and returns it; call recv_response() to read the reply. */
		std::uint32_t send_request(const std::string& method, const std::string& path, const std::string& body = std::string()) {
			const std::uint32_t stream_id = next_stream_id_;
			next_stream_id_ += 2;

			header_list fields{
				{ ":method", method },
				{ ":scheme", "http" },
				{ ":path", path },
				{ ":authority", "localhost" },
			};

			std::string block;
			hpack_encoder_.encode(fields, block);

			const std::uint8_t flags = body.empty()
				? static_cast<std::uint8_t>(frame_flags::end_headers | frame_flags::end_stream)
				: frame_flags::end_headers;

			send_frame(frame_type::headers, flags, stream_id, block);

			if (!body.empty())
				send_frame(frame_type::data, frame_flags::end_stream, stream_id, body);

			return stream_id;
		}

		struct response_result {
			int status = 0;
			std::string body;
		};

		/* reads frames (across however many streams are in flight) until
		 * `target_stream` has a complete response — frames belonging to
		 * *other* streams (e.g. when multiple requests are in flight at
		 * once) are buffered, not discarded, so a later recv_response() call
		 * for that other stream still sees them. */
		response_result recv_response(std::uint32_t target_stream) {
			response_result result;
			bool got_status = false;
			bool end_stream_seen = false;

			// drain anything already buffered for this stream from an earlier call.
			auto buffered_it = pending_.find(target_stream);

			if (buffered_it != pending_.end()) {
				for (auto& [fh, payload] : buffered_it->second)
					apply_frame(fh, payload, result, got_status, end_stream_seen);

				pending_.erase(buffered_it);
			}

			while (!end_stream_seen) {
				frame_header fh;
				if (!recv_frame_header(fh))
					break;

				std::vector<std::uint8_t> payload(fh.length);
				if (fh.length > 0)
					recv_exact(payload.data(), fh.length);

				if (fh.type == frame_type::settings && !(fh.flags & frame_flags::ack)) {
					send_frame(frame_type::settings, frame_flags::ack, 0, std::string_view());
					continue;
				}

				if (fh.type == frame_type::window_update || fh.type == frame_type::ping)
					continue; // not relevant to this simple client's bookkeeping

				if (fh.stream_id != target_stream) {
					pending_[fh.stream_id].emplace_back(fh, std::move(payload));
					continue;
				}

				apply_frame(fh, payload, result, got_status, end_stream_seen);
			}

			REQUIRE(got_status);
			return result;
		}

	private:
		void apply_frame(const frame_header& fh, const std::vector<std::uint8_t>& payload,
			response_result& result, bool& got_status, bool& end_stream_seen)
		{
			if (fh.type == frame_type::headers) {
				header_list fields;
				REQUIRE(hpack_decoder_.decode(payload.data(), payload.size(), fields));

				for (const header_field& f : fields) {
					if (f.name == ":status") {
						result.status = std::atoi(f.value.c_str());
						got_status = true;
					}
				}

				if (fh.flags & frame_flags::end_stream)
					end_stream_seen = true;
			}
			else if (fh.type == frame_type::data) {
				result.body.append(reinterpret_cast<const char*>(payload.data()), payload.size());

				if (fh.flags & frame_flags::end_stream)
					end_stream_seen = true;
			}
		}

		void send_frame(frame_type type, std::uint8_t flags, std::uint32_t stream_id, std::string_view payload) {
			frame_header fh;
			fh.length = static_cast<std::uint32_t>(payload.size());
			fh.type = type;
			fh.flags = flags;
			fh.stream_id = stream_id;

			std::string out;
			fh.write(out);
			out += payload;
			send_all(out);
		}

		bool recv_frame_header(frame_header& out) {
			std::uint8_t buf[frame_header::wire_size];

			if (!recv_exact(buf, sizeof(buf)))
				return false;

			frame_header::parse(buf, out);
			return true;
		}

		bool recv_exact(void* buf, std::size_t n) {
			auto* p = static_cast<std::uint8_t*>(buf);
			std::size_t got = 0;

			while (got < n) {
				const std::int64_t r = sock_.read(p + got, n - got);
				if (r <= 0) return false;
				got += static_cast<std::size_t>(r);
			}

			return true;
		}

		void send_all(std::string_view data) {
			std::size_t sent = 0;

			while (sent < data.size()) {
				const std::int64_t n = sock_.write(data.data() + sent, data.size() - sent);
				if (n <= 0) break;
				sent += static_cast<std::size_t>(n);
			}
		}

		socket_handle sock_;
		hpack_encoder hpack_encoder_;
		hpack_decoder hpack_decoder_;
		std::uint32_t next_stream_id_ = 1;
		bool connected_ = false;
		std::unordered_map<std::uint32_t, std::vector<std::pair<frame_header, std::vector<std::uint8_t>>>> pending_;
	};

}

TEST_CASE("connection_h2 serves a single prior-knowledge request/response", "[integration][http2]") {
	running_server server;
	raw_h2_client client(server.port());
	REQUIRE(client.connected());

	const std::uint32_t stream = client.send_request("GET", "/widgets");
	const auto resp = client.recv_response(stream);

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == "path=/widgets");
}

TEST_CASE("connection_h2 relays a POST body", "[integration][http2]") {
	running_server server;
	raw_h2_client client(server.port());
	REQUIRE(client.connected());

	const std::uint32_t stream = client.send_request("POST", "/echo", "hello over http/2");
	const auto resp = client.recv_response(stream);

	REQUIRE(resp.status == 200);
	REQUIRE(resp.body == "hello over http/2");
}

TEST_CASE("connection_h2 multiplexes two concurrent streams on one connection", "[integration][http2]") {
	running_server server;
	raw_h2_client client(server.port());
	REQUIRE(client.connected());

	const std::uint32_t stream_a = client.send_request("GET", "/alpha");
	const std::uint32_t stream_b = client.send_request("GET", "/beta");

	const auto resp_b = client.recv_response(stream_b);
	const auto resp_a = client.recv_response(stream_a);

	REQUIRE(resp_a.status == 200);
	REQUIRE(resp_a.body == "path=/alpha");
	REQUIRE(resp_b.status == 200);
	REQUIRE(resp_b.body == "path=/beta");
}
