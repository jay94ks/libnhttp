# CLAUDE.md

**Language**: [English](CLAUDE.md) | 한국어

이 저장소에서 작업하는 모든 Claude Code 세션을 위한 참고 문서입니다. 빌드 파일, 아키텍처,
레거시 트리를 건드리기 전에 먼저 읽으세요.

## 이 저장소는 무엇인가

`libnhttp`는 C++20 기반의 코루틴, 멀티쓰레드, 이벤트 드리븐 HTTP 서버 라이브러리로 **완전히
처음부터 재작성**되고 있습니다. 이건 `libnhttp/`, `libnhttp-tests/`, `nhttpd/` 아래에 있던
기존 C++17 코드의 리팩터링이나 점진적 현대화가 아닙니다 — 그 트리는 *이전* 구현이었고, 새
구현이 기능적으로 동등해질 때까지는 참고용으로만 디스크에 그대로 남겨졌으며, 그 시점에
(사용자의 명시적 확인을 받아 — 절대 묻지 않고 삭제하지 않음) 삭제되었습니다.

기존 구현으로부터 무엇이, 왜 계승되는지는 두 문서에 기록되어 있습니다:

- [`CONCEPTS.md`](CONCEPTS.ko.md) — 보존할 가치가 있는 설계 철학과 불변조건들(이벤트
  드리븐/스트리밍 모델, 두 단계 확장 계약, 라우터 트라이의 우선순위 시맨틱, 태그 기반
  커넥션/요청 단위 메타데이터 등), 그리고 기존 구현이 갖고 있던 공백들(multipart/form-data가
  구현된 적이 없었음, WebSocket은 핸드셰이크만 있었음, TLS 없음)과 반복하지 말아야 할
  네이밍/빌드 매크로 실수들의 명시적인 목록.
- [`USAGE.md`](USAGE.ko.md) — 기존 구현의 관찰 가능한 API 사용 패턴들(부트스트래핑, 라우팅,
  정적 파일 서빙, vhost, 수동 curl 기반 스모크 테스트 매트릭스) — 새로운 API 설계가 시그니처나
  이름이 다르더라도 여전히 표현할 수 있어야 하는 것들.

**기존 트리에서 코드가 이식된 것은 없습니다.** 저 두 문서에 담긴 기능 집합과 철학만이
계승됩니다. "기존 코드는 X를 어떻게 했었지"가 궁금하다면 먼저 `CONCEPTS.md`/`USAGE.md`를
읽으세요 — 그것들이 정리된 요약본입니다; 저 두 문서가 다루지 않는 무언가에 대해서만
`libnhttp/nhttp/**`를 직접 파고드세요.
(참고: 이 레거시 트리는 사용자 확인을 거쳐 이미 저장소에서 삭제되었습니다 — 아래 Phase 10
항목 참고. 위 인용문은 그 결정이 내려지기 전까지의 작업 원칙을 그대로 남겨둔 것입니다.)

전체 재설계 계획(아키텍처 + 단계별 빌드 순서)은 이걸 계획했던 머신의
`C:\Users\jay94\.claude\plans\dazzling-fluttering-badger.md`에 있습니다 — 새로 클론한
환경에는 없을 것이므로, 아래 요약이 영구적인 기록입니다.

## 아키텍처 결정 사항 (사용자와 확정됨 — 조용히 재논의하지 말 것)

1. **C++20**, 17이 아님. 코루틴이 비동기 I/O 모델 전체에 쓰입니다.
2. **CMake** 빌드 시스템, 기존 `Makefile`/`.vcxproj`/`.sln`을 대체함. **Linux 우선**:
   Windows 지원은 의도적으로 미뤄졌지만, 플랫폼 경계(`src/platform/`)는 나중에 그 위의
   아무것도 재설계하지 않고도 Windows를 추가할 수 있도록 깔끔한 이음매로 유지되어야 합니다.
3. **동시성**: 코루틴 `task<T>` 타입 + `io_context`(epoll 인스턴스 하나 + ready 큐 + 타이머
   힙) + 각각 자신의 OS 스레드에 고정된 N개의 그런 컨텍스트로 이루어진 `io_context_pool`.
   새 커넥션은 수동 work-stealing이 아니라 **`SO_REUSEPORT`**를 통해 워커 스레드 사이에
   로드밸런싱됩니다(각 워커가 같은 주소:포트에 자신만의 리스닝 소켓을 바인딩하고; 커널이
   accept를 분산시킵니다). 커넥션의 코루틴은 그것을 accept한 스레드에 생애주기 내내
   고정됩니다 — 커넥션 로컬 상태에 대해 스레드 간 동기화가 필요 없습니다. 별도의
   `thread_pool`은 epoll이 다룰 수 없는, 진짜로 블로킹하는 작업(파일시스템 stat/read)만을
   위해 존재합니다. **리액터 스레드는 절대 블로킹하면 안 됩니다** — 워커를 기다리며
   뮤텍스/condvar에서 블로킹하는 것 — 이것이 기존 구현의 동시성 계약에서 온, 협상 불가능한
   단 하나의 불변조건입니다(`CONCEPTS.md` §2 참고).
4. **테스트**: Catch2(v3, CMake `FetchContent`를 통해), 기존의 출력만 하던 `test_case` 클래스를
   대체함. 모듈별 유닛 테스트에 더해 루프백(`127.0.0.1`과 `::1` 양쪽 — 듀얼스택은 선택이 아니라
   최우선 요구 사항)에서의 실제 소켓 통합 테스트.
5. **라우터**(기존 `xfwk` 네임스페이스/코드네임의 후계자 — 이름 변경, `nhttp::router` 제안):
   트라이 매칭 우선순위 규칙을 **정확히** 이식해야 합니다 — static 자식 → 가장 깊게 매칭되는
   parameter 자식 → wildcard — 기존 구현의 실제 버그(커밋 `61a8fa1`, `CONCEPTS.md` §5 참고)가
   바로 이 우선순위 결정을 잘못해서 생겼기 때문입니다. 라우터의 매칭 로직에 대한 어떤 변경도,
   올바른 것으로 간주되기 전에 이 정확한 우선순위 순서를 단언하는 회귀 테스트로 검증되어야
   합니다.
6. **Multipart/form-data 파싱과 WebSocket 프레임 송수신이 이번엔 진짜로 구현됩니다** — 기존
   구현은 이것들을 스텁으로만 남겨뒀습니다(form-data는 파서가 아예 없었음; WebSocket은 HTTP
   Upgrade 핸드셰이크만 완성했지 실제 프레임 입출력은 아니었음).
