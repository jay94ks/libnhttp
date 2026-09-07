# 프로토콜 확장성 — HTTP/2 & QUIC 준비도 검토

**Language**: [English](protocol-extensibility.md) | 한국어

이 문서는 Phase 4~7(서버 코어, 확장, 라우터, WebSocket)을, 재설계 초기에 약속했던 세 가지
아키텍처적 이음매(seam)에 대해 검토한 기록입니다(`CONCEPTS.md`와 `CLAUDE.md`의 architecture
decisions 참고): 지금은 HTTP/2도 QUIC도 구현되어 있지 않지만, 코드베이스는 나중에
**재설계 없이도** 이들을 지원하도록 성장할 수 있어야 합니다. 이 문서에는 아무것도 구현되어
있지 않습니다 — 이건 감사(audit)이고, 그 감사 과정에서 드러난 작은 인터페이스 수정 하나가
전부입니다.

## 이음매 1 — connection과 exchange의 분리

**검증할 주장:** 라우팅, 확장, 핸들러는 절대 "커넥션당 요청 하나"를 가정하면 안 된다 — 그래야
미래의 멀티플렉싱 HTTP/2 드라이버가 하나의 커넥션 위에서 여러 exchange를 동시에 실행할 수
있다.

**결과:** 성립함. `request`/`response`(`include/nhttp/server/request.hpp`, `response.hpp`)는
자신을 만들어낸 `connection`에 대한 참조를 전혀 갖지 않습니다 — 핸들러, `extension`, `router`는
언제나 `request&`만 보고, 커넥션 객체 자체는 절대 보지 않습니다. 커넥션 단위 vs. 요청 단위 태그
구분(`connection::tags()` vs. `request::tags`, `CONCEPTS.md` §1)이 바로 이걸 정직하게
유지해주는 장치입니다: 한 커넥션 위에서 여러 요청에 *걸쳐* 정당하게 유지되어야 하는 것(원래
설계에서의 vhost 스택)은 그것을 위한 명시적인 자리를 갖고, 나머지 전부는 exchange마다
리셋됩니다. `extension_registry::dispatch`, `router::on_handle`, `middleware_stack::handle`
모두 순수하게 `request&`만 다루고 `response`(또는 `task<response>`)를 반환합니다 — 그중 어느
것도 커넥션 내부로 손을 뻗지 않고, 이들의 시그니처 어디에도 "소켓당 정확히 하나씩 존재한다"는
것이 새겨져 있지 않습니다.

*커넥션*이 프로토콜 특화적인 순서 제어를 정당하게 내장하고 있는 유일한 곳은 `server::connection`
자신입니다(`src/server/connection.cpp`): 그 `run()` 루프는, 올바르게도, HTTP/1.1 전용
드라이버입니다(요청 하나를 읽고, 디스패치하고, 응답 하나를 쓰고, keep-alive가 끝날 때까지
반복). 이건 예상된 것입니다 — 미래의 `http2_connection`은 멀티플렉싱된 프레임 레이어를 여러
동시 `request`/`response` 쌍으로 디코딩하고, 각각을 오늘 쓰이는 것과 *같은*
`extension_registry`/`router`/`handler_type` 장치를 통해 디스패치하는 *다른* 커넥션 타입일
것입니다. 이런 드라이버를 추가하는 데 `extension.hpp`, `router/`, 또는 `server/extensions/`의
확장들에는 아무 변경도 필요하지 않을 것입니다.

## 이음매 2 — transport에 무관한 비동기 스트림

**검증할 주장:** 드라이버는 오직 `io::stream` 추상화를 통해서만 네트워크와 대화해야 하고,
raw TCP 소켓을 절대 가정하면 안 된다 — 그래야 미래의 QUIC transport(근본적으로 다름: UDP
기반이고, 단일한 순서 있는 바이트 스트림이 없음)가 위쪽의 아무것도 건드리지 않고 그 아래에
같은 인터페이스를 구현할 수 있다.

