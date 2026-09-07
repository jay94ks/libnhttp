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
2. **CMake** 빌드 시스템, 기존 `Makefile`/`.vcxproj`/`.sln`을 대체함. ~~Linux 우선: Windows
   지원은 의도적으로 미뤄짐~~ — **대체됨**: Windows(IOCP 기반) 지원이 Phase 12에서 추가되었습니다
   (아래 진행 로그 참고), 바로 이 결정이 요구했던 깔끔한 플랫폼 경계 이음매 위에 그대로
   얹혀서요 — `include/nhttp/platform/**`는 강화가 필요했지만(Phase 12 항목 참고),
   `src/platform/` 위쪽은 아무것도 바뀌지 않았습니다.
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
8. ~~HTTP/2와 QUIC은 구현되지 않음~~ — **부분적으로 대체됨**: HTTP/2는 Phase 14에서
   구현되었습니다(아래 진행 로그 참고), 바로 이 결정이 보존하라고 요구했던 세 가지 이음매
   위에 그대로 — 셋 다 변경 없이 유지되었음을 Phase 14 항목에서 확인했습니다.
   **QUIC은 명시적으로 계속 보류됩니다** — 사용자가 QUIC 구현을 벤더링할지 처음부터 RFC
   9000/9114/9204 스택을 직접 구현할지 논의한 뒤, 둘 다 하지 않고 완전히 보류하기로
   결정했습니다(이런 세션에서 처음부터 상호운용 가능한 수준으로 구현하는 건 비현실적이라고
   판단했고, 벤더링은 이 프로젝트의 '서드파티 코드 없음' 원칙을 깨뜨리기 때문입니다). 세 가지
   이음매: (a) connection/exchange 분리 — 라우터와 핸들러 코드는 커넥션당 요청 하나를 절대
   가정하면 안 됨; (b) transport에 무관한 비동기 스트림 추상화 — 드라이버는 오직 이걸 통해서만
   네트워크와 대화해야 하고 raw TCP 소켓을 절대 가정하면 안 됨; (c) 헤더는 디코딩된 키/값
   쌍으로 라우터/확장에 도달해야 하며 raw 와이어 바이트로는 절대 안 됨 — 그래야 HPACK/QPACK으로
   디코딩된 헤더가 그 레이어에서 HTTP/1.1 헤더와 구분되지 않습니다. 이 세 가지는 QUIC/HTTP-3을
   나중에 구현할 때도 여전히 필요한 이음매로 남습니다.
9. **경고 없는 빌드**(`-Wall -Wextra -Wpedantic`, 에러로 처리)는 지향점이 아니라 반드시
   지켜야 하는 요구 사항입니다 — 컴파일하려면 경고를 억제해야 하는 코드를 추가하지 마세요.

## 디렉터리 레이아웃

```
CMakeLists.txt              최상위 빌드 설정
cmake/                      CMake 헬퍼 모듈 (컴파일러 경고 등)
include/nhttp/              공개 헤더 (src/ 모듈 레이아웃을 그대로 반영)
src/
  platform/                 이식 가능한 공개 API (address/socket/reactor/file_info) + 이를
                             구현하는 posix/win32 하위 디렉터리 (epoll 대 IOCP, POSIX 대
                             Winsock) (Phase 12)
  async/                    task<T>, io_context, io_context_pool, 소켓 awaitable, 타이머,
                             thread_pool (블로킹 작업 오프로드)
  io/                       비동기 스트림 인터페이스 + memory/file/range/socket 스트림
  protocol/                 header/method/status/mime/date/query_string/resource/urlencode,
                             청크 코덱, multipart/form-data 파서
  server/                   listener, HTTP/1.1 connection 코루틴, connection_h2 (HTTP/2,
                             Phase 14), 공유 http1_io.* 프레이밍 헬퍼, request/response
                             파사드, 태그 저장소, params/설정
  server/extensions/        extension registry, vhost, vpath, 정적 overlay, 단일 파일 서빙,
                             reverse_proxy (Phase 13)
  router/                   트라이 기반 라우터 (facade/route/middleware/target), xfwk의 후계자
  ws/                       WebSocket 핸드셰이크 + RFC6455 프레임 코덱 + 비동기 send/recv
  tls/                      OpenSSL 기반 TLS/SSL (tls_context, tls_stream) — 메모리 BIO
                             패턴, io::stream을 구현하므로 그대로 꽂히는 transport; 서버
                             *및* 클라이언트 모드 (Phase 11, 클라이언트 모드는 Phase 13에서 추가)
  http2/                    HPACK (RFC 7541) + 프레임 코덱 (RFC 9113) — QUIC/HTTP-3은 없음 (Phase 14)
  depends/                  벤더링된 서드파티 (sha1, utf8) — 표준 시설로 간단히 대체 가능하지
                             않은 이상 그대로 재사용
tests/
  unit/                     모듈당 파일 하나, Catch2
  integration/              루프백 위의 실제 리스너, 작은 테스트 HTTP/WS/HTTP2 클라이언트로 구동됨
examples/
  nhttpd/                   기존 데모 엔드포인트를 반영하는 샘플 앱 (마지막 단계에서 작성됨)
libnhttp/, libnhttp-tests/, nhttpd/, coverage/, Makefile, *.vcxproj, *.sln
                             레거시 — 기존 C++17 구현, 손대지 않고 참고용으로만 유지,
                             새 구현이 동등해지고 사용자가 확인한 뒤에만 삭제됨
```