7. ~~TLS/SSL은 이번 라운드의 범위 밖~~ — **대체됨**: TLS/SSL은 Phase 11에서 구현되었습니다
   (아래 진행 로그 참고), 바로 이 결정이 유지하라고 요구했던 stream/transport 추상화 위에
   그대로 얹혀서요. 그 추상화는 변경할 필요가 없었습니다.
8. **HTTP/2와 QUIC은 구현되지 않지만**, 나중에 재설계 없이 추가할 수 있도록 세 가지
   아키텍처적 이음매가 보존되어야 합니다: (a) connection/exchange 분리 — 라우터와 핸들러
   코드는 커넥션당 요청 하나를 절대 가정하면 안 됨; (b) transport에 무관한 비동기 스트림
   추상화 — 드라이버는 오직 이걸 통해서만 네트워크와 대화해야 하고 raw TCP 소켓을 절대
   가정하면 안 됨; (c) 헤더는 디코딩된 키/값 쌍으로 라우터/확장에 도달해야 하며 raw 와이어
   바이트로는 절대 안 됨 — 그래야 HPACK/QPACK으로 디코딩된 헤더가 그 레이어에서 HTTP/1.1
   헤더와 구분되지 않습니다.
9. **경고 없는 빌드**(`-Wall -Wextra -Wpedantic`, 에러로 처리)는 지향점이 아니라 반드시
   지켜야 하는 요구 사항입니다 — 컴파일하려면 경고를 억제해야 하는 코드를 추가하지 마세요.

## 디렉터리 레이아웃

```
CMakeLists.txt              최상위 빌드 설정
cmake/                      CMake 헬퍼 모듈 (컴파일러 경고 등)
include/nhttp/              공개 헤더 (src/ 모듈 레이아웃을 그대로 반영)
src/
  platform/                 epoll 래퍼, 로우 소켓 래퍼, ipv4/ipv6 엔드포인트 타입
  async/                    task<T>, io_context, io_context_pool, 소켓 awaitable, 타이머,
                             thread_pool (블로킹 작업 오프로드)
  io/                       비동기 스트림 인터페이스 + memory/file/range/socket 스트림
  protocol/                 header/method/status/mime/date/query_string/resource/urlencode,
                             청크 코덱, multipart/form-data 파서
  server/                   listener, HTTP/1.1 connection 코루틴, request/response 파사드,
                             태그 저장소, params/설정
  server/extensions/        extension registry, vhost, vpath, 정적 overlay, 단일 파일 서빙
  router/                   트라이 기반 라우터 (facade/route/middleware/target), xfwk의 후계자
  ws/                       WebSocket 핸드셰이크 + RFC6455 프레임 코덱 + 비동기 send/recv
  tls/                      OpenSSL 기반 TLS/SSL (tls_context, tls_stream) — 메모리 BIO
                             패턴, io::stream을 구현하므로 그대로 꽂히는 transport (Phase 11)
  depends/                  벤더링된 서드파티 (sha1, utf8) — 표준 시설로 간단히 대체 가능하지
                             않은 이상 그대로 재사용
tests/
  unit/                     모듈당 파일 하나, Catch2
  integration/              루프백 위의 실제 리스너, 작은 테스트 HTTP/WS 클라이언트로 구동됨
examples/
  nhttpd/                   기존 데모 엔드포인트를 반영하는 샘플 앱 (마지막 단계에서 작성됨)
libnhttp/, libnhttp-tests/, nhttpd/, coverage/, Makefile, *.vcxproj, *.sln
                             레거시 — 기존 C++17 구현, 손대지 않고 참고용으로만 유지,
                             새 구현이 동등해지고 사용자가 확인한 뒤에만 삭제됨
```

(위 레거시 항목 목록은 실제로 삭제되기 전 계획 당시의 원칙을 그대로 남겨둔 것입니다 — 실제
삭제는 Phase 10 항목 참고.)

## 빌드 & 테스트 (Linux — 호스트가 Windows일 때는 WSL Ubuntu를 통해 개발함)

이 머신의 WSL Ubuntu 인스턴스에는 GCC 13.3.0, CMake 3.28, Ninja가 미리 설치되어 있으며,
리액터가 epoll 기반이라 네이티브 Windows에서는 빌드하거나 실행할 수 없으므로 이 프로젝트를
실제로 컴파일/실행/테스트하는 데 쓰이는 환경입니다. Windows 셸에서 호출한다면 명령어 앞에
`wsl.exe -d Ubuntu -- bash -lc "..."`를 붙이세요; `C:\GitHub\libnhttp` 아래 경로들은 WSL
안에서 `/mnt/c/GitHub/libnhttp`로 접근 가능합니다.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

작업의 모든 단계는 다음 단계로 넘어가기 전에 위 세 명령어가 컴파일러 경고 0개로 성공하는
상태로 트리를 남겨둬야 합니다.

## 코딩 관례

- 로우 `new`/`delete` 금지; 소유권은 `std::unique_ptr`/`std::shared_ptr` 또는 스코프드
  RAII 타입으로 표현합니다.
- 기존 구현이 C++17에 비소유(non-owning) 뷰가 없어서 순전히 그 때문에 복사했던 곳에서는
  복사 대신 `std::string_view`/`std::span`을 선호합니다.
- 표준 라이브러리 프리미티브(`std::mutex`, `std::condition_variable`, `std::jthread`,
  `std::atomic`)를 래퍼 타입으로 재발명하지 말고 직접 사용합니다 — 단, 표준 시설이 맞지 않는
  구체적이고 문서화된 이유가 있다면 예외입니다(그런 경우를 만나면 여기에 기록하세요).
- 코루틴을 반환하는 함수(`task<T>`)는 자신을 호출한 스레드를 절대 블로킹하지 않습니다; 진짜
  OS 스레드를 블로킹해야 하는 무언가(파일시스템 I/O)가 있다면, 리액터 스레드 코루틴 안에서
  인라인으로 하지 말고 `thread_pool`을 거쳐야 합니다.
- `include/nhttp/` 아래의 공개 헤더는 합리적으로 가능한 한 구현 세부사항이 새어나가지 않게
  유지해야 하며(pimpl이나 깔끔한 인터페이스 타입), `src/` 아래의 소스 레이아웃을 그대로
  반영해야 합니다.

## 진행 기록

