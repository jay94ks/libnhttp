#include <catch2/catch_test_macros.hpp>

#include "nhttp/server/listener.hpp"
#include "nhttp/server/extensions/overlay.hpp"
#include "nhttp/server/extensions/single_file.hpp"
#include "nhttp/server/extensions/vhost.hpp"
#include "nhttp/server/extensions/vpath.hpp"
#include "../support/raw_http_client.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace nhttp::server;
using namespace nhttp::platform;
using nhttp_test::raw_http_client;

namespace {

	void write_file(const std::string& path, const std::string& content) {
		std::ofstream f(path, std::ios::binary);
		f << content;
	}

	struct temp_dir {
		std::string path;

		temp_dir() {
			path = (std::filesystem::temp_directory_path() / "nhttp_test_overlay_dir").string();
			std::filesystem::create_directory(path);
		}

		~temp_dir() {
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		}
	};

	struct running_server {
		listener srv;
		std::thread thread;

		explicit running_server() : srv(make_params()) {
			const ip_address addr = ip_address::any_v4();
			REQUIRE(srv.listen(endpoint(addr, 0)));

			thread = std::thread([this] { srv.run(); });
		}

		~running_server() {
			srv.stop();
			thread.join();
		}

		std::uint16_t port() const { return srv.local_endpoint()->port(); }

	private:
		static params make_params() {
			params p;
			p.io_worker_count = 2;
			p.blocking_pool_size = 2;
			return p;
		}
	};

}

TEST_CASE("overlay serves index.html at the root and named files by path", "[integration][extensions][overlay]") {
	temp_dir dir;
	write_file(dir.path + "/index.html", "<h1>home</h1>");
	write_file(dir.path + "/another.txt", "just another file");

	running_server server;
	server.srv.extends(overlay_of(dir.path, "index.html", server.srv.blocking_pool()));

	raw_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	auto root = client.send("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(root.status == 200);
	REQUIRE(root.body == "<h1>home</h1>");

	raw_http_client client2(ip_version::v4, server.port());
	auto other = client2.send("GET /another.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(other.status == 200);
	REQUIRE(other.body == "just another file");

	raw_http_client client3(ip_version::v4, server.port());
	auto missing = client3.send("GET /missing.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	// overlay's wants() checks real file existence, so a genuinely missing file
	// means overlay declines entirely (unlike a path-traversal attempt, which it
	// still claims to give a definitive 403) — nothing else is registered here,
	// so the listener's own no-handler-set fallback (501) is what answers.
	REQUIRE(missing.status == 501);
}

TEST_CASE("overlay rejects a path-traversal attempt", "[integration][extensions][overlay]") {
	temp_dir dir;
	write_file(dir.path + "/index.html", "<h1>home</h1>");

	running_server server;
	server.srv.extends(overlay_of(dir.path, "index.html", server.srv.blocking_pool()));

	raw_http_client client(ip_version::v4, server.port());
	REQUIRE(client.connected());

	auto resp = client.send("GET /../../../../etc/passwd HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

	REQUIRE(resp.status == 403);
}

TEST_CASE("overlay supports conditional GET via ETag and byte-Range requests", "[integration][extensions][overlay]") {
	temp_dir dir;
	write_file(dir.path + "/another.txt", "0123456789");

	running_server server;
	server.srv.extends(overlay_of(dir.path, "index.html", server.srv.blocking_pool()));

	raw_http_client first(ip_version::v4, server.port());
	auto initial = first.send("GET /another.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(initial.status == 200);
	const std::string etag = initial.header("ETag");
	REQUIRE_FALSE(etag.empty());

	raw_http_client second(ip_version::v4, server.port());
	auto not_modified = second.send(
		"GET /another.txt HTTP/1.1\r\nHost: localhost\r\nIf-None-Match: " + etag + "\r\nConnection: close\r\n\r\n");
	REQUIRE(not_modified.status == 304);

	raw_http_client third(ip_version::v4, server.port());
	auto ranged = third.send(
		"GET /another.txt HTTP/1.1\r\nHost: localhost\r\nRange: bytes=2-5\r\nConnection: close\r\n\r\n");
	REQUIRE(ranged.status == 206);
	REQUIRE(ranged.body == "2345");
	REQUIRE(ranged.header("Content-Range") == "bytes 2-5/10");
}

TEST_CASE("vhost dispatches different content based on the Host header", "[integration][extensions][vhost]") {
	const std::string path_a = (std::filesystem::temp_directory_path() / "nhttp_test_vhost_a.txt").string();
	const std::string path_b = (std::filesystem::temp_directory_path() / "nhttp_test_vhost_b.txt").string();
	write_file(path_a, "site A");
	write_file(path_b, "site B");

	running_server server;

	auto vhost_a = vhost_for(std::string("a.example.com"));
	vhost_a->extends(file_of(path_a, server.srv.blocking_pool()));
	server.srv.extends(vhost_a);

	auto vhost_b = vhost_for(std::string("b.example.com"));
	vhost_b->extends(file_of(path_b, server.srv.blocking_pool()));
	server.srv.extends(vhost_b);

	raw_http_client client_a(ip_version::v4, server.port());
	auto resp_a = client_a.send("GET / HTTP/1.1\r\nHost: a.example.com\r\nConnection: close\r\n\r\n");
	REQUIRE(resp_a.status == 200);
	REQUIRE(resp_a.body == "site A");

	raw_http_client client_b(ip_version::v4, server.port());
	auto resp_b = client_b.send("GET / HTTP/1.1\r\nHost: b.example.com\r\nConnection: close\r\n\r\n");
	REQUIRE(resp_b.status == 200);
	REQUIRE(resp_b.body == "site B");

	raw_http_client client_c(ip_version::v4, server.port());
	auto resp_c = client_c.send("GET / HTTP/1.1\r\nHost: c.example.com\r\nConnection: close\r\n\r\n");
	REQUIRE(resp_c.status == 501); // no vhost matched, and no fallback handler is set

	std::remove(path_a.c_str());
	std::remove(path_b.c_str());
}

TEST_CASE("vpath scopes a nested overlay under a URL prefix", "[integration][extensions][vpath]") {
	temp_dir dir;
	write_file(dir.path + "/index.html", "mounted content");

	running_server server;

	auto mount = vpath_for("/static");
	mount->extends(overlay_of(dir.path, "index.html", server.srv.blocking_pool()));
	server.srv.extends(mount);

	raw_http_client client(ip_version::v4, server.port());
	auto matched = client.send("GET /static/index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(matched.status == 200);
	REQUIRE(matched.body == "mounted content");

	raw_http_client client2(ip_version::v4, server.port());
	auto unmatched = client2.send("GET /elsewhere/index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
	REQUIRE(unmatched.status == 501); // no extension claimed it, no fallback handler set either
}