**결과: 실제 공백 하나를 발견해 이번 검토 중에 고쳤습니다.** `server::connection`은 원래
자신의 wire를 구체적인 `io::socket_stream` 값 멤버로 갖고 있었고 생성자에서도 값으로
받았습니다 — 정신적으로는 맞았지만(모든 read/write가 `io::stream`의 가상 인터페이스를
거쳤음), *타입* 자체가 불필요하게 TCP 소켓에 구체적으로 결합되어 있었습니다. `io::stream`을
구현하는 가상의 QUIC 스트림 래퍼는 `connection` 자신의 시그니처를 바꾸지 않고서는 결코 그
자리에 대신 들어갈 수 없었을 것입니다. 이번 단계에서 수정함: `connection`은 이제
`std::shared_ptr<io::stream>`을 받아서 저장합니다(`include/nhttp/server/connection.hpp`), 그리고
`listener::handle_connection`(`src/server/listener.cpp`)만이 유일하게 wire가 `socket_stream`
이라는 것을 아는 곳으로 남습니다 — 그곳에서 하나를 생성해 평범한 `io::stream`으로 넘겨줍니다.
WebSocket 업그레이드 핸드오프(`response::upgrade_handler`, `connection::write_response`)는
이미 wire를 `shared_ptr<io::stream>`으로 넘기고 있었으므로, 이번 수정으로 업그레이드가 일어날
때마다 있었던 불필요한 재포장(re-wrap)도 제거됐습니다.

그 외 모든 곳은 변경 없이도 이미 이 이음매를 지키고 있었습니다: `io::range_stream`,
`protocol::chunked_decoder_stream`, `protocol::multipart_reader`, `ws::ws_connection`
모두 `shared_ptr<stream>`/`stream&` 위에서 동작하고, 구체적인 소켓 타입 위에서 동작하지
않습니다.

## 이음매 3 — 와이어에서 디코딩된 헤더 모델

**검증할 주장:** 라우터와 확장들은 헤더를 디코딩된 키/값 쌍으로만 봐야 하고 raw 바이트로는 절대
보면 안 된다 — 그래야 HPACK/QPACK으로 디코딩된 HTTP/2/3 헤더가 그 레이어에서 HTTP/1.1 텍스트
헤더와 구분되지 않는다.

**결과:** 깔끔하게 성립함. `protocol::http_headers`(`include/nhttp/protocol/http_header.hpp`)는
대소문자 구분 없는 조회를 지원하는, `{name, value}` 문자열 쌍의 평범한 순서 있는 컬렉션입니다
— CRLF로 끝나는 텍스트 라인이나 그 밖의 어떤 와이어 세부사항도 여기엔 담겨 있지 않습니다.
`http_header::try_parse`만이 유일하게 HTTP/1.1의 콜론-그리고-CRLF 와이어 형식을 아는 것이고,
오직 `connection::read_headers`에서만 호출됩니다. 모든 확장, `router`, 그리고
`static_content.hpp`의 모든 조건부 GET/Range 검사는 `req.headers.get(...)` /
`.isset(...)`을 통해 헤더를 읽습니다 — HPACK을 같은 `http_headers` 구조로 디코딩하는 가상의
HTTP/2 드라이버(`:method`/`:path` 의사 헤더를 `http_resource`로 매핑하고, 나머지는 그대로
넘기는 식)는 `router/`, `server/extensions/`, `static_content.cpp`를 올바르게 동작시키기 위해
아무것도 바꿀 필요가 없을 것입니다.

## 요약

| 이음매 | 상태 | 취한 조치 |
|---|---|---|
| connection과 exchange의 분리 | 성립함 | 필요 없음 |
| transport에 무관한 스트림 | 공백 발견 | `connection`이 이제 구체적인 `socket_stream`이 아니라 `shared_ptr<io::stream>`을 받음 |
| 와이어에서 디코딩된 헤더 모델 | 성립함 | 필요 없음 |

이 검토의 결과로 HTTP/2나 QUIC 코드가 작성되지는 않았습니다 — 계획에 따르면 이 단계는
감사 전용이고, 감사 과정에서 발견된 작은 인터페이스 수정이 있다면 그것만 적용합니다. 위의
수정 하나(Phase 8)는 행동을 바꾸지 않는 리팩터링입니다: `connection`이 받아들이는 *타입*을
바꿨을 뿐 그걸로 무엇을 하는지는 바꾸지 않았고, 스위트의 기존 테스트 전부(이 검토 시점 기준
96/96)가 변경 없이 통과합니다.