- **Phase 0 (스캐폴딩) — 완료.** CMake 골격, `cmake/CompilerWarnings.cmake`, FetchContent를
  통한 Catch2, `nhttp` 라이브러리 타겟, CTest에 연결된 `nhttp_unit_tests` 타겟.
- **Phase 1 (플랫폼 + 비동기 코어) — 완료.** `src/platform/`(주소, 소켓, epoll 래퍼)와
  `src/async/`(`task<T>`, `detached_task`, `sync_wait`, `io_context`, `io_context_pool`,
  `thread_pool`, `async_socket`)가 구현되었고 유닛/통합 테스트로 커버됩니다(실제 epoll
  리액터를 통한 루프백 accept/connect/read/write, 타이머, 스레드 간 `post()`, 스레드풀
  오프로드, 멀티 컨텍스트 풀). 14개 테스트 전부 경고 없이 통과.
  - **ThreadSanitizer로 검증함**(아래 참고) — `sync_wait_event::set()`에서 실제 레이스 하나를
    발견해 수정함(락 해제 후 notify하는 방식이 대기자가 notifier가 아직
    `pthread_cond_broadcast` 안에 있는 동안 condvar를 파괴하게 만들었음; 락을 쥔 채로
    notify하도록 수정, `include/nhttp/async/sync_wait.hpp`).
  - **살아있는 `io_context`를 건드리는 향후 비동기 코드를 위한 설계 메모**: `io_registration`의
    필드들과 `io_context`의 타이머 힙은 동기화되어 있지 *않습니다* — 그 컨텍스트의 `run()`
    루프를 실행하는 단일 스레드에서만 건드리는 게 안전합니다. 안전한 패턴(`tests/unit/
    test_io_context.cpp`와 `test_io_context_pool.cpp` 전체에서 쓰임)은 그 컨텍스트의 리액터
    스레드가 존재하기 *전에* 코루틴 체인을 스폰(첫 번째 중단 지점에 도달)하는 것입니다 —
    그러면 첫 등록이 단일 스레드에서 이루어지고; 그 이후의 모든 재개는 `run()` 자신의
    디스패치 안에서, 즉 이미 올바른 스레드에서 일어납니다. 리액터가 실행 중일 때 외부
    스레드에서 건드려도 안전한 건 오직 `post()`/`stop()`(그리고 평범한 `std::atomic`)뿐입니다.
    이는 위의 아키텍처 결정에 나온 "커넥션은 자신을 accept한 스레드에 고정된다"는 불변조건과
    정확히 같은 것입니다 — 단순한 성능 선택이 아니라, `io_registration`/타이머 상태에 락이
    없다는 점을 감안하면 정확성을 위해 반드시 필요합니다.
  - **이 머신의 WSL2 Ubuntu에서 ThreadSanitizer를 돌리려면 바이너리 앞에
    `setarch $(uname -m) -R`가 필요합니다**(예: `setarch $(uname -m) -R
    ./build-tsan/tests/nhttp_unit_tests`) — 그냥 실행하면 WSL2의 기본 ASLR 레이아웃 때문에
    즉시 `FATAL: ThreadSanitizer: unexpected memory mapping`으로 실패합니다; 이건 CTest의
    `catch_discover_tests` 빌드 후 단계(바이너리를 직접 호출함)도 깨뜨리므로, 별도의
    `-fsanitize=thread` CMake 빌드 디렉터리를 만들어서 `ctest`가 아니라 `setarch`로 그
    테스트 바이너리를 직접 실행하세요.