(위 레거시 항목 목록은 실제로 삭제되기 전 계획 당시의 원칙을 그대로 남겨둔 것입니다 — 실제
삭제는 Phase 10 항목 참고.)

## 빌드 & 테스트 — Linux (호스트가 Windows일 때는 WSL Ubuntu)

이 머신의 WSL Ubuntu 인스턴스에는 GCC 13.3.0, CMake 3.28, Ninja가 미리 설치되어 있습니다.
Windows 셸에서 호출한다면 명령어 앞에 `wsl.exe -d Ubuntu -- bash -lc "..."`를 붙이세요;
`C:\GitHub\libnhttp` 아래 경로들은 WSL 안에서 `/mnt/c/GitHub/libnhttp`로 접근 가능합니다.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## 빌드 & 테스트 — Windows (네이티브, Phase 12부터 — 더 이상 WSL 전용이 아님)

이 머신에는 Visual Studio 18 Community(MSVC `cl` 19.51+)와 CMake/Ninja가 네이티브로 설치되어
있습니다. 네이티브 Windows 셸(PowerShell/cmd, WSL **아님**)에서 먼저 MSVC 환경을 로드한 뒤,
Linux와 정확히 같은 방식으로 구성/빌드/테스트하세요 — `NHTTP_ENABLE_TLS=OFF`는 **이 머신에서만**
특별히 필요합니다(Phase 12 진행 로그 항목 참고: MSVC와 링크 가능한 OpenSSL 개발 패키지가
설치되어 있지 않음 — 설계상의 공백이 아니라 빌드 환경상의 공백입니다):

```bash
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNHTTP_ENABLE_TLS=OFF
cmake --build build-win
ctest --test-dir build-win --output-on-failure
```

(`vcvars64.bat`의 환경은 이 환경에서 인라인 PowerShell 한 줄 명령을 통해서는 안정적으로
전달되지 않습니다 — 인라인으로 체이닝하지 말고 위 내용을 `.bat` 파일로 만들어 실행하세요.)

