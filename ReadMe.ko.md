# libnhttp

**Language**: [English](ReadMe.md) | 한국어

<p align="center">
<img src="https://raw.githack.com/jay94ks/libnhttp/main/logo.png" />
</p>

멀티쓰레드 epoll 리액터 위에 구축된, C++20 코루틴 기반의 이벤트 드리븐 HTTP/1.1 서버 라이브러리입니다.
현재는 Linux만 지원합니다 (Windows 지원은 의도적으로 뒤로 미뤄뒀습니다 — [CLAUDE.md](CLAUDE.ko.md) 참고).

이 라이브러리는 기존 libnhttp를 처음부터 완전히 다시 설계한 결과물입니다: 기능 집합과 설계 철학은
동일하게 유지하되, 기존 구현 코드는 하나도 그대로 가져오지 않았습니다. 무엇이 왜 바뀌었는지, 그리고
일부 코드가 의도적으로 방지하고 있는 과거의 버그들은 [CONCEPTS.md](CONCEPTS.ko.md),
[USAGE.md](USAGE.ko.md), [docs/protocol-extensibility.md](docs/protocol-extensibility.ko.md)에
문서화되어 있습니다.

**목차**

* [라이선스](#라이선스)
* [요구 사항](#요구-사항)
* [빌드](#빌드)
* [빠른 시작 예제](#빠른-시작-예제)
* [아키텍처 개요](#아키텍처-개요)
* [정적 파일 서빙](#정적-파일-서빙)
* [가상 호스팅](#가상-호스팅)
* [라우팅 (`router` 모듈)](#라우팅-router-모듈)
* [WebSocket](#websocket)
* [TLS/SSL](#tlsssl)
* [설계 문서](#설계-문서)

## 라이선스

```
MIT License

Copyright (c) 2021 Jay (jay94ks@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

이번 재설계에서는 어떤 서드파티 코드도 그대로 가져다 쓰지(vendoring) 않았습니다 — WebSocket
핸드셰이크에 필요한 SHA-1/base64도 같은 라이선스 하에 자체 구현되어 있습니다
(`src/ws/sha1.cpp` 참고).

## 요구 사항

* Linux (epoll 기반 리액터; 이번 라운드에서는 Windows 미지원)
* GCC ≥ 11 또는 Clang ≥ 14 (C++20 코루틴)
* CMake ≥ 3.20
* OpenSSL (TLS/SSL 지원용; 비활성화하려면 아래 `NHTTP_ENABLE_TLS` 참고)

## 빌드

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

빌드 옵션 (구성 시점에 `-D<옵션>=ON|OFF`):

| 옵션 | 기본값 | 의미 |
|---|---|---|
| `NHTTP_BUILD_TESTS` | `ON` | Catch2 테스트 스위트를 빌드 |
| `NHTTP_BUILD_EXAMPLES` | `ON` | `examples/nhttpd`를 빌드 |
| `NHTTP_WARNINGS_AS_ERRORS` | `ON` | 컴파일러 경고를 에러로 처리 |
| `NHTTP_ENABLE_TLS` | `ON` | TLS/SSL 지원을 빌드 (OpenSSL 필요) |

이 라이브러리는 `-Wall -Wextra -Wpedantic`(그리고 그 외 몇 가지, `cmake/CompilerWarnings.cmake`
참고) 기준으로 경고 없이 빌드됩니다 — 이는 지향점이 아니라 반드시 지켜야 하는 요구 사항입니다.

## 빠른 시작 예제

```cpp
#include "nhttp/server/listener.hpp"
#include "nhttp/server/extensions/overlay.hpp"
#include "nhttp/router/router.hpp"

using namespace nhttp::server;
using namespace nhttp::router;
using namespace nhttp::platform;

int main() {
	listener srv;

	// "." 아래의 정적 파일을 서빙 (디렉터리는 index.html로 폴백).
	srv.extends(overlay_of(".", "index.html", srv.blocking_pool()));

	// 작은 REST API.
	auto api = make_router();

	api->get("whoami", target_by([](request&) {
		return make_response("I'm jay.");
	}));

	api->get("/:user", target_by([](request& req) {
		return make_response(route_of(req).captures.at(":user") + " is ...");
	}));

	srv.extends(api);

	if (!srv.listen(endpoint(ip_address::loopback_v4(), 8080)))
		return 1;

	srv.run(); // 블로킹; 핸들러 등에서 srv.stop()을 호출하면 반환됨
	return 0;
}
```

더 완전한 예제(정적 파일 + 라우트 그룹/파라미터 predicate가 있는 REST API + WebSocket 에코
엔드포인트)는 `examples/nhttpd/main.cpp`를, 더 많은 호출부 패턴은 [USAGE.md](USAGE.ko.md)를
참고하세요.

## 아키텍처 개요

```
src/platform/    epoll, 로우 소켓, ipv4/ipv6 엔드포인트
src/async/       task<T> (코루틴), io_context (리액터), io_context_pool
                 (SO_REUSEPORT 멀티쓰레딩), thread_pool (블로킹 작업 오프로드)
src/io/          비동기 스트림 인터페이스 + memory/file/range/socket 스트림
src/protocol/    header/method/status/mime/date/query-string/resource 파싱,
                 청크 전송 코덱, multipart/form-data 스트리밍 파서
src/server/      listener, HTTP/1.1 connection (상태 머신 enum 없이 코루틴 하나로 표현),
                 request/response, extension registry, vhost/vpath/overlay/single_file
src/router/      REST 라우터: 경로 트라이, 플루언트 등록 DSL, 미들웨어, 그룹핑
src/ws/          WebSocket 핸드셰이크 + 실제 RFC 6455 프레임 입출력
```

`listener`는 워커 쓰레드마다 하나의 `io_context`를 실행합니다; 각 워커는 리스닝 엔드포인트마다
자신만의 `SO_REUSEPORT` 소켓을 바인딩하므로, 이 라이브러리가 아니라 **커널이** 쓰레드 간 accept된
커넥션을 로드밸런싱합니다. 하나의 커넥션은 그것을 accept한 워커에 생애주기 내내 고정됩니다.

## 정적 파일 서빙

```cpp
srv.extends(overlay_of("/var/www", "index.html", srv.blocking_pool()));
```

조건부 GET (`ETag`/`If-None-Match`/`If-Modified-Since`)과 byte-`Range` 요청
(`206 Partial Content`)은 핸들러 코드 없이도 자동으로 동작합니다 — 디렉터리 서빙(`overlay`)과
단일 고정 파일 서빙(`single_file`) 둘 다 같은 엔진을 사용합니다.

> **참고:** `"/"`에 마운트된 `router`(또는 `vpath` 위에 만들어진 모든 확장)는 저렴한 `wants()`
> 검사에서 모든 요청을 받아들입니다. 따라서 먼저 거부권을 가져야 하는 것(정적 overlay 등)에는
> router보다 **더 낮은** 우선순위 번호를 줘야 합니다 — `overlay` 생성자의 우선순위 인자와
> `examples/nhttpd/main.cpp`의 실제 예제를 참고하세요. 이 내용은 `CLAUDE.md`의
> architecture-decisions 로그에 더 자세히 다뤄져 있습니다.

## 가상 호스팅

```cpp
auto example_com = vhost_for("www.example.com"); // 정확한 호스트명, 정규식, 또는 predicate
example_com->extends(overlay_of("./example.com", "index.html", srv.blocking_pool()));
srv.extends(example_com);
```

## 라우팅 (`router` 모듈)

```cpp
auto router = make_router();

router->get("whoami", target_by([](request&) { return make_response("I'm jay."); }));

router->group([](facade_ptr inner) {
	inner->get(":user/profile", target_by([](request& req) {
		return make_response(route_of(req).captures.at(":user") + " is ...");
	}));

	inner->post(":user/set", target_by([](request& req) -> async::task<response> {
		std::string body;
		co_await req.body->read_all(body);
		co_return make_response(std::move(body));
	}));

	inner->param(":user", [](const std::string& name) {
		return name == "jay" || name == "kay";
	});
})->prepend(std::make_shared<my_logging_middleware>());

srv.extends(router);
```

경로 세그먼트별 매칭 우선순위는 **static 자식 → 가장 깊게 매칭되는 파라미터 자식 → wildcard**
순서입니다 — 이 정확한 우선순위 규칙은 실제로 과거에 출시됐던 버그의 원인이었고, 전용 회귀
테스트(`tests/unit/test_router_route.cpp`)로 고정되어 있습니다.

## WebSocket

```cpp
srv.extends(websocket_endpoint_for("/ws", [](std::shared_ptr<ws::ws_connection> ws) -> async::task<void> {
	for (;;) {
		auto msg = co_await ws->receive();
		if (!msg) co_return;
		co_await ws->send_text(msg->data); // 에코
	}
}));
```

핸드셰이크뿐만 아니라 완전한 RFC 6455 프레임 입출력(마스킹, 조각화, ping/pong, close
핸드셰이크)까지 지원합니다.

## TLS/SSL

```cpp
srv.listen_tls(endpoint(ip_address::loopback_v4(), 8443), "cert.pem", "key.pem");
```

OpenSSL과 `NHTTP_ENABLE_TLS`(기본값 켜짐)가 필요합니다. `listen_tls`는 `listen`의 워커별
`SO_REUSEPORT` 바인딩을 그대로 반영하므로, TLS 엔드포인트도 평문 HTTP와 동일한 멀티쓰레드
부하 분산을 얻습니다. 내부적으로 `tls_stream`은 `socket_stream`과 동일한 `io::stream`
인터페이스를 구현합니다 — OpenSSL을 한 쌍의 메모리 내 BIO에 대해 구동하고 암호문을 커넥션
자신의 비동기 스트림과 주고받으므로, TLS 핸드셰이크와 그 이후의 모든 읽기/쓰기가 평범한
`co_await`가 되며 리액터 쓰레드를 절대 블로킹하지 않습니다. transport 레이어 위의 모든 것
(라우팅, 확장, `overlay`, WebSocket 등)은 코드 변경 없이 TLS 위에서도 동일하게 동작합니다 —
평문 HTTP와 HTTPS를 동시에 서빙하는 리스너의 실제 예제는 `examples/nhttpd/main.cpp`를
참고하세요.

## 설계 문서

* [CONCEPTS.md](CONCEPTS.ko.md) — 원래 구현으로부터 계승한 설계 철학과 불변조건들, 그리고 원래
  구현이 남겨뒀던 공백(multipart 파싱, WebSocket 프레임, TLS) — 이번 재작성에서 전부 해소됨.
* [USAGE.md](USAGE.ko.md) — 원래 구현의 실제 사용 패턴. "새 API가 여전히 같은 의도를 표현할 수
  있는가"를 판단하는 기준으로 사용됩니다.
* [docs/protocol-extensibility.md](docs/protocol-extensibility.ko.md) — 현재 설계를, 향후
  HTTP/2/QUIC 구현에 필요할 아키텍처적 이음매(seam)들과 대조해 검토한 문서.
* [CLAUDE.md](CLAUDE.ko.md) — 이 저장소에서 작업을 이어받는 누구나(사람이든 AI든)를 위한
  빌드/아키텍처/의사결정 기록.