- **Phase 2 (비동기 스트림) — 완료.** `src/io/`: `stream`(비동기 read/write/seek/close + 공유
  `read_all`), `memory_stream`, `range_stream`(바이트 윈도우 데코레이터, 무제한 통과 폴백),
  `socket_stream`(`async_socket`을 감쌈), `file_stream`(`thread_pool`을 통해 오프로드, "리액터
  스레드는 절대 블로킹하지 않는다" 불변조건에 따름). 24개 테스트 전부 경고 없이 통과.
  - **Catch2 CMake 디스커버리 함정**: `TEST_CASE` 이름에 리터럴 `[`/`]` 문자가 들어가면(예:
    산문으로 `[begin, end)` 범위를 설명하는 경우) `catch_discover_tests`의 빌드 후 파서가 그
    테스트를 이어지는 여러 테스트와 합쳐서 하나의 가짜 CTest 항목으로 만들어버릴 수 있습니다
    — 테스트 바이너리 자체는 멀쩡합니다(`--list-tests`는 올바르게 분리된 이름들을 보여줌);
    디스커버리 스크립트가 대괄호에 걸려 넘어지는 것입니다. 테스트 **이름**에는 대괄호를
    피하세요(두 번째 인자인 태그, `"[io][range_stream]"`,는 괜찮습니다 — 산문 이름만이
    위험합니다).
- **Phase 3 (HTTP/1.1 프로토콜 파싱 + multipart/form-data) — 완료.** `src/protocol/`:
  `http_method`(CONCEPTS.md에 따라 시맨틱 플래그를 데이터로 표현), `http_header`/
  `http_headers`(점진적 라인 파서, 대소문자 구분 없는 조회), `http_status`(reason-phrase
  테이블), `urlencode`, `http_query_string`, `http_date`(RFC 1123, `timegm`/`gmtime_r`을
  통해 — 원래의 localtime-delta 트릭보다 단순함), `http_mime_type`(+ 확장자 테이블 + MIME
  파라미터와 multipart Content-Disposition 양쪽에서 쓰이는 공유 `parse_header_parameters`),
  `http_resource`(요청 라인 파서), `http_chunked`(`chunked_decoder_stream` + 청크 헤더
  포맷팅), 그리고 `http_multipart`(`multipart_reader`/`multipart_part`) — 원래 구현이
  실제로는 가진 적 없던(CONCEPTS.md §6 참고), 스트리밍 방식이고 메모리가 유계인
  multipart/form-data 파서. 63개 테스트 전부 경고 없이 통과.
  - **이 단계에서 발견된 익명 네임스페이스 friend 버그로부터 이어지는 함정**:
    `src/protocol/http_multipart.cpp`에서 `multipart_part_stream`은 익명 네임스페이스 안에
    두면 안 됩니다. `multipart_reader`의 `friend class multipart_part_stream;`이 그 클래스를
    구체적으로 감싸는 `nhttp::protocol` 네임스페이스에서 지목하기 때문입니다 — 같은 이름의
    익명 네임스페이스 클래스는 별개의 타입이고 friend 관계가 조용히 적용되지 않습니다
    (friend 전용 멤버가 실제로 호출되기 전까지는 멀쩡하게 컴파일됨).
  - **테스트 작성 시 함정**: `using namespace nhttp;` 없이 `using namespace nhttp::io;`
    등을 하는 `.cpp` 안에서 `io::stream`이라고 쓰면 동작하지 **않습니다** — `using
    namespace`는 네임스페이스의 *멤버들*만 한정자 없이 끌어올 뿐, 네임스페이스 자신의 이름을
    쓸 수 있는 별칭을 만들어주지 않습니다. 그냥 한정자 없는 이름(`stream`)을 쓰세요,
    `io::stream`이 아니라.
- **Phase 4 (서버 코어) — 완료.** `src/server/`: `params`(확장된 멀티쓰레드 설정),
  `tag_storage`(타입 소거된 타입별 슬롯 — CONCEPTS.md §1의 태그 메커니즘, 커넥션 단위
  (`connection::tags()`)와 요청 단위(`request::tags`) 양쪽에 쓰임), `request`/`response`
  파사드 + `make_response(...)` 자유 함수, `connection`(HTTP/1.1 read-dispatch-write 루프를
  코루틴 하나로 표현 — 상태 머신 enum 없음, CONCEPTS.md §2 참고), `listener`(`io_context_pool`을
  소유하고, 엔드포인트마다 워커당 하나의 `SO_REUSEPORT` 소켓을 바인딩하며, `run()`/`stop()`이
  호출한 스레드를 블로킹/해제함). 지금은 설정 가능한 단일 `handler_type`
  (`std::function<task<response>(request&)>`)이 최종 디스패치 대상입니다 — Phase 5에서 이걸
  완전한 우선순위 정렬 extension registry로 대체/감쌉니다. 70개 테스트 전부 경고 없이
  통과했고, 여기에는 `tests/integration/test_server_basic.cpp`도 포함됩니다 — 직접 만든
  작은 블로킹 HTTP/1.1 클라이언트(nhttp 자신의 protocol/ 코드와 의도적으로 무관하게 만들어서,
  공유된 버그가 실패를 숨기지 않도록 함)로 end-to-end 구동되는 실제 `listener`: 평범한 GET,
  Content-Length가 있는 POST, 청크 요청 디코딩, 청크 응답 인코딩(알 수 없는 본문 길이), 404,
  하나의 커넥션에서 두 요청에 걸친 keep-alive, IPv6 루프백.
  - **이 통합 테스트를 통해 발견되어 `range_stream`에서 수정된 실제 버그**: 그 생성자는
    *내부* 스트림의 길이를 알 수 없을 때(`get_length() < 0`) 호출자가 진짜 `[begin, end)`를
    넘겼는지 여부와 무관하게 무제한 통과로 폴백하곤 했습니다. 이건 원래의 "알려진 범위가
    없으니 그냥 전부 통과시킨다" 케이스에는 맞지만, 자신의 길이를 절대 알 수 없는 살아있고
    탐색 불가능한 커넥션 스트림 위에 정확한 바이트 수 제한(예: HTTP Content-Length 본문)을
    걸기 위해 `range_stream`을 쓰는 경우엔 틀렸습니다 — 이 폴백이 호출자가 요청한 경계를
    조용히 무시하고 영원히 스트리밍하는 바람에, 본문이 있는 첫 POST 통합 테스트가 멈춰버렸습니다.
    수정: 이제 `bounded_`는 순전히 `begin`/`end`가 음수가 아닌지(호출자의 실제 의도)로
    결정됩니다; 내부 길이는 알려져 있을 때 그 경계를 *제한(clamp)*할 뿐, 더는 그것을
    덮어쓰지 않습니다. 수정 내용과 이유를 담은 주석은 `src/io/range_stream.cpp` 참고.
  - **아직 다루지 않은, 알려진 Phase 4의 단순화 지점들**: 요청 단위 타임아웃 강제가 없음
    (`params::header_timeout`/`idle_timeout`은 정의되어 있지만 아무것도 연결되어 있지 않음
    — 아직 존재하지 않는 "타이머와 read를 경합시키는" 컴비네이터가 필요함); `Connection`/
    `Transfer-Encoding` 헤더 값 매칭은 완전한 토큰 목록 파싱이 아니라 단순한 대소문자 구분
    없는 부분 문자열 검사임(실무에서 `close`/`keep-alive`/`chunked`에는 문제없지만, 다중 값을
    가진 병리적 헤더에는 스펙에 엄격하지 않음).
- **Phase 5 (확장) — 완료.** `src/server/extension.hpp`/`.cpp`: 두 단계 `wants`/`handle`
  계약 + 우선순위 정렬 `extension_registry`, 이제 `listener`에 연결됨(`listener::extends(...)`;
  레지스트리가 단일 폴백 `handler_type`보다 먼저 시도됨). `src/server/extensions/`: `vpath`
  (URL 프리픽스 스코핑 프리미티브 — `subpath_of(req)`는 중첩 디스패치를 감싸며 push/pop되는
  요청 단위 `vpath_tag` 스택을 읽음; Phase 6의 라우터는 별도의 것이 아니라 이 정확히 같은
  프리미티브 위에 구축됨, CONCEPTS.md §4-5 참고), `vhost`(호스트명/정규식/predicate로
  매칭되는 스코핑, vpath와 같은 모양이지만 `request::hostname`을 키로 씀),
  `static_content`(**공유되는** 조건부 GET + byte-Range 엔진 — `serve_stream_conditionally`,
  `make_etag`, `qualify_relative_path` — `overlay`(디렉터리 서빙, index 파일로 폴백)와
  `single_file`(매칭되는 모든 경로에서 고정 파일 하나) 양쪽에서 동일하게 쓰여서, 이 로직이
  둘 사이에 절대 중복되지 않음). 75개 테스트 전부 경고 없이 통과했고, 여기엔 다음의 실제
  end-to-end 커버리지가 포함됩니다: index 파일 폴백, 이름 있는 파일 서빙, 404, **경로 순회
  거부**(`qualify_relative_path`가 `/../../etc/...`를 올바르게 차단하고 403을 반환함),
  ETag/If-None-Match → 304, byte-Range → 올바른 `Content-Range`와 함께 206, `Host` 헤더
  기준 vhost 디스패치, vpath로 스코핑된 중첩 overlay 마운팅.
  - `qualify_relative_path`(`static_content.hpp` 안)는 지금까지 유일하게 공유되는 경로
    정규화기입니다; Phase 6의 라우터도 자신의 경로 처리를 위해 같은 "`.`/`..` 해석, 탈출
    거부" 로직을 원할 것입니다 — CONCEPTS.md의 중앙화된 경로 정규화 원칙에 따라 이걸 다시
    만들지 말고 재사용하세요, 라우터가 다른 형태(한 번에 정규화하는 게 아니라 세그먼트를
    하나씩 꺼내는)를 필요로 한다면 이것의 자리를 조정하세요.
- **Phase 6 (라우터) — 완료.** `src/router/`: `route`/`route_kind`(네 가지 노드 종류를 가진
  경로 트라이 — static/param/wildcard/root — CONCEPTS.md §5의 정확한 static→가장 깊은
  param→wildcard 우선순위와 캡처 롤백을 그대로 가짐), `target`/`target_by(...)`(람다와
  바인딩된 멤버 함수 타겟), `middleware`/`middleware_stack`(실제 클로저를 통한
  chain-of-responsibility — 코루틴 + `shared_ptr`이 직접 안전한 캡처 시맨틱을 주기 때문에,
  기존과 달리 태그 스택 우회법이 필요 없음), `facade`/`group_proxy`(플루언트 DSL + "`group()`
  바디 안에서 건드려진 모든 라우트에 미들웨어를 한꺼번에 적용하기" 어법), 그리고
  `router`(트리를 `server::vpath`의 스코핑에 엮어주는 확장, 캡처를 가져오기 위한
  `route_of(req)`). "xfwk"에서 이름 변경됨 — 계승할 의미가 없었음. 85개 테스트 전부 경고
  없이 통과했고, 여기엔 `tests/unit/test_router_route.cpp`(계획에 따라 트라이 로직을
  직접 대상으로 함)와 실제 리스너를 통과하는 두 개의 통합 스위트가 포함됩니다.
  - **역사적 버그의 회귀 테스트는 `test_router_route.cpp`의
    "routing picks the deepest matching parameter branch, not the first one tried"입니다**
    — 이건 실제로 출시됐던 정확한 시나리오를 그대로 인코딩합니다(두 개의 parameter 브랜치가
    같은 세그먼트를 받아들이는데, 하나는 얕은 wildcard를 통해, 다른 하나는 두 개의 추가
    static 세그먼트를 통해 완전한 매치에 도달함) 그리고 **양쪽** 등록 순서 모두에서 더 깊은
    쪽이 이기는지 단언합니다 — 이게 실제로 순서 의존적인 회귀를 잡아내는 방식입니다. **이
    테스트가 실제로 의미 있는지 검증함**: 수정 로직의 `!have_best || trial.depth >
    best_state.depth` 비교를 일시적으로 "항상 마지막 매치를 취한다"는 규칙으로 바꿔서
    (역사적 버그가 그럴듯하게 재발한 상황을 시뮬레이션) 테스트가 실패하는지 확인한 뒤
    ("`:y`가 `:x`보다 먼저 등록됨" 순서에서 잘못된 브랜치가 선택됨) 되돌렸습니다 —
    `route_match`를 건드릴 때 이런 종류의 검증을 건너뛰지 마세요.
  - **설계 메모**: 이번 재설계의 수정은 원래 것과 다르게 표현되어 있습니다(원래의 "현재
    깊이보다 하나 낮게 `deep_state.depth`를 프라이밍하는" 트릭 대신, 첫 성공한 후보를
    항상 기준선으로 받아들이는 명시적인 `have_best` 플래그) — 시맨틱은 같지만 더 명백하게
    올바르고, 다시 미묘하게 잘못될 여지가 적습니다.
  - `"/"`(또는 다른 어떤 프리픽스든)에 마운트된 라우터는 그 프리픽스 아래 모든 것에 대해
    권한을 가집니다: 경로가 마운트에는 매치되지만 등록된 라우트가 없는 요청은 라우터 자신의
    404를 반환하고(`vpath::handle`의 "처리되지 않음 = 404" 폴백을 통해), 리스너의 다른
    확장이나 최종 핸들러로 폴스루하지 **않습니다**. 이건 버그가 아니라 올바르고 의도된
    스코핑 동작입니다 — `test_router_server.cpp`의 한 테스트가 처음에는 반대(501)를
    단언했다가 수정되어야 했습니다.
- **Phase 7 (WebSocket) — 완료.** `src/ws/`: 의존성 없이 직접 작성한 `sha1()`(FIPS 180-1 —
  약 70줄짜리를 위해 서드파티 파일을 벤더링할 가치는 없었음) + 핸드셰이크를 위한 base64
  인코딩(`compute_accept_key`, RFC 6455 §1.3의 예제로 검증됨), 그리고 `ws_connection` —
  **실제** RFC 6455 프레임 코덱: fin/opcode, 7/16/64비트 페이로드 길이, 마스킹(스펙에 따라
  서버는 항상 들어오는 걸 언마스킹하고 나가는 건 절대 마스킹하지 않음), 조각난 메시지
  재조립, 자동 ping→pong / close 핸드셰이크 처리. 이건 원래 구현이 스텁으로만 남겨뒀던
  기능이고(핸드셰이크는 동작했지만 프레임 입출력은 아니었음 — CONCEPTS.md §6), 이번 라운드엔
  완전히 구현되고 테스트되었습니다(96개 테스트 전부 경고 없이 통과), 여기엔 nhttp 자신의
  `ws::` 코드와 무관한 직접 만든 클라이언트를 사용하는 실제 소켓 위의 진짜 end-to-end
  핸드셰이크 + 에코 테스트(`tests/integration/test_websocket_server.cpp`)가 포함됩니다.
  - **이 단계에서 새로 생긴 프로토콜 업그레이드 메커니즘**: `server::response`에
    `upgrade_handler` 필드가 추가되었습니다(`std::function<task<void>(shared_ptr<io::stream>,
    std::string)>`). 어떤 확장이 이걸 설정하면(`websocket_endpoint::handle` 참고),
    `connection::write_response`는 상태 라인 + 확장 자신의 헤더만 보내고(자동 Content-Length/
    Transfer-Encoding/Connection 없음), 그다음 자신의 `wire_`를 갓 힙 할당된
    `io::socket_stream`으로 옮기고 그걸 — 그리고 `read_buffer_`에 남아 있던 것(HTTP 요청을
    지나서 이미 읽어버린 바이트가 있다면)까지 — 핸들러에 넘겨주며, `connection::run()`은
    다음 keep-alive 요청으로 계속하는 대신 자신의 HTTP 루프를 멈춥니다(`upgraded_` 플래그).
    이것이 CONCEPTS.md의 "드라이버 교체" 아이디어 뒤에 있는 구체적인 메커니즘이고, WebSocket
    뿐 아니라 미래의 어떤 프로토콜 업그레이드든 재사용할 이음매입니다.
  - `websocket_endpoint`의 `on_connect` 핸들러는 `co_await ws->receive()`가 `nullopt`을
    반환할 때까지 반복하는 루프로서 커넥션의 생애주기 전체를 소유합니다 — 원래의 스텁이었던
    `http_websocket`처럼 콜백 스타일(`on_message`/`on_disconnect`)이 아니라 의도적으로 이렇게
    만들었습니다, 진짜 코루틴을 쓸 수 있게 된 이상 평범한 루프가 훨씬 더 자연스럽기
    때문입니다.
- **Phase 8 (프로토콜 확장성 검증) — 완료.** `docs/protocol-extensibility.md`가 Phase
  4~7을 CLAUDE.md의 아키텍처 결정에 나온 세 가지 HTTP/2-/QUIC-준비도 이음매에 비추어
  검토합니다. 셋 중 둘은 변경 없이 성립했고; 실제 공백 하나를 발견해 수정했습니다:
  `server::connection`이 추상적인 `shared_ptr<io::stream>` 대신 구체적인 `io::socket_stream`을
  받아서 저장하고 있었는데, 이는 `connection` 자신의 시그니처를 바꾸지 않고서는 미래의
  non-TCP transport로 교체하는 걸 막았을 것입니다. 수정함(`connection.hpp`/`.cpp`,
  `listener.cpp`) — 이제 `listener::handle_connection`만이 유일하게 wire가 TCP
  `socket_stream`이라는 걸 아는 곳입니다; 이 수정으로 WebSocket 업그레이드마다 있었던
  불필요한 재포장도 제거됐습니다. 수정 후에도 96개 테스트 전부 변경 없이 통과합니다(행동을
  바꾸지 않는 타입 변경이기 때문). 이음매별 전체 결과는 문서 참고.
  - **같은 검토에서 나온 두 번째 수정, `overlay`에서**: `overlay::wants()`는 GET/HEAD에
    대해 무조건 `true`를 반환하고 실제 존재 여부 검사는 `handle()`로 미루곤 했습니다 —
    즉 overlay는 항상 모든 GET을 "차지"했고, 존재하지 않는 파일에 대한 요청은 우선순위가
    더 낮은 것이 있더라도 **다른 어떤 확장으로도 폴스루하지 않고** overlay 자신의 404를
    받았습니다. 이건 같은 마운트 포인트에 라우터와 정적 파일 overlay를 함께 계층화하는
    흔한 경우(존재하지 않는 정적 파일에 대한 요청은 최종적으로 404/501이 되기 전에
    라우터/다른 확장으로 폴스루해야 함)에는 거꾸로입니다. 수정함: `overlay::wants()`는
    이제 실제로 경로를 해석하고(`overlay::resolve`, `handle()`과 공유됨) 진짜로 존재하는
    파일에 대한 요청만 차지합니다 — 경로 순회 시도는 예외로, 다른 확장이 탈출한 경로를
    보게 놔두는 대신 확정적인 403을 줄 수 있도록 여전히 무조건 차지합니다. 이건 성공한
    요청이 파일을 두 번(한 번은 `wants()`에서, 한 번은 `handle()`에서) stat한다는
    뜻이지만; 이 규모에서는 아직 최적화할 가치가 없습니다(요청 태그 캐시가 필요할 것).
- **Phase 9 (예제 앱) — 완료.** `examples/nhttpd/main.cpp`(+ `examples/CMakeLists.txt`,
  `NHTTP_BUILD_EXAMPLES`가 켜져 있으면 자동으로 빌드됨 — 어디든 *새* CMakeLists.txt를 추가한
  뒤에는 한 번 `cmake -S . -B build`로 재구성해야 함, ninja는 그렇지 않으면 알아채지 못함)가
  새 API 위에서 USAGE.md의 기존 데모 엔드포인트들을 그대로 반영합니다: 정적 파일을 위한
  `overlay`, `/ws` 에코를 위한 `websocket_endpoint`, 그리고 `/whoami`, `/always-501`,
  `/exit`(`listener::stop()`을 호출함), `jay`/`kay` `param()` predicate가 있는
  `:user/profile|greetings|set` 그룹을 가진 `router`. 선택적인 디렉터리와 포트 인자를
  받습니다(`./nhttpd [dir] [port]`). **자동 테스트 스위트뿐 아니라 실제로 실행 중인
  인스턴스에 curl로 수동 검증함**: 정적 index/파일 서빙, byte-Range(올바른 `Content-Range`와
  함께 `206`), 라우터의 param/group/승격된-메서드 라우트, 존재하지 않는 정적 경로가 라우터
  자신의 404에 도달하는 `overlay`→라우터 폴스루, `/always-501`, 그리고 `POST /exit`을 통한
  깔끔한 종료.
  - **이 예제를 작성하는 중에 발견되어 수정된, 실제로 중요한 버그**: `overlay`/`single_file`은
    생성 시점에 **고정된 `io_context&`**를 받아서 자신의 모든 `thread_pool` 파일 I/O
    오프로드를 그 위에서 재개하곤 했습니다 — 하지만 리스너는 여러 워커 io_context를
    실행하고, 커넥션은 자신을 accept한 것에 고정됩니다(CLAUDE.md의 SO_REUSEPORT 결정).
    고정된 컨텍스트가 어떤 커넥션을 실제로 실행하고 있는 것과 우연히 다르다면, 거기서
    재개하는 것은 그 고정을 조용히 위반하는 것입니다: 코루틴이 *잘못된* 스레드에서 요청을
    계속 실행하면서, 소유 스레드만 필요로 한다고 설계가 가정하는 동기화 없이 그 커넥션의
    소켓/`io_registration` 상태를 건드리게 됩니다. 기존 테스트들은 이걸 잡아내지 못했습니다
    (너무 적은 워커에 걸친 너무 적은 동시 커넥션이라 신뢰성 있게 불일치하는 스레드에
    떨어지지 않았음). `request::io_ctx`를 추가해서(모든 요청마다 `connection::run()`이 그
    커넥션 자신의 컨텍스트로 설정함) `overlay`/`single_file`이 저장된 것 대신
    `*req.io_ctx`에서 재개하도록 해서 수정했습니다 — 이들의 생성자는 더는 `io_context&`를
    전혀 받지 않고, `thread_pool&`만 받습니다(호출부: `overlay_of(...)`/`file_of(...)`/
    `overlay(...)`/`single_file(...)`에서 컨텍스트 인자를 제거). **`thread_pool::run`으로
    작업을 오프로드하는 미래의 어떤 확장이든 반드시 `req.io_ctx`에서 재개해야지, 확장 자신의
    생성 시점에 캡처된 컨텍스트에서 재개하면 안 됩니다** — 이건 이제 이 패턴에 대한 반드시
    지켜야 할 규칙입니다.
  - **예제 자신의 주석에 문서화된, 버그는 아니지만 중요한 관련 상호작용**: `"/"`에 마운트된
    `router`(또는 어떤 `vpath`든)는 모든 요청을 사소하게 받아들이는 `wants()`를 가집니다
    (`"/"`에 대한 프리픽스 매칭은 항상 성공함), 그러므로 먼저 거부권을 가져야 하는 것 —
    예를 들어 정적 파일 `overlay`나 `websocket_endpoint` — 보다 *더 높은* 우선순위 번호
    (더 낮은 우선순위, 더 나중에 시도됨)를 줘야 합니다. 숫자 기본값(`vpath`/`router` =
    `0x80000000`, `overlay` = `0xE0000000`)은 `"/"`에 뿌리내린 라우터를 마운트한다면 이걸
    저절로 보장해주지 **않습니다**; 예제는 올바른 계층화를 위해 overlay에 명시적으로 더 낮은
    우선순위(`0x10000000`)를 넘깁니다. Phase 10의 `ReadMe.md` 재작성 전에 좀 더 생각해볼
    가치가 있음: *기본값* 자체를 바꿔야 할지, 아니면 그냥 눈에 띄게 문서화만 해야 할지
    (현재 기울어진 쪽: 문서화하기 — 지금 기본값을 바꾸면 이번 세션에 이미 있었던
    `overlay::wants()` 수정 위에 또 하나의 조용한 동작 변경을 얹는 셈이 됨).
- **Phase 10 (최종 정리) — 완료.** `ReadMe.md`가 새 API/빌드 안내와 설계 문서 링크로
  재작성됨. 사용자가 레거시 트리 삭제를 명시적으로 확인함; `git rm -r`로 제거됨(스테이징만
  됨, 커밋은 안 함 — 사용자가 커밋을 요청하지 않았음): `libnhttp/`, `libnhttp-tests/`,
  `nhttpd/`, `coverage/`, `nhttpd.sln`(524개 파일). `benchmark/`, `bench1.jpg`, `bench2.jpg`,
  `logo.png`는 의도적으로 그대로 뒀습니다 — 확인된 정리 범위 밖입니다(오래된 JMeter 벤치마크
  자산/이미지이지 레거시 코드가 아님). 제거 후 전체 스위트(96/96)가 통과함을 확인함. **이로써
  재설계가 완료됩니다** — 계획했던 10단계 전부 완료: 비동기 코어, 스트림, HTTP/1.1
  프로토콜(multipart 포함), 서버 코어, 확장, 라우터(우선순위 버그 회귀 테스트가 실제로
  의미 있음을 검증함), 실제 WebSocket 프레임 입출력, 프로토콜 확장성 검토(수정 두 건 발견 및
  적용), curl로 실제 검증된 예제 앱, 그리고 이 정리 작업.
  - **이 단계에서 발견되어 알린, 이 세션의 추적된 작업이 원인이 아닌 관련 없는 이상 현상**:
    `LICENSE`가 이 세션의 기록에 대응하는 편집 없이 디스크에서 이미 수정되어 있었습니다
    (`Copyright (c) 2021 neurnn corp` 줄이 제거됨). 조용히 커밋하거나 눈대중으로
    되돌리는 대신 사용자에게 명시적으로 드러냈고; 사용자는 현재 상태(줄이 제거된 상태)를
    유지하기로 확인했습니다.
- **Phase 11 (TLS/SSL) — 완료.** Phase 10 이후에 사용자가 명시적으로 요청한, 기존 구현이 갖고
  있던 공백들을 구현하는 작업의 일환으로 추가됨(`CONCEPTS.md`의 알려진 공백 목록 중 마지막
  항목이 TLS였고, multipart와 WebSocket은 이미 Phase 3/7에서 완료됨). `src/tls/` +
  `include/nhttp/tls/`: `tls_context`(`SSL_CTX*`를 감쌈, `create_server(cert_chain_file,
  private_key_file)`, 최소 버전 `TLS1_2_VERSION`)와 `tls_stream`(`io::stream`을 구현하므로
  `socket_stream`과 완전히 똑같은 방식으로 `connection`/`listener`에 그대로 꽂힘 — transport
  레이어 위 어떤 것도 바꿀 필요가 없었고, 위 결정 #7의 이음매 설계가 유효했음을 확인함).
  - **설계: `SSL_set_fd`가 아니라 메모리 BIO 쌍.** `tls_stream`은 SSL 객체를 소켓 fd에 직접
    바인딩하는 대신 OpenSSL에 두 개의 `BIO_s_mem()` BIO를 줍니다(`SSL_set_bio`). 모든 OpenSSL
    연산(`SSL_accept`/`SSL_read`/`SSL_write`)은 오직 이 메모리 내 BIO만 건드리며;
    `tls_stream`이 `feed_rbio_from_network()`/`flush_wbio()`에서 이것과 실제 `io::stream`
    사이의 암호문을 명시적으로 펌핑합니다(`co_await inner_->read/write(...)`). 이것이
    OpenSSL을 리액터 스레드의 블로킹 경로에서 완전히 떼어놓는 핵심입니다 —
    `SSL_ERROR_WANT_READ`/`WANT_WRITE`가 OpenSSL이 직접 fd를 `read()`/`write()`하려는 것이
    아니라(리액터 스레드를 그대로 블로킹시킴 — 결정 #3의 협상 불가능한 불변조건 위반) 평범한
    `co_await`로 바뀝니다.
  - **`listener::listen_tls(ep, cert_chain_file, private_key_file)`**는 `listen()`의 워커별
    `SO_REUSEPORT` 루프를 그대로 반영하며, 모든 워커 스레드가 `tls_context` 하나(`SSL_CTX*`
    하나)를 공유합니다 — 설정이 끝난 뒤로는 읽기 전용인 `SSL_CTX`에서 여러 스레드가 동시에
    `SSL_new()`를 호출하는 것은 OpenSSL 자체의 스레드 안전성 보장 범위 안입니다. `dispatch()`와
    `run_connection()`을 기존의 평문 HTTP 전용이었던 `handle_connection()`에서 분리해서 평문과
    TLS accept 경로 둘 다 하나의 커넥션 처리 구현을 공유하게 했습니다; transport 생성
    (`socket_stream` 대 `socket_stream`을 감싼 `tls_stream`)만 다릅니다.
  - **처음엔 오해를 불러일으켰던 긴 간헐적 실패 조사, 결국 라이브러리 버그가 **아님**으로
    결론남.** 이 단계에서 실행 중인 `examples/nhttpd` 인스턴스를 상대로 수동으로
    `curl -k https://...`를 스모크 테스트하는 동안 간헐적으로 실패했습니다(클라이언트 쪽
    `SSL_ERROR_SYSCALL`, 시도의 약 50%, `strace` 아래에서는 더 심함), 반면 `openssl s_client`,
    전용 블로킹 OpenSSL 테스트 클라이언트, 자동화된 Catch2 통합 스위트는 모두 안정적으로
    통과했습니다. 근본 원인은(결국 `tls_stream`/`listener`의 배관/스레딩 버그가 전혀 아니었고)
    **같은 오래 지속된 셸에서 이전의 수동 테스트 실행으로부터 남겨진 낡은 `nhttpd` 프로세스들**
    이었습니다 — 백그라운드로 실행(`&`)한 뒤 같은 포트로 새 인스턴스를 시작하기 전에 명시적으로
    죽이지 않아서, 이전 실행의 겹치는 포트에 여전히 `SO_REUSEPORT`로 바인딩된 채 남아있었던
    것입니다. `SO_REUSEPORT`는 의도적으로 여러 독립적인 리스닝 소켓이 — 심지어 관계없고 이미
    고아가 된 프로세스의 것이라도 — 한 포트를 공유하도록 허용하며, 커널은 *새* 커넥션을 그
    전부에 걸쳐 로드밸런싱합니다; 낡은/절반쯤 죽은 인스턴스에 걸린 커넥션은 실패하는데, 이게
    클라이언트 입장에서는 정확히 간헐적인 서버 쪽 버그처럼 보입니다. 조사 도중 `ps aux`로 그런
    낡은 프로세스가 살아있는 걸 직접 찾아내서 확인했고, 그 다음 환경이 낡은 리스너로부터
    깨끗하다는 걸 확인한 뒤에는 **100개 이상의 새 TLS 커넥션에서 실패 0건**을 재현함으로써
    (curl과 테스트 클라이언트 둘 다, 순차적으로도 기본 8개 워커 전체에 걸친 동시 버스트로도)
    확인했습니다. **이 저장소에서 향후 수동 스모크 테스트를 할 때의 교훈(WSL을 거쳐, 긴 세션에
    걸쳐): 새 수동 테스트의 결과를 신뢰하기 전에 항상 이전의 `examples/nhttpd`(또는 고정되고
    재사용되는 포트에 바인딩된 다른 어떤 테스트 바이너리든)가 아직 실행 중이지 않은지
    확인하세요 — 매 수동 실행 전에 `pgrep -f build/examples/nhttpd`를 하거나, 모든 Catch2
    통합 테스트가 이미 하고 있는 것처럼 포트 0(OS가 할당)을 사용하세요 — 이게 바로 자동화된
    스위트가 전혀 영향받지 않았던 이유입니다.**
  - 통합 테스트: `tests/integration/test_tls_server.cpp`(GET, POST 바디, keep-alive, 그리고
    여러 워커에 걸쳐 퍼지는 다수의 새 커넥션 테스트)가 `tests/support/raw_tls_http_client.hpp`
    를 사용합니다 — `nhttp::tls`의 자체 코드와 의도적으로 분리된 작고 독립적인 블로킹 OpenSSL
    클라이언트로, 공유된 버그가 양쪽이 서로 동의하는 뒤에 숨을 수 없게 합니다.
    `NHTTP_ENABLE_TLS`(CMake 옵션, 기본값 `ON`)가 OpenSSL `find_package` 요구사항과 위의 모든
    것을 게이팅합니다; `NHTTP_HAVE_TLS`는 그 결과로 생기는 컴파일 정의로 `listener.hpp`/`.cpp`
    와 예제 앱의 `#ifdef`들을 감쌉니다. `examples/nhttpd`는 인증서/키 경로 인자가 주어지면
    선택적으로 `port+1`에서 HTTPS도 서빙합니다(`./nhttpd [dir] [port] [cert.pem] [key.pem]`).
  - 전체 스위트: 이 단계 이후 100/100 경고 없이 통과.
- 계획에 남은 것이 없습니다. 이 저장소의 향후 작업은 깨끗하고 완전히 테스트된 C++20 구현에서
  시작합니다 — 위의 모듈 맵과 빌드 안내를, 그리고 사용자 대상 API 투어는 `ReadMe.md`를
  참고하세요.

## 이 저장소에 특화된 작업 스타일 메모

- 이건 크고 여러 단계로 이루어진 재작성입니다. 작업은 단계별로 진행되며(정확한 단계 목록은
  위의 계획 파일 경로 참고); 각 단계는 다음 단계를 시작하기 전에 트리를 빌드 가능하고 완전히
  테스트된 상태로 남겨둬야 하고, 각 경계마다 사용자에게 다시 확인받지 않고 단계들이
  진행됩니다 — 맨 마지막에 레거시 트리를 삭제하는 것만은 예외이며, 먼저 명시적인 확인이
  필요합니다.
- 기존 동작이 보존할 가치가 있는지 의심스러울 때는, 그냥 가정하기 전에 `CONCEPTS.md`
  (철학/불변조건)와 `USAGE.md`(호출부 모양)를 확인하세요 — 이 둘 다 기존 소스를 매번 다시
  읽지 않고도 그 질문에 답하기 위해 기존 구현을 처음부터 끝까지 완전히 읽고 작성된
  것입니다.
