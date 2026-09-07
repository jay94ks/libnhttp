# libnhttp

**Language**: [English](ReadMe.md) | 한국어

<p align="center">
<img src="https://raw.githack.com/jay94ks/libnhttp/main/logo.png" />
</p>

멀티쓰레드 리액터(Linux는 epoll, Windows는 IOCP — 둘 다 네이티브로 지원됩니다) 위에 구축된,
C++20 코루틴 기반의 이벤트 드리븐 HTTP/1.1 **및 HTTP/2** 서버 라이브러리입니다.

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
* [리버스 프록시](#리버스-프록시)
* [HTTP/2](#http2)
* [Windows](#windows)
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

* Linux (epoll 기반 리액터) 또는 Windows (IOCP 기반 리액터)
* GCC ≥ 11, Clang ≥ 14, 또는 MSVC ≥ 19.29 (VS 2019 16.10) — C++20 코루틴
* CMake ≥ 3.20
* OpenSSL (TLS/SSL 지원용; 비활성화하려면 아래 `NHTTP_ENABLE_TLS` 참고) — Windows에서는 `openssl`
  CLI만으로는 부족하고 MSVC와 링크 가능한 OpenSSL 개발 패키지(예: vcpkg)가 필요합니다

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
MSVC에서는 가장 가까운 대응물이 쓰입니다(`/W4 /permissive-` — 모든 GCC/Clang 플래그에 정확히
대응하는 건 아닙니다).

Windows에서는, MSVC 툴체인이 `PATH`에 있는 네이티브 셸(예: `vcvars64.bat` 실행 후)에서 같은
세 명령어가 그대로 동작합니다:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

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
src/platform/    이식 가능한 address/socket/reactor/file_info API, posix/ (epoll) 또는
                 win32/ (IOCP)로 구현됨 — 한 빌드에 둘 다 들어가지 않음
src/async/       task<T> (코루틴), io_context (리액터), io_context_pool
                 (SO_REUSEPORT 멀티쓰레딩, 또는 SO_REUSEPORT가 없을 때의 명시적 라운드로빈
                 대체), thread_pool (블로킹 작업 오프로드)
src/io/          비동기 스트림 인터페이스 + memory/file/range/socket 스트림
src/protocol/    header/method/status/mime/date/query-string/resource 파싱,
                 청크 전송 코덱, multipart/form-data 스트리밍 파서
src/server/      listener, HTTP/1.1 connection과 HTTP/2 connection_h2 (각각 상태 머신 enum
                 없이 코루틴 하나로 표현), request/response, extension registry,
                 vhost/vpath/overlay/single_file/reverse_proxy
src/router/      REST 라우터: 경로 트라이, 플루언트 등록 DSL, 미들웨어, 그룹핑
src/ws/          WebSocket 핸드셰이크 + 실제 RFC 6455 프레임 입출력
src/http2/       HPACK (RFC 7541) + 프레임 코덱 (RFC 9113)
```

`listener`는 워커 쓰레드마다 하나의 `io_context`를 실행합니다. `SO_REUSEPORT`가 있는
플랫폼(Linux)에서는 각 워커가 리스닝 엔드포인트마다 자신만의 소켓을 바인딩하고 커널이 쓰레드
간 accept된 커넥션을 로드밸런싱합니다; 그게 없는 곳(Windows엔 대응물이 없음)에서는 워커 하나가
accept하고 새 커넥션을 다른 워커로 명시적으로 라운드로빈합니다. 어느 쪽이든, 하나의 커넥션은
그것을 소유하게 된 워커에 생애주기 내내 고정됩니다.

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

## 리버스 프록시

```cpp
using nhttp::server::upstream;

std::vector<upstream> upstreams{
	upstream(endpoint(ip_address::loopback_v4(), 9001)),
	upstream(endpoint(ip_address::loopback_v4(), 9002)),
};

srv.extends(reverse_proxy_for("/api", upstreams)); // 둘 사이의 평범한 라운드로빈
```

하나 이상의 업스트림을 URL 접두사 아래 마운트하고(`router`가 마운트되는 것과 같은 `vpath`
방식) HTTP/1.1 요청/응답을 그대로 릴레이합니다, WebSocket 업그레이드(업스트림이 `101`로
응답하면 raw 바이트 스플라이스로 패스스루됨)도 포함해서요. HTTPS 업스트림이라면 `upstream`
항목에 `use_tls = true`와 `tls_sni_hostname`을 설정하세요(`verify_tls_cert`는 기본적으로
켜져 있습니다). 프록시된 요청마다 새 업스트림 커넥션을 엽니다 — 아직 커넥션 풀링이나 능동
헬스체크는 없습니다(다운된 업스트림은 그 요청 하나만 `502`로 실패); 현재 알려진 단순화의
전체 목록은 `CLAUDE.md`의 Phase 13 로그를 참고하세요.

## HTTP/2

평문에서는 **prior knowledge**로 자동 협상됩니다 — 위에 이미 보여준 것 외에 코드 변경이
필요 없습니다; `listener`가 아무 평문 커넥션에서나 HTTP/2 클라이언트 연결 프리페이스를
감지하고 HTTP/1.1 대신 HTTP/2 드라이버로 라우팅합니다:

```bash
curl --http2-prior-knowledge http://127.0.0.1:8080/whoami
```

모든 확장, 라우터, `reverse_proxy`가 코드 변경 없이 HTTP/2 위에서도 동일하게 동작합니다 —
스트림의 요청은 HTTP/1.1이 쓰는 것과 정확히 같은 `listener::dispatch()` 경로로 디스패치됩니다.
**이번 라운드에는 TLS 위 ALPN으로 협상되는 HTTP/2는 구현되지 않았습니다**(몇 가지 다른 실제
범위 축소와 함께 보류됨 — 요청 바디는 디스패치 전에 완전히 버퍼링되고, 응답 헤더는 HEADERS
프레임 하나에 들어간다고 가정하며, 일부 SETTINGS는 강제되지 않습니다; 완전하고 정직한 목록은
`CLAUDE.md`의 Phase 14 로그를 참고하세요). QUIC/HTTP-3은 전혀 구현되지 않았고 이 프로젝트에서
계획되어 있지도 않습니다(이유는 `CLAUDE.md`의 아키텍처 결정 로그 참고).

## Windows

Windows는 (최선을 다한 정도가 아니라) 완전히 지원되고 네이티브로 테스트된 대상입니다 —
플랫폼 레이어가 전용 단계에서 강화되었기 때문입니다. IOCP 리액터를 만들면서 발견한 실제
버그들은 `CLAUDE.md`의 Phase 12 로그를 참고하세요(그중 몇 개는 libnhttp에만 국한되지 않는,
Windows 소켓 프로그래밍을 하는 누구에게나 진짜 유용한 "함정"입니다). Linux와의 실제 영구적인
동작 차이 하나: Windows에는 `SO_REUSEPORT` 대응물이 없어서, `listener`는 거기서 단일 accept
루프가 커넥션을 워커들 사이에 명시적으로 라운드로빈하는 방식으로 대체합니다(위 아키텍처
개요 참고) — 기능적으로는 동등하지만 커널이 균형을 맞춰주지는 않습니다.

## 벤치마크

nginx 1.24, Apache 2.4.58(event MPM)와의 정적 파일 처리량을 [wrk](https://github.com/wg/wrk)로
측정했습니다. 동일한 결정적(deterministic) 10&nbsp;KB HTML 파일을, 8 threads / 200 connections /
30초 조건으로 서빙했습니다. Apache의 기본 `MaxRequestWorkers`(150)는 측정 전에 800으로
올렸습니다 — 기본값은 실제 운영 배포가 쓸 법한 동시성보다 한참 낮아서, 올리지 않으면 의미 있는
수치 대신 소켓 오류만 발생했습니다. **nhttpd는 이런 튜닝이 전혀 필요 없습니다**: 아래 모든
수치는 라이브러리 기본값(`blocking_pool_size` = 4) 그대로입니다 — 작은 풀이 왜 이제는 큰
풀보다 더 빠른지는 [PLAN.md](PLAN.ko.md)의 P4를 참고하세요. 이전 라운드의 이 벤치마크 섹션이
줬던 조언과 정반대입니다.

**이 벤치마크를 돌리는 과정에서 이 라이브러리의 실제 버그 두 개를 발견해서 고쳤습니다** —
둘 다 지금 `main`에 이미 반영되어 있고, 아래 수치는 수정된 빌드 기준입니다:

* **`task<T>`가 `co_await`할 때마다 자기 자신의 코루틴 프레임을 누수시키고 있었습니다.**
  `operator co_await() &&`가 컴파일러에게 `awaiter`를 넘기면서 task 자신의 `handle_`을 null로
  비워버렸는데, 정작 그 완료된 콜리(callee) 코루틴 프레임을 파괴하는 건 아무도 하지 않았습니다
  (`final_suspend`는 그저 suspend한 뒤 continuation으로 넘길 뿐입니다 — 전체 메커니즘은
  `include/nhttp/async/task.hpp`의 주석 참고). `task<T>`는 이 코드베이스의 거의 모든 비동기
  함수의 반환 타입이기 때문에, 모든 중첩된 `co_await`마다 누수가 발생했습니다. 정적 파일을
  keep-alive로 지속 요청했을 때 RSS가 23만 3천 요청 동안 5.6&nbsp;MB에서 5.78&nbsp;GB까지
  치솟는 것으로 실제 관찰됐고(요청당 약 24&nbsp;KB), 서버와 완전히 분리한 최소 재현으로도
  확인했습니다(아무 일도 안 하는 `task<int>`를 200만 번 await하니 약 125&nbsp;MB 누수).
  **수정**: `operator co_await()`에서 더 이상 `handle_`을 비우지 않도록 했습니다. 흔한
  `co_await foo()` 형태에서 `foo()`는 prvalue 임시 객체이고, 일반적인 C++ 임시 객체 수명 규칙에
  따라 그 수명은 전체 표현식이 끝날 때까지(즉 `await_resume()` 이후까지) 이어집니다 — `handle_`을
  그대로 두면 이미 올바르게 `handle_.destroy()`를 호출하는 task의 소멸자가 바로 그 시점에 실행되어
  프레임을 해제합니다. cppcoro 스타일 task 타입들이 쓰는 것과 같은 패턴입니다.
* **연결을 중간에 끊는 클라이언트 하나가 서버 프로세스 전체를 죽였습니다.** 이미 리셋된
  소켓에 `write()`하면 `SIGPIPE`가 발생하는데, 기본 처리 방식은 프로세스 전체를 즉시
  종료시키는 것입니다 — core도 안 남고, 디버거나 새니타이저가 잡을 것도 없습니다. 어떤
  지속 부하 벤치마크든 이걸 확실히 유발합니다(최소한 벤치마크 종료 시 클라이언트가 커넥션
  풀을 정리할 때). **수정**: POSIX에서 `listener`의 생성자에 `std::signal(SIGPIPE, SIG_IGN)`을
  추가했습니다(Windows는 소켓 쓰기에 대해 `SIGPIPE`가 없고 `WSAECONNRESET`/
  `WSAECONNABORTED`를 에러로 반환합니다). 그 결과로 나오는 `write()`의 `EPIPE`는 이미 기존
  소켓 에러 처리 경로에서 정상적으로 "연결 종료"로 처리되고 있었습니다.

### 루프백(같은 커널) 측정, 현재 `main` 기준

| 서버 | Req/s | 평균 지연시간 | p50 | p99 |
|---|---:|---:|---:|---:|
| nginx 1.24 | 124,984 | 3.13 ms | 1.04 ms | 27.14 ms |
| Apache 2.4.58 (event MPM, 튜닝됨) | 34,696 | 11.13 ms | 6.00 ms | 95.80 ms |
| **nhttpd (이 저장소, 튜닝 없음/기본값)** | **57,964** | **4.11 ms** | **2.89 ms** | **24.45 ms** |

nhttpd는 이 벤치마크 섹션이 처음 기록했던 13.8K req/s에서 이 동일한 벤치마크 기준 58K
req/s로 — 약 4.2배 개선되어, 이제 Apache를 확실히 앞서고 nginx의 대략 절반 수준까지
왔습니다(예전엔 10분의 1 수준이었습니다). [PLAN.md](PLAN.ko.md)에 여기까지 오는 동안 시도한
모든 것 — 이론만으로 판단하지 않고 구현·측정 후 반려한 두 개의 막다른 길을 포함해서 —
정직하게 전부 기록되어 있습니다:

* **P1 — 정적 파일용 `sendfile(2)` 고속 경로.** 가장 큰 한 방: 전체 파일 GET이 이제 파일
  디스크립터에서 소켓으로 시스템콜 한 번에 바로 가서, 블로킹 스레드 풀과 일반 read/write
  루프가 필요로 했던 모든 유저스페이스 버퍼 복사를 건너뜁니다.
* **P2 — 불필요한 스레드 풀 왕복 제거.** `overlay`/`single_file`이 같은 경로를 두 번(`wants()`
  한 번, `handle()` 한 번) stat하지 않게 됐고, `file_stream::open()`은 `fopen`과 크기 탐지를
  두 번이 아니라 한 번의 스레드 풀 왕복으로 합쳤습니다.
* **P3 — 커스텀 코루틴 프레임 할당자: 시도, 측정, 반려.** `task<T>` 프레임을 위한 스레드
  로컬 크기별 free-list 할당자를 구현했지만, A/B 벤치마크에서 측정 가능한 개선이 없었습니다
  (glibc의 `tcache`가 이미 스레드 로컬·크기별로 이 역할을 하고 있음).
* **P4 — `thread_pool`의 작업 큐: 세 번의 반복, 마지막 버전을 채택.** ① `std::counting_
  semaphore` 기반 락프리 큐는 지속 부하에서 오히려 약 2배 느렸습니다 — 이미 바쁜 워커조차
  매 dequeue마다 동기화 비용을 치러야 했기 때문인데, 기존 뮤텍스+큐는 "바쁜" 상태에서 이걸
  건너뛸 수 있었습니다. ② 락프리 큐 + 유휴 워커를 깨우기 위한 뮤텍스/condvar 조합으로
  이 문제는 해결했지만, `blocking_pool_size`가 과다 프로비저닝되지 않을 때만 그랬습니다:
  큰 풀 크기(64 — 이 벤치마크 섹션이 예전에 권장했던 값)에서는 8개 CPU 코어 위에서 64개
  OS 스레드가 경합하면서 락프리 큐가 절약한 것보다 컨텍스트 스위칭 오버헤드로 잃는 게
  훨씬 컸고, 작은 풀 크기에서는 같은 설계가 68K+ req/s로 이번 라운드 전체 최고 기록을
  냈습니다. ③ 깨우기 알림 주변의 "누가 대기 중인가" 원자적 게이트를 제거해서 더
  단순화했습니다: glibc의 `condition_variable::notify_one()`은 대기자가 없으면 이미 내부적으로
  깨우기 시스템콜을 건너뛰므로, 그 게이트는 라이브러리가 이미 하고 있는 시스템콜 생략을 위해
  매 작업마다 원자적 읽기와 분기를 하나 더 추가하는 것에 불과했습니다 — 측정 결과 동등했고,
  더 단순한 코드를 택했습니다. **결과적으로 `blocking_pool_size`는 이제 수동 튜닝이 전혀
  필요 없습니다 — 라이브러리 자체의 작은 기본값이 가장 빠른 설정입니다**, 이 섹션이 예전에
  권장하던 것과 정반대입니다.
* **P5 — 윈도우 슬라이딩 `mmap` 읽기 경로.** `io::file_stream`이 버퍼링된 `fread()` 대신
  파일을 메모리 매핑할 수 있게 됐는데, 큰 파일 전체를 매핑하는 대신 제한된 크기
  (4&nbsp;MiB) 윈도우를 파일 위에서 슬라이딩합니다 — `platform::file_mapping` 참고. 스트림의
  실제 첫 `read()` 호출 시점에 **지연 생성**되도록 했는데, 정확히는 전체 파일 GET(P1의
  sendfile 경로)이 `read()`를 아예 호출하지 않기 때문입니다; 처음에는 매번 미리 매핑을
  시도하는 방식으로 구현했다가, 절대 쓰이지 않을 매핑 비용을 흔한 경우(sendfile 경로)마다
  지불하는 바람에 측정 가능한 회귀가 발생해서 지연 방식으로 바꿨습니다. 오늘 기준으로는
  바이트 `Range`/TLS/청크드 응답 — sendfile을 탈 수 없는 것들 — 만 이 경로를 탑니다.

### Docker 네트워크 스택 벤치마크

같은 호스트·같은 커널에서의 루프백 벤치마크는 실제 환경의 오버헤드를 과소평가합니다: 리눅스의
루프백 인터페이스는 실제 소켓-투-NIC 경로의 상당 부분(실제 이더넷 프레이밍, 드라이버 큐잉,
많은 경우 체크섬 계산까지)을 건너뜁니다. `benchmark/docker/`는 같은 세 서버를 하나의 Docker
브리지 네트워크 위 별도 컨테이너로 올리고, 네 번째 클라이언트 컨테이너가 각 서버를 컨테이너
DNS 이름으로 `wrk`로 때립니다 — 모든 요청이 실제 veth 페어와 리눅스 브리지를 거치며, 실제 NIC
배포가 거치는 것과 동일한 커널 코드 경로를 탑니다. 세 서버 컨테이너 모두 동일한 `cpus`/
`mem_limit` 리소스 제한을 받아 어느 쪽도 유리하지 않습니다.

재현 방법:

```bash
cd benchmark/docker
docker compose build
docker compose up -d bench-nginx bench-apache bench-nhttp
docker compose run --rm bench-client
```

결과(서버 컨테이너당 4 CPU / 1&nbsp;GiB, 그 외 파라미터는 위 루프백 측정과 동일), 현재
`main` 기준, nhttpd는 여전히 튜닝 없는 기본값:

| 서버 | Req/s | 평균 지연시간 | p50 | p99 |
|---|---:|---:|---:|---:|
| nginx 1.24 | 55,171 | 5.31 ms | 2.71 ms | 34.20 ms |
| Apache 2.4.58 (event MPM, 튜닝됨) | 21,558 | 19.36 ms | 9.65 ms | 135.37 ms |
| **nhttpd (이 저장소, 튜닝 없음/기본값)** | **35,869** | **6.53 ms** | **4.62 ms** | **34.00 ms** |

루프백 결과와 같은 이야기입니다: nhttpd(11,170 → 35,869 req/s, 약 3.2배 개선)는 여기서도
Apache를 확실히 앞서고, nginx의 6분의 1이 아니라 대략 3분의 2 수준까지 왔습니다. 세 컨테이너
모두 전체 실행 동안 죽지 않고 메모리도 안정적이었습니다 — nhttpd는 108만 요청 처리 후에도
**스레드 13개에 RSS 4.96&nbsp;MiB**만 사용했는데, 이는 nginx 자신(프로세스/스레드 9개,
16.96&nbsp;MiB)보다도 낮고 Apache(프로세스 199개, 26.93&nbsp;MiB)보다는 훨씬 낮은
수치입니다 — 코루틴 누수 수정이 루프백뿐 아니라 실제 컨테이너 네트워크 트래픽 아래서도
유효함을, 그리고 P1~P4가 도달한 작은 기본 스레드/워커 수가 단순한 처리량 숫자가 아니라
실제 자원 효율임을 확인했습니다.

## 설계 문서

* [CONCEPTS.md](CONCEPTS.ko.md) — 원래 구현으로부터 계승한 설계 철학과 불변조건들, 그리고 원래
  구현이 남겨뒀던 공백(multipart 파싱, WebSocket 프레임, TLS) — 이번 재작성에서 전부 해소됨.
* [USAGE.md](USAGE.ko.md) — 원래 구현의 실제 사용 패턴. "새 API가 여전히 같은 의도를 표현할 수
  있는가"를 판단하는 기준으로 사용됩니다.
* [docs/protocol-extensibility.md](docs/protocol-extensibility.ko.md) — 설계를 HTTP/2가
  필요로 했던 아키텍처적 이음매(seam)들과 대조해 검토한 문서(셋 다 변경 없이 유지됨), 그리고
  QUIC이 여전히 필요로 할 이음매들.
* [PLAN.md](PLAN.ko.md) — 위 벤치마크 섹션에서 드러난 격차를 줄이기 위한 성능 개선 계획,
  예상 효과 순으로 우선순위가 매겨져 있습니다.
* [CLAUDE.md](CLAUDE.ko.md) — 이 저장소에서 작업을 이어받는 누구나(사람이든 AI든)를 위한
  빌드/아키텍처/의사결정 기록.
