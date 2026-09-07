# 성능 개선 계획

**언어**: [English](PLAN.md) | 한국어

이 저장소의 성능 관련 후속 작업을 위한 살아있는 할 일 목록입니다 — **아직 남아 있는 항목만**
담습니다. 완료된 작업은 끝나는 대로 여기서 지워집니다; 무엇을 시도했고, 무엇을 측정했고,
무엇을 왜 반려했는지에 대한 전체 이야기는 `CLAUDE.md`의 phase 로그에 영구적으로 남아 있고,
핵심 수치는 `ReadMe.md`의 벤치마크 섹션에 있습니다. 이력이 궁금하면 그것부터 확인하세요 —
이 파일은 항상 "지금 남은 것"만 담습니다.

## 남은 작업

- **Windows: 정적 파일 응답을 위한 `sendfile(2)` 대응물.** Linux 고속 경로(`CLAUDE.md`의
  Phase 16 로그 참고)는 아직 Windows 대응물이 없습니다 — `platform::socket_handle::
  supports_send_file()`는 거기서 `false`를 반환해서, 모든 응답이 일반 read/write 경로로
  폴백합니다. `TransmitFile`이 자연스러운 선택이지만, 이 리액터의 평범한 읽기/쓰기가 지금
  쓰지 않는 진짜 오버랩드 I/O 완료 처리가 필요합니다; 대충 얹는 게 아니라 그게 설계되고
  검증 가능해진 뒤에 하세요.
- **실제로 `perf`를 돌릴 수 있는 호스트가 이제 생겼으니, 그걸로 하는 진짜 프로파일링.**
  Phase 16의 걸림돌(이 WSL2 커널에 맞는 `linux-tools` 패키지가 없던 것)은 더 이상 유효하지
  않습니다 — `perf stat`과 `perf record -g` 둘 다 여기서 잘 동작합니다(유저스페이스 심볼은
  잘 풀리고, 커널 심볼은 여전히 안 풀리지만 이 코드베이스 자체를 프로파일링하는 데는 상관
  없습니다). Phase 17에서 이걸로 아래 router 항목을 해결했지만, Phase 16이 A/B 벤치마크로만
  해결할 수밖에 없었던 두 가지 질문은 아직 실제 프로파일로 다시 살펴볼 가치가 남아 있습니다:
  코루틴 프레임 할당 처리량(P3, 측정된 개선이 없어서 반려됨)과 "워커를 과다 프로비저닝하지
  말 것" 이상의 더 세밀한 `thread_pool` 튜닝(P4) — 둘 다 `CLAUDE.md`의 Phase 16 로그에
  있습니다.
- **`route::method_targets_`의 문자열 키 조회.** 같은 항목의 더 영향이 큰 두 부분(`route_state`의
  캡처 맵과 후보별 predicate 할당)은 Phase 17에서 고쳤습니다(`CLAUDE.md`의 phase 로그 참고) —
  `benchmark/router/bench_router_main.cpp`를 `perf`로 프로파일링해서 둘 다 실제 핫스팟임을
  확인한 뒤였습니다. 늘 우선순위가 낮았던 이 세 번째 부분만 아직 남아 있습니다:
  `route::method_targets_`는 메서드 *이름 문자열*로 키를 잡은 `std::map<std::string,
  target_ptr>`이고, 매치된 요청당 `get_target()`으로 한 번만 조회됩니다 — 백트래킹 중 방문하는
  트리 노드마다 치르는 다른 둘보다는 저렴하지만(요청당 O(1)), 그래도 `protocol::http_method`가
  오늘 시점에 더 저렴한 식별자를 갖고 있지 않은 채 문자열 비교 트리 조회를 하는 셈입니다. 언젠가
  이게 해볼 가치가 있다고 측정되면 `protocol::http_method`에 작은 enum/id를 추가할 가치가
  있습니다 — 이제 그걸 A/B할 기준선으로 `bench_router_main.cpp`가 존재합니다.

## 이 계획에서 명시적으로 제외되는 것

- 이미 별도로 추적 중인 것들: QUIC/HTTP-3(`CLAUDE.md`의 아키텍처 결정 8번 참고),
  TLS 위 ALPN 협상 HTTP/2, HTTP/2 `CONTINUATION`/서버 푸시(`CLAUDE.md`의 Phase 14 로그 참고).
- 리버스 프록시 커넥션 풀링과 능동적 업스트림 헬스체크(`CLAUDE.md`의 Phase 13 로그가 이미 이번
  라운드 범위 밖의 알려진, 합리적인 후속 작업으로 기록해둠).
- `io_context`/`async_socket`을 현재의 이식 가능한 준비성(readiness) 기반 계약 대신 네이티브
  완료 모델 중심으로 다시 짜는 것(`CLAUDE.md`의 Phase 12 설계 노트가 Windows/IOCP에서도 왜
  일부러 이걸 피했는지 설명함) — 여기 어떤 항목도 그걸 재검토할 필요가 없습니다.