작업의 모든 단계는 다음 단계로 넘어가기 전에 빌드+테스트 명령어가 **두 플랫폼 모두에서**
컴파일러 경고 0개로 성공하는 상태로 트리를 남겨둬야 합니다.

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
- **Phase 12 (Windows 지원 & 강화된 플랫폼 경계) — 완료.** 사용자가 원래 구현과의 기능 동등성을
  넘어서(리버스 프록시, Windows, HTTP/2 — QUIC은 명시적으로 계속 보류, 위 결정 #8 참고) 요청한
  작업. 실제로 컴파일되고 테스트된 지원으로 검증됨: 이 머신에서 MSVC로 네이티브로(VS 18
  Community, `cl` 19.51, CMake/Ninja 모두 Windows에 네이티브로 존재) — 최선을 다한
  미검증 코드가 아니라, 이후 모든 단계에서 두 플랫폼 모두 `ctest`가 100% 통과합니다.
  - **공개 플랫폼 헤더들이 이 단계 이전부터 이미 POSIX 타입을 누출하고 있었고**, 이게 바로
    "Windows 지원"의 대부분이었습니다: `include/nhttp/platform/address.hpp`가 `ip_address`/
    `endpoint`의 공개 인터페이스 자체에 `in_addr`/`in6_addr`/`sockaddr*`를 담고 있었고;
    `socket.hpp`는 `sockaddr_storage`/`socklen_t`/`ssize_t`를 담고 네이티브 핸들을 `int`로
    저장했으며(Windows `SOCKET`엔 틀린 타입); `io_context.hpp`는 private 필드 하나를
    선언하려고 `platform/epoll.hpp`를 include했습니다. 모든 공개 플랫폼 타입을 이식 가능하게
    만들어 수정: `ip_address`는 `in_addr`/`in6_addr` 대신 원시 주소 바이트
    (`std::array<uint8_t,4/16>`)를 저장; `socket_handle`은 `native_socket_t` typedef
    (`#if defined(_WIN32) std::uintptr_t #else int #endif` — 순수 언어 기능, 이걸 쓰는 데
    OS 헤더가 필요 없음)를 사용하고 `read`/`write`는 `std::int64_t`를 반환; `accept()`는
    `sockaddr_storage&`를 받는 대신 `optional<pair<socket_handle, endpoint>>`를 반환;
    sockaddr 변환은 `src/platform/{posix,win32}/sockaddr_convert.hpp`(내부 전용, 설치되지
    않음)로 이동; `io_context`는 이제 구체적인 `epoll_handle` 대신 `unique_ptr<platform::
    reactor>`(새로운 이식 가능 인터페이스 — `epoll_handle`이 이미 갖고 있던 것과 같은
    `add`/`modify`/`remove`/`wait`)를 가지므로 `io_context.hpp`는 더 이상 OS 타입을 전혀
    이름으로 갖지 않습니다. `overlay`/`single_file`도 더 이상 `<sys/stat.h>`를 include하지
    않습니다 — 새로운 `platform::file_info`/`stat_file()`(이식 가능한 `{kind, size, mtime}`)이
    그 공개 인터페이스에서 `struct stat`를 대체했습니다.
  - **설계 — 두 플랫폼에서 준비성(readiness) 기반 `io_context` 계약을 동일하게 유지했습니다.**
    `async_socket`/`io_context`를 IOCP의 네이티브 완료(completion) 모델 중심으로 재설계하는
    것(모든 소켓 호출 지점을 건드리는 훨씬 크고 위험한 재작성)을 의도적으로 하지 않았습니다.
    Windows용 `iocp_reactor`(`src/platform/win32/reactor.cpp`)는 **WSAEventSelect +
    스레드풀 대기(`RegisterWaitForSingleObject`)로 Win32 이벤트 객체 신호를 평범한
    `PostQueuedCompletionStatus`를 통해 같은 IOCP 완료 포트로 연결**하는 방식을 씁니다 —
    순수하게 관찰만 하는 방식이라, `async_socket::read_some/write_some/accept/connect`와
    `socket_handle`에 있던 *이미 이식 가능한* would_block() 재시도 루프는 전혀 바뀔 필요가
    없었습니다. 이 단일 메커니즘(등록당 하나의 `WSAEventSelect` 마스크: 읽기 관심에는
    `FD_READ|FD_ACCEPT|FD_CLOSE`, 쓰기 관심에는 `FD_WRITE|FD_CONNECT|FD_CLOSE`)이 리스닝
    소켓, 연결 중인 소켓, 이미 맺어진 커넥션을 균일하게 다룹니다 — 처음에 시도했다가 버린
    다른 두 설계와 그 이유는 아래 참고.
  - **이 리액터를 만들면서 발견하고 고친 실제 버그 세 가지**(발견 순서대로 — 각각이 다음에
    다시 겪지 않을 가치가 있는, 진짜이고 뻔하지 않은 Windows 소켓 프로그래밍 함정이라 자세히
    남겨둡니다):
    1. *MSVC는 BOM이 없으면 이 저장소의 UTF-8 소스 파일을 UTF-8이 아니라 시스템 코드페이지로
       디코딩합니다.* 이 머신(한국어 로캘)에서는 CP949이고; 주석에 있는 모든 비-ASCII
       문자(이 파일 전체 포함)가 `C4819`를 유발했고 `/WX` 아래에서는 치명적 오류였습니다.
       `cmake/CompilerWarnings.cmake`의 MSVC 분기에 `/utf-8`을 추가해서 해결 — 장식이
       아니라, 영어/UTF-8 기본 로캘 밖에서 이 트리가 MSVC로 컴파일되려면 반드시 필요합니다.
    2. *연결된 소켓의 읽기 준비성을 위한 표준 기법인 제로바이트 오버랩드 `WSARecv`는 리스닝
       소켓에는 전혀 적용되지 않습니다*(Winsock이 아예 거부함 — 리스닝 소켓엔 데이터 채널이
       없음), 그리고 관용적인 IOCP식 답인 `AcceptEx`는 무장(arm)하는 과정 자체가 대기 중인
       커넥션을 *소비*해버려서, 이 리액터의 "준비성을 신호하고, 범용 재시도 루프가 실제
       작업을 하게 둔다"는 계약과 맞지 않습니다 — `read`/`write`/`connect` 전부가 똑같이
       의존하는 바로 그 계약입니다. 이게 위의 통합 `WSAEventSelect` 설계로 이어진 계기였습니다
       (읽기엔 제로바이트 `WSARecv` 기법을, 리스닝 소켓엔 `WSAEventSelect`/`FD_ACCEPT`만
       섞어 쓰던 초기 버전을 대체함).
    3. *`WSAEventSelect(socket, NULL, 0)`은 이벤트 핸들을 닫기 **전에** 이전 이벤트 연결을
       해제하기 위해 호출되어야 합니다* — 이벤트를 먼저 닫아버리면(제가 처음 시도했던 방식,
       그 호출을 건너뜀) 소켓 자체가 망가진 상태가 됩니다: 이후의 모든 `accept()`/`recv()`/
       `send()`가 소켓을 닫은 게 아무것도 없는데도 `WSAENOTSOCK`으로 실패합니다. `async_socket
       accept/connect/read/write round trip`(Phase 1의 유닛 테스트, 리액터를 실제로 구동하는
       첫 번째 테스트)이 "pure virtual method called"로 크래시하는 것으로 나타났고 — `gdb`의
       코루틴 프레임 백트레이스 지원으로 추적한 결과 이미 손상된 `io::stream`에 대한 참조를
       읽는 `buffered_wire_stream`으로 귀결되었습니다. `iocp_reactor::remove()`의 리스닝
       소켓 분기(`src/platform/win32/reactor.cpp`)에서 `WSACloseEvent` 전에
       `WSAEventSelect(fd, nullptr, 0)`을 호출하도록 수정.
    4. *`SSL_set_tlsext_host_name`의 매크로 확장에 구식 C 캐스트가 들어있어서* GCC/Clang에서
       모든 호출 지점마다 `-Wold-style-cast`를 유발합니다(Phase 13이 클라이언트 모드 TLS의
       첫 호출자를 추가하면서 비로소 드러남). 매크로 대신 `SSL_ctrl`을 적절한
       `static_cast`/`const_cast`로 직접 호출해서 수정 — `src/tls/stream.cpp` 참고.
    5. *Windows에는 인바운드 TCP accept 부하분산을 위한 `SO_REUSEPORT` 대응물이 없습니다* —
       같은 포트에 독립적인 리스닝 소켓 N개를 바인딩해도 Linux처럼 커널이 균형 있게 accept를
       분배해주지 않습니다. `platform::socket_handle::set_reuse_port()`는 이를 정직하게
       보고합니다(성공한 척하지 않고 Windows에서는 `false`를 반환); `listener::listen()`/
       `listen_tls()`는 이걸 실제 기능 확인으로 취급합니다: 사용할 수 없으면 리스닝 소켓
       딱 하나만 바인딩하고, 새로운 `io_context::schedule()` 원시 기능(호출한 코루틴을
       중단시키고 *특정* 대상 컨텍스트 자신의 스레드에서 재개시키는 것 — `thread_pool::run()`이
       내부적으로 이미 하던 것의 일반형)을 통해 accept된 각 커넥션을 명시적으로 다른 워커에
       라운드로빈으로 분배합니다. 이건 플랫폼 간 겉치레가 아니라 진짜 동작 차이이며,
       `listener.hpp`의 문서 주석이 이제 SO_REUSEPORT를 무조건 가정하지 않고 "가능한 경우"라고
       말하는 이유입니다.
  - 발견했지만 아직 고치지 않은 공백: 이 머신에는 (MSVC와 링크 가능한) OpenSSL 개발 패키지가
    설치되어 있지 않습니다(MSYS2/Git에 딸려온 `openssl.exe` CLI만 있음) — 이 단계와 이후
    단계의 Windows 검증 빌드는 `-DNHTTP_ENABLE_TLS=OFF`로 구성합니다. `tls_context`/
    `tls_stream` 자체는 Windows 전용 수정이 필요 없습니다(OpenSSL은 이미 크로스플랫폼) —
    이건 이 특정 머신의 빌드 환경상의 공백이지 설계상의 공백이 아닙니다.
  - CMake: `src/CMakeLists.txt`가 `WIN32`에 따라 `platform/{posix,win32}/*.cpp`를 선택하고,
    거기서는 `Threads` 대신 `ws2_32`/`mswsock`을 링크합니다; 최상위 `CMakeLists.txt`의 강경한
    "Linux 전용" 경고가 이제 `WIN32`도 받아들입니다(그 외는 여전히 거부). `cmake/
    CompilerWarnings.cmake`에 MSVC 분기가 추가됨(`/W4 /permissive- /utf-8`,
    `NHTTP_WARNINGS_AS_ERRORS` 아래 `/WX`) — 이 프로젝트가 쓰는 여러 GCC/Clang 플래그
    (`-Wshadow`, `-Wold-style-cast`, `-Wconversion` 등)에 정확히 대응하는 것이 없어서, 근접하지만
    완전히 동일하지는 않습니다.
  - 전체 스위트: 이 단계 끝에서 Linux(WSL/ctest)와 네이티브 Windows(MSVC/ctest) **둘 다**에서
    96/96 경고 없이 통과; 예제 앱도 Windows에서 실제로 수동 검증됨(듀얼스택 리슨, `overlay`를
    통한 정적 파일 서빙, 라우터, `POST /exit`를 통한 정상 종료) — 정확한 명령어는
    `ReadMe.md`의 Windows 빌드 섹션 참고.
- **Phase 13 (리버스 프록시) — 완료.** 새로운 확장 `reverse_proxy`(`src/server/extensions/
  reverse_proxy.cpp`)는 `router`와 정확히 같은 방식으로(같은 "URL 접두사에 마운트되고, 중첩
  registry 대신 `on_handle()`을 쓰는" 모양) `vpath`에서 파생됩니다 — 전체 범위: 라운드로빈
  로드밸런싱을 하는 다중 업스트림, HTTPS 업스트림, WebSocket 업그레이드 패스스루까지, 범위를
  정할 때 사용자가 고른 네 가지 옵션 전부이며 단일 고정 업스트림만이 아닙니다.
  - **`connection.cpp`에서 뽑아낸 공유 HTTP/1.1 메시지 입출력**을 `src/server/
    http1_io.{hpp,cpp}`(`read_headers`, `make_body_stream`, `write_all`,
    `write_message_body`)로 옮겼습니다 — 서버 역할인 `connection`과 새로운 프록시 클라이언트
    코드 둘 다 변경 없이 그대로 사용합니다, 그래서 프록시된 업스트림 요청/응답 프레이밍이
    서버 자신의 것과 조용히 어긋날 수 없습니다(청크 코덱 하나, 프레이밍 규칙 하나를 양방향
    모두에서 사용). `protocol::http_status`에 `http_resource::try_parse`를 반영한
    `try_parse`가 추가되었는데, 업스트림의 상태 줄을 읽는 데 필요합니다.
    `protocol::header_value_contains_token`도 같은 이유로 `connection.cpp` 로컬 헬퍼에서
    `http_header.hpp`의 공유 함수로 승격되었습니다.
  - **클라이언트 모드 TLS**: 기존의 서버 전용 `create_server`/`accept()`와 나란히
    `tls_context::create_client(verify_peer)`와 `tls_stream::connect(sni_hostname)`가
    추가되었습니다 — 같은 메모리 BIO 펌프 루프를 그대로 쓰고, `SSL_set_accept_state` 대신
    `SSL_set_connect_state` + SNI(`SSL_ctrl`, Phase 12의 버그 #4 참고)만 다릅니다. 인증서
    검증은 기본적으로 켜져 있습니다.
  - **작성 중 발견하고 고친 실제 수명(lifetime) 버그 크래시**: `reverse_proxy::on_handle`이
    `http1_io::make_body_stream`으로 업스트림의 디코딩된 응답 바디를 만들었는데, 이 함수는
    호출자의 leftover 바이트 버퍼와 wire에 대한 *참조*를 갖는 `buffered_wire_stream`을
    반환합니다 — `connection.cpp` 자신의 사용에서는 안전합니다(그 참조들이 커넥션 전체 동안
    살아있는 `connection` 자신의 멤버 필드를 가리키므로) 하지만 여기서는 아닙니다:
    `on_handle`의 코루틴 프레임(과 그 로컬 변수 `upstream_leftover`/`wire`)은 `on_handle`이
    반환하자마자 파괴되는데, 이는 `resp.body`가 다운스트림 커넥션의 `write_response`에 의해
    실제로 읽히기 *전*입니다 — 댕글링 참조를 남기고, 이는 최초의 프록시 테스트에서 Catch2의
    치명적 신호 핸들러에 의해 "pure virtual method called"로 잡혔습니다. `http1_io::
    make_owned_body_stream` / `owned_buffered_wire_stream`을 추가해서 수정했는데, 이건
    호출자의 스택 프레임을 참조하는 대신 leftover 바이트의 옮겨진 복사본과 wire에 대한
    `shared_ptr`을 *소유*합니다 — `reverse_proxy`가 참조 기반 버전 대신 이걸 사용하며,
    `make_body_stream`의 문서 주석이 이제 앞으로의 호출자들에게 이 정확한 수명 함정을
    명시적으로 경고합니다.
  - 라운드로빈은 (Phase 12의 SO_REUSEPORT 대체 워커 분배와 같은 패턴인) 평범한
    `std::atomic<std::size_t>` 카운터를 씁니다. Phase 4가 기록한 것과 같은 정신의 알려진
    단순화: **업스트림 커넥션 풀링 없음**(프록시된 요청/업그레이드마다 새 커넥션)과 **능동
    헬스체크 없음**(다운된 업스트림은 그냥 그 요청 하나만 `502`로 실패) — 둘 다 합리적인
    후속 작업이며, 오늘 올바르게 동작하는 데 필수는 아닙니다.
  - 통합 테스트(`tests/integration/test_reverse_proxy.cpp`): 일반 GET과 POST 바디가
    end-to-end로 릴레이됨, 도달 불가능한 업스트림에 대해 `502`(진짜로 거부되도록 raw
    `socket_handle`을 바인딩한 뒤 즉시 닫음 — 그냥 "바인딩됐지만 아무도 `accept()`하지
    않는" 상태로 두면 프록시의 `connect()`가 실패 대신 멈춰버리는데, 이 테스트를 작성하며
    실수로 겪고 잡아낸 문제입니다), 가짜 업스트림 두 개에 걸친 라운드로빈 분배, 그리고
    프록시를 통해 왕복하는 WebSocket 에코. 전체 스위트: Linux와 Windows에서 105/105 경고
    없이 통과.
- **Phase 14 (HTTP/2) — 완료, 원래 계획에서 범위가 축소됨.** 새로운 `src/http2/` 모듈과
  HTTP/1.1 `connection`을 대체하지 않고 나란히 놓이는 새로운 커넥션 드라이버
  `server::connection_h2` — 위 결정 #8의 Seam 1(`docs/protocol-extensibility.md`의 "다른
  커넥션 타입, 같은 하위 메커니즘" 주장)이 정확히 쓰인 그대로 유지됨을 확인합니다:
  `connection_h2`는 다중화된 프레임 레이어를 여러 개의 동시적인 `request`/`response` 교환으로
  디코딩하고, 각각을 HTTP/1.1이 쓰는 것과 *같은* `listener::dispatch()`로 디스패치하며,
  `extension.hpp`, `router/`, 어떤 확장에도 변경이 필요 없습니다.
  - **HPACK**(`src/http2/hpack.cpp`, RFC 7541): 정적 테이블(Appendix A), 처음부터 작성한
    허프만 코덱(Appendix B의 코드 테이블을 기억으로 옮겨적었는데 — 257개 항목짜리 비트 단위로
    정확해야 하는 테이블에서 옮겨적기 오류의 실제 위험을 고려해 — 다른 어떤 것도 신뢰하기 전에
    `tests/unit/test_hpack.cpp`에서 **RFC 7541 Appendix C.4.1의 공식 허프만 코딩 테스트
    벡터**로 의도적으로 검증했습니다), 테이블로부터 한 번 만들어지는 디코드 트라이, §5.1/§5.2에
    따른 정수/문자열 표현, 그리고 디코더 쪽을 위한 동적 테이블. 인코더는 항상
    literal-without-indexing만 내보냅니다(동적 테이블을 전혀 쓰지 않음) — 더 단순하면서도
    완전히 RFC를 준수합니다(디코더는 어떤 유효한 표현 선택도 받아들여야 하므로, 그저 최대로
    압축되지 않을 뿐입니다); 이는 또한 피어의 `SETTINGS_HEADER_TABLE_SIZE`가 이 인코더의
    동작에 아무 영향이 없다는 뜻인데(테이블을 채우지 않으니 테이블 크기 제한도 필요 없음),
    `connection_h2`의 SETTINGS 처리가 이유 없이 조용히 무시하는 대신 이 점을 명시적으로
    기록합니다.
  - **프레임 코덱**(`src/http2/frame.cpp`): 9바이트 프레임 헤더, SETTINGS/WINDOW_UPDATE/
    RST_STREAM/GOAWAY/PING 페이로드 읽기/쓰기, 그리고 DATA와 HEADERS가 공유하는 패딩 제거.
    서버 푸시, 오래된 `Upgrade: h2c` 부트스트랩, PRIORITY 프레임 재정렬은 의도적으로
    구현하지 않았습니다(푸시는 현재 브라우저들에서 실질적으로 사용 중단됨; 이미 구현된
    prior-knowledge가 curl의 `--http2-prior-knowledge`를 포함해 현재 거의 모든
    클라이언트/도구가 실제로 쓰는 방식; PRIORITY는 5바이트 페이로드를 건너뛸 만큼만 파싱하고
    나머지는 무시하는데, 이는 RFC 9218 자체가 원래의 우선순위 체계를 경시하는 것과 일치합니다).
  - **`connection_h2`**는 연결 프리페이스 + SETTINGS 교환, 스트림별 상태, 흐름 제어(연결
    및 스트림 수준 송신 윈도우, `DATA`를 소비한 직후 `WINDOW_UPDATE`로 즉시 보충 — 단순하고
    올바르지만 가장 대역폭 효율적인 배칭은 아님)를 소유합니다. 스트림의 응답 작성 코루틴은
    송신 윈도우가 소진되면 스트림별 `coroutine_handle`에서 중단되고, 프레임 읽기 루프의
    `WINDOW_UPDATE` 처리가 직접 재개시킵니다 — 의도적으로 일반적인 `async::` 원시 기능이
    아닌데, 정확히 이 하나의 용도만 필요하기 때문입니다. 여러 동시 스트림은 (스트림의
    헤더와, 있다면 바디까지 완전히 수신되면 프레임 읽기 루프가 생성하는) 진짜로 독립적인
    detached 코루틴이며, 반드시 그래야 하는 곳에서만 직렬화됩니다: 실제로 프레임을 wire에
    쓰는 부분을, 작은 단일 스레드 협조적 `writer_lock`으로(진짜 뮤텍스가 아님 — 한 커넥션의
    모든 코루틴은 구조상 같은 io_context 스레드에서 실행되므로, 이건 그저 인터리빙된 코루틴
        중단만 조정하면 되고 진짜 스레드 간 경합은 절대 없습니다). 자동화된 스위트를 작성하기
    전에 **실제 curl**(`--http2-prior-knowledge`, 하나의 TCP 커넥션 위에서 여러 스트림을
    진짜로 다중화하는 `-Z` 병렬 모드 포함)이 예제 앱의 정적 파일, 라우터, 파라미터 라우트와
    상호운용함을 검증했습니다.
  - **협상, 원래 계획에서 범위 축소됨: 이번엔 prior-knowledge 평문만, ALPN-오버-TLS는
    명시적으로 보류**(시간 제약 하에 내린 실제적이고 정직한 범위 축소이지 실수가 아닙니다 —
    결정 #8에서 QUIC의 보류를 표시한 것과 같은 방식으로 여기 표시해서, 나중에 조용히
    "다 됐다"고 가정되지 않도록 합니다). `listener::handle_connection`(평문 accept 경로만 —
    TLS의 `handle_connection_tls`는 건드리지 않음)이 모든 평문 커넥션의 첫 4바이트를
    엿봅니다(`"PRI "`는 HTTP/2 클라이언트 프리페이스의 시작이고, RFC 9113 §3.4, 실제
    HTTP/1.1 메서드는 절대 만들어내지 않는 요청줄 모양입니다 — RFC가 정확히 이렇게
    구분되도록 일부러 고른 것) 그리고 그에 따라 `connection_h2` 또는 `connection`을
    구성하며, 그 바이트들을 각 드라이버의 초기 버퍼로 그대로 재생합니다(`io::stream`에는
    비파괴적 엿보기가 없어서, `connection`의 생성자에 `read_buffer_`를 그 바이트들로 시드하는
    `initial_buffer` 매개변수가 추가되었습니다). 이건 이제 HTTP/2뿐 아니라 **모든** 평문
    커넥션에 추가된 작지만 영구적인 4바이트 읽기입니다 — HTTP/1.1을 포함한 기존 전체 스위트가
    변경 없이 계속 통과함을 확인했습니다.
  - 그 외 알려진 단순화: **요청 바디는 스트림의 핸들러가 실행되기 전에 완전히 버퍼링됩니다**
    (h2를 통한 실시간 점진적 요청 바디 스트리밍 없음 — 더 단순하지만, 큰 업로드를 메모리에
    완전히 들고 있어야 하는 대가가 있고, 지금은 스트림당 고정 16MiB로 제한되며 아직 `params`에
    연결되지 않음); **응답 헤더는 HEADERS 프레임 하나에 들어간다고 가정합니다**(송신 쪽에는
    CONTINUATION이 없음 — 실제 헤더 세트는 거의 항상 `SETTINGS_MAX_FRAME_SIZE`보다 훨씬
    작음); **`SETTINGS_INITIAL_WINDOW_SIZE` 변경은 이후에 열리는 스트림에만 적용**되고
    RFC 9113 §6.9.2가 요구하는 완전한 일반성처럼 이미 열린 스트림에 소급 적용되지 않습니다
    (실제 클라이언트는 스트림을 열기 전에 초기 SETTINGS를 보내므로 실제 사용에서는 문제가
    되지 않음); **`SETTINGS_MAX_CONCURRENT_STREAMS`는 강제되지 않습니다**; `response::
    upgrade_handler`(예: `websocket_endpoint`나 `reverse_proxy`의 WS 패스스루에서)는 h2에서
    조용히 시도되는 대신 `501`을 받습니다 — RFC 9113 §8.5는 HTTP/1.1 Upgrade 메커니즘을
    전혀 지원하지 않으므로 대체할 올바른 동작이 없습니다.
  - 통합 테스트(`tests/integration/test_http2_server.cpp`)는 이 라이브러리 *자신*의
    `http2::frame_header`/`hpack_encoder`/`hpack_decoder` 위에 만든 `raw_h2_client`를
    씁니다 — 다른 모든 통합 스위트의 독립 파서 철학에서 의도적으로 벗어난 것인데, HPACK/프레이밍
    정확성은 이미 `test_hpack.cpp`에서 RFC 벡터로 독립적으로 검증되었으므로 여기서 재사용하는
    게 실제로 테스트의 초점을 (코덱 정확성을 다시 다투는 게 아니라) `connection_h2`의 드라이버
    로직 — 프리페이스/SETTINGS 핸드셰이크, 다중화, 흐름 제어 — 에 맞추기 때문입니다. 작성 중
    잡아낸 실제 테스트-클라이언트 버그 하나: 다중화 테스트의 첫 버전은 현재 기다리고 있는
    스트림이 아닌 다른 스트림에 속한 프레임을 *버렸는데*, 나중 호출이 읽기 전에 그 스트림의
    응답을 조용히 잃어버렸습니다 — 테스트 클라이언트 자체에서 스트림별로 대상이 아닌 프레임을
    버리지 않고 버퍼링하도록 고쳤습니다. 다루는 것: 단일 요청/응답, POST 바디, 그리고 요청한
    것과 *반대* 순서로 읽어들이는 두 개의 동시 다중화 스트림(순차적 완료가 아니라 진짜
    인터리빙을 검증). 전체 스위트: Linux와 Windows에서 113/113 경고 없이 통과.
- **Phase 15 (nginx/Apache 대비 벤치마크) — 완료.** nginx와 Apache 2 대비 벤치마크를 돌려서
  `ReadMe.md`에 결과를 반영해달라는 사용자 요청에 따름. 그 과정에서 실제 버그 두 개를 발견해서
  고쳤습니다 — 지속 부하 벤치마크는 기존 테스트 스위트(테스트당 수백만이 아니라 수십 건의
  요청)로는 절대 드러나지 않았을 종류의 문제였고, 둘 다 그냥 알려진 공백으로 적어두는 게 아니라
  지금 `main`에 실제로 수정되어 있습니다:
  1. **`task<T>`가 `co_await`할 때마다 자기 자신의 코루틴 프레임을 누수시키고 있었습니다 —
     이 재작성의 첫 커밋부터 계속.** `include/nhttp/async/task.hpp`의
     `operator co_await() &&`가 콜리(callee)의 코루틴 핸들을 절대 파괴하지 않는 일회용
     `awaiter`에게 넘기면서 `std::exchange(handle_, nullptr)`로 task 자신의 `handle_`을
     비워버렸고, 그 결과 `handle_.destroy()`를 호출하는 유일한 곳인 task 자신의 소멸자도
     아무 일도 하지 않게 됐습니다. `final_suspend()`는 그저 suspend한 뒤 continuation으로
     symmetric transfer할 뿐, 프레임을 파괴하지 않습니다. 결과적으로 완료된 task의 코루틴
     프레임을 파괴하는 곳이 어디에도 없었습니다. `task<T>`는 이 코드베이스 거의 모든 비동기
     함수의 반환 타입이므로, 어디서든 중첩된 `co_await`마다 누수가 발생했습니다. 지속적인
     `wrk` 벤치마크로 정적 파일 서빙 RSS가 23만 3천 요청 동안 5.6&nbsp;MB에서
     5.78&nbsp;GB까지 자라는 것으로 잡아냈고(요청당 약 24&nbsp;KB — 한 요청이 `task<T>`를
     반환하는 함수를 수십 번 중첩 호출하며, 그중 몇몇은 `write_message_body`/`read_headers`의
     4&nbsp;KB 스크래치 버퍼처럼 코루틴 프레임에 그대로 박히는 스택 로컬 버퍼를 갖고 있다는
     걸 감안하면 그럴듯한 수치), 서버와 완전히 분리한 최소 재현으로도 확인했습니다(아무 일도
     안 하는 `task<int>`를 200만 번 await하니 약 125&nbsp;MB 누수; 수정 후엔 거의 0).
     **수정**: `operator co_await()`에서 더 이상 `handle_`을 비우지 않도록 함. 흔한
     `co_await foo()` 형태에서 `foo()`는 prvalue 임시 객체이고 — 일반적인 C++ 임시 객체
     수명 규칙에 따라 — 그 수명은 전체 표현식이 끝날 때까지, 즉 `await_resume()` 이후까지
     이어집니다; `handle_`을 그대로 두면 이미 올바르게 동작하는 task 자신의 소멸자가 바로 그
     시점에 실행되어 이제 완료된 프레임을 해제합니다 — cppcoro 스타일 task 타입들이 쓰는
     것과 정확히 같은 패턴입니다. 검증: 기존 113/113 테스트가 변경 없이 그대로 통과(이 수정은
     메모리가 해제되는 *시점*만 바꿀 뿐 관찰 가능한 결과는 전혀 바꾸지 않습니다 —
     `await_resume()`은 기존 코드와 새 코드 모두에서 awaiter 자신의 핸들 사본을 통해 이미
     결과를 뽑아냈습니다), 30초/200-커넥션 지속 벤치마크에서 이제 RSS가 부풀지 않고
     ~13&nbsp;MB로 평탄하게 유지되며, task를 await한 뒤 `.valid()`/`.done()`이 (의도치
     않게) 일찍 무효화되는 옛 동작에 의존하는 코드가 없음을 grep으로 확인했습니다.
  2. **연결을 중간에 리셋하는 클라이언트 하나가 그 연결만이 아니라 서버 프로세스 전체를
     죽였습니다.** 이미 상대가 리셋한 소켓에 `write()`하면 `SIGPIPE`가 발생하는데, 기본
     처리 방식은 프로세스 전체를 즉시 종료시키는 것입니다 — core도 없고, 디버거나
     새니타이저가 잡을 것도 없습니다(확인: 완전히 동일한 바이너리의 ASan+UBSan 빌드가 이렇게
     죽었을 때 새니타이저 출력이 전혀 없었고, 이번 조사 중 그보다 앞서 `SIGABRT` 같은 치명적
     시그널은 실제로 잡아냈던 WSL 커널 자체의 크래시 캡처도 아무것도 기록하지 않았습니다).
     그래서 처음엔 원인 모를 조용한 크래시처럼 보였습니다; `strace -f -e trace=exit,
     exit_group,kill,tkill,tgkill,rt_sigaction`으로 서버를 감싸서 근본 원인을 찾았는데,
     `wrk`가 벤치마크 실행 종료 시 커넥션 풀을 정리하는 바로 그 순간 모든 스레드에 걸쳐
     `+++ killed by SIGPIPE +++`가 찍혔습니다 — 엣지 케이스가 아니라 실제 부하 상황에서는
     그냥 흔히 벌어지는 일입니다. **수정**: POSIX에서 `listener`의 생성자에
     `std::signal(SIGPIPE, SIG_IGN)` 추가(`src/server/listener.cpp` — Windows는 소켓
     쓰기에 대해 `SIGPIPE`가 없고 대신 `WSAECONNRESET`/`WSAECONNABORTED`를 평범한 에러
     반환값으로 보고합니다). 이후 `write()`가 내는 `EPIPE`는 기존 소켓 에러 경로가 이미
     올바르게 "연결 종료"로 처리하고 있었으므로 다른 코드는 바꿀 필요가 없었습니다. 검증:
     113/113 테스트 통과 유지, 그리고 전에는 5~30초 안에 프로세스를 죽이던 바로 그
     벤치마크가 이제 30초 동안 426,113개 요청을 죽지 않고 메모리도 안정적으로 처리하며,
     직후 `curl`에도 바로 응답합니다.
  - **벤치마크 방법론**: 이 머신의 WSL Ubuntu에 `apt`로 설치한 nginx 1.24와 Apache 2.4.58
    (event MPM, `MaxRequestWorkers`를 150→800으로 상향 — 기본값은 운영 튜닝된 배포보다 한참
    낮은 동시성으로 캡을 걸어버림)이 동일한 결정적 10&nbsp;KB 정적 HTML 파일을 서빙; nhttpd는
    (양쪽 루프백 스택을 다 바인딩하고 CLI로 `blocking_pool_size`를 조절할 수 없는
    `examples/nhttpd`가 아니라) 작은 독립 `bench_server.cpp`로 벤치마크했으며,
    Apache 튜닝과 같은 공정성 이유로 `blocking_pool_size`를 4→64로 올렸습니다.
    `wrk -t8 -c200 -d30s --latency`를 같은 호스트 루프백 구성과 Docker Compose 스택
    (`benchmark/docker/` — 동일한 `cpus`/`mem_limit` 캡을 가진 세 서버 컨테이너가 하나의
    브리지 네트워크 위에, 네 번째 클라이언트 컨테이너가 컨테이너 DNS 이름으로 `wrk`를 실행)
    양쪽에서 실행 — 후자가 존재하는 이유는 같은 커널 안 루프백 벤치마크가 Docker 브리지
    네트워크의 veth-페어-플러스-브리지 경로가 실제로 거치는 진짜 NIC 경로 커널 작업(프레이밍,
    드라이버 큐잉, 체크섬)을 건너뛰기 때문입니다. 전체 수치와 재현 방법은 여기 중복하지 않고
    `ReadMe.md`의 벤치마크 섹션에 있습니다.
  - **결과, 정직하게**: 두 수정을 반영한 뒤에도 nhttpd는 이 특정 마이크로벤치마크(작은 정적
    파일을 반복 서빙)에서 nginx와 Apache에 뒤처집니다 — 루프백에서 13.8K req/s 대
    nginx의 139K, Apache의 38.6K, Docker 벤치마크에서도 상대적 격차는 동일. 이건 실제
    미최적화 아키텍처 격차이지(정적 파일 요청 하나마다 블로킹 스레드 풀을 여러 번 왕복함 —
    stat, open, size, read, close — nginx는 제로카피 `sendfile()` 호출 한 번으로 처리) 버그가
    아니며, 이번 phase에서 다루지 않았습니다. 이 벤치마크 라운드에서 발견한 내용을 바탕으로 곧장
    작성한 우선순위별 격차 해소 계획은 [PLAN.md](PLAN.ko.md) 참고.
- 계획에 남은 것은 QUIC/HTTP-3(보류, 결정 #8 참고), Phase 12가 기록한 Windows에서의 OpenSSL
  빌드 환경 공백, 그리고 Phase 15의 성능 개선 계획([PLAN.md](PLAN.ko.md))뿐입니다. 이 저장소의
  향후 작업은 여기서부터 시작합니다 — 위의 모듈 맵과 빌드 안내, 사용자 대상 API 투어는
  `ReadMe.md`, 성능 관련 다음 단계는 `PLAN.md`를 참고하세요.

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
