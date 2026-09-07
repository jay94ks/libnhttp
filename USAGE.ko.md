# libnhttp — 사용 패턴 (원래 구현 기준)

**Language**: [English](USAGE.md) | 한국어

이 문서는 원래 libnhttp의 **관찰 가능한 사용 표면(usage surface)** — 라이브러리를 실제로
사용하는 코드가 어떤 모양이었는지 — 를 `ReadMe.md`, `nhttpd/main.cpp`, `libnhttp-tests/main.cpp`에서
추출해 정리한 것입니다. 재설계된 새 API를 "예전과 똑같은 시그니처를 그대로 따르지 않으면서도,
호출자가 같은 의도를 여전히 표현할 수 있는가"라는 기준으로 판단할 수 있도록 존재합니다.

## 1. 서버 부트스트래핑

```cpp
socket_watcher watcher(1024);                       // 리액터, 최대 fd 개수로 크기 지정
http_listener listener(watcher, http_params());      // 리스너 하나, 리액터를 공유하거나 단독 소유

listener.with(ipv4::resolve("127.0.0.1", 8080));      // 주소 체계별로 bind + listen
listener.with(ipv6::resolve("::1", 8080));

listener.run();                                       // 현재 쓰레드를 이벤트 루프로 블로킹
```

다른 곳에서 쓰인 변형들:
- `run(lambda)` — 호출자가 co-loop 람다를 제공하며, 유휴 틱마다 한 번, 준비된 이벤트마다 한 번
  호출됨. `false`를 반환하면 루프가 멈춤 (테스트에서 atomic 플래그 / `/exit` 엔드포인트를 통한
  정상 종료에 사용).
- 로우 `socket_watcher` / `listener` 자체에 대한 range-for로 "메인 쓰레드를 준비된 컨텍스트를
  꺼내오는 N개 워커 쓰레드 중 하나로 사용" (`for (http_context_ptr ctx : listener)`).
- 여러 리스너가 하나의 `socket_watcher`를 공유(co-hosted)하거나, 각자 하나씩 소유(self-hosted)할
  수 있음.

**보존해야 할 특성:** 메인 쓰레드가 `run()`에서 완전히 블로킹될지, 커스텀 루프 바디를 직접
구동할지, 아니면 준비된 작업을 꺼내가는 여러 소비자 중 하나일 뿐인지는 호출자가 결정한다 —
라이브러리는 자신이 쓰레드를 소유한다고 가정하지 않는다.

## 2. 요청에 응답하기

```cpp
context->response = make_response("hello world");                         // 200, text/html
context->response = make_response(404);                                    // 상태 코드만
context->response = make_response("{...}", http_mime_type::APPLICATION_JSON);
context->close();                                                           // 마무리/flush
```

핸들러는 본문을 미리 만들어진 버퍼가 아니라 스트림으로 읽는다:
```cpp
std::string body;
if (!req->get_request_body()->read_all(body))
    return make_response(400);
```

**보존해야 할 특성:** 문자열로부터, 상태 코드만으로, 문자열+mime 쌍으로, 혹은 (다른 곳에서는)
stream/file/byte-range로부터 응답을 만드는 것 모두 똑같이 간단한 한 줄이어야 한다 — 이건 어떤
핸들러에서든 가장 자주 호출되는 부분이므로 반드시 간결해야 한다.

## 3. 정적 파일 / 디렉터리 서빙

```cpp
listener.extends(overlay_of(".", "index.html"));                 // cwd 서빙, 기본 index
example_com->extends(overlay_of("./example.com", "index.html")); // vhost별 overlay
```

**보존해야 할 특성:** 조건부 GET(ETag/If-None-Match/Last-Modified)과 byte-range 요청
(`Range:` 헤더, `206 Partial Content`)은 이 방식으로 서빙되는 모든 것에 대해 핸들러 쪽 코드 없이
자동으로 동작해야 한다 — Makefile의 수동 curl 기반 스모크 테스트에서 검증됨
(`Range: bytes=0-5`, `If-None-Match`, `If-Modified-Since`를 IPv4/IPv6 리스너 양쪽에 대해 실행).

## 4. 가상 호스팅

```cpp
auto example_com = vhost_for("www.example.com");   // 정확한 호스트명
auto by_regex     = vhost_for(std::regex(".*"));    // 패턴 기반

listener.extends(example_com);
example_com->extends(some_router_or_overlay);       // 확장은 vhost 아래에 중첩됨
```

**보존해야 할 특성:** vhost는 호스트명 기준으로 "내 자식들이 어떤 요청을 보는지"를 좁혀주는
또 하나의 확장일 뿐이다; 리스너에 설치 가능한 것은 무엇이든 vhost 안에도 설치할 수 있어야 한다.

## 5. REST 라우팅 (xfwk 라우터)

```cpp
auto router = std::make_shared<xfwk_router>();
listener.extends(router);

router
    ->get("whoami", target_by([](http_request_ptr) {
        return make_response("I'm jay.");
    }))
    ->get("/:user", target_by([](http_request_ptr req) {
        std::string user = route_of(req).captures[":user"];
        return make_response(user + " is ...");
    }))
    ->post("/:user", target_by([](http_request_ptr req) {
        std::string user = route_of(req).captures[":user"];
        std::string body;
        if (!req->get_request_body()->read_all(body))
            return make_response(400);
        return make_response(user + " says " + body);
    }));

// 경로 파라미터 제약:
router->param("/:user", [](const std::string& value) {
    return value == "jay" || value == "kay";
});

// catch-all 폴백:
router->any("/:any", [](http_request_ptr) {
    return make_response("not allowed");
});
```

그룹핑 + 미들웨어, 그리고 하나의 경로에 대한 메서드 체이닝 (`libnhttp-tests/main.cpp`에서):
```cpp
router->group([](xfwk_facade_ptr inner) {
    inner->get(":user/profile", handler_a)
         ->get(":user/greetings", handler_b)
         ->post(":user/set", handler_c);

    inner->put(":user/set", handler_d);          // 같은 경로, 다른 메서드
    inner->delet(":user", handler_e);
    inner->param(":user", [](const std::string& name) {
        return name == "jay" || name == "kay";
    });
});
```

메서드를 타겟으로 사용하기 (람다 대신 멤버 함수를 바인딩):
```cpp
class my_controller {
public:
    http_response_ptr hello(http_request_ptr p) { return make_response("hello world!"); }
};

auto my_ctrl = std::make_shared<my_controller>();
router->get(target_by(my_ctrl, &my_controller::hello));
```

**보존해야 할 특성:** 체이닝된 등록 호출이 계속 체이닝 가능한 무언가를 반환할 것; 캡처된 라우트
파라미터를 핸들러 내부에서 request 객체를 통해 가져올 수 있을 것; 파라미터별 검증 predicate;
여러 라우트 등록을 묶어서 나중에 공유 미들웨어를 붙일 수 있는 그룹핑; 자유 람다와 기존 객체의
멤버 함수를 동일한 호출부 어법으로 타겟으로 바인딩할 수 있을 것.

## 6. 수동 프로토콜 레벨 스모크 테스트 매트릭스 (`libnhttp/Makefile`의 `run-test-app`에서)

이것은 원래 프로젝트가 가지고 있던 통합 테스트에 가장 가까운 것이며, 서버가 올바르게 처리해야
하는 요청 형태들을 나열합니다 — 재설계판의 Catch2 통합 테스트를 위한 체크리스트로 유용하며,
`127.0.0.1`과 `::1` 양쪽에 대해 실행됩니다:

- 반복되거나 특이한 구분자를 포함한 쿼리 스트링이 있는 평범한 `GET /`
  (`?dummy=query-string&a=1,b=c&d=3`).
- `Range: bytes=0-5`(부분 콘텐츠)와 `Range: bytes=0-`(오프셋부터)가 있는 `GET /`.
- `If-None-Match: "<etag>"`가 있는 `GET /` (매치 시 304 예상).
- `If-Modified-Since: <date>`가 있는 `GET /` (not-modified 시 304 예상).
- `GET /whoami`, `GET /always-501` (명시적으로 처리되지 않는 라우트 케이스, 501 예상).
- 라우팅된 경로(`/jay/set`)와 always-501 경로에 urlencoded 본문을 담은 `POST`, `PUT`
  (라우팅/우선순위가 실수로 이걸 처리해버리지 않는지 검증).
- GET/POST/PUT/DELETE만 정의된 경로에 `PATCH` (405 Method Not Allowed 예상).
- 본문이 **없는** `DELETE`(성공/처리됨 예상) vs 본문이 **있는** `DELETE`(400 예상 — DELETE는
  요청 본문을 가질 수 있게 정의되어 있지 않음).
- **라우팅되지 않은** 경로에 `Transfer-Encoding: chunked`가 있는 `POST` (청크 본문이 올바르게
  디코딩되어야 함; 어떤 핸들러도 소비하지 않더라도 청크 본문이 여전히 올바르게 소진/폐기되는지
  검증).
- 큰 바이너리 본문(`Content-Type: application/octet-stream`, `index.html` 크기 정도의
  페이로드)의 `POST`/`PUT` — 고정 길이 콘텐츠 핸들러를 라운드트립.
- 정적 overlay를 통해 큰 파일(~100MB, `dd`/`wget`)을 업로드한 뒤 다운로드 — 작은 인메모리
  본문뿐 아니라 스트리밍 경로 전체를 end-to-end로 검증.
- 위의 모든 케이스를 IPv6 리스너(`[::1]:8080`)에 대해서도 동일하게 반복 — 듀얼스택 동작은
  나중에 덧붙이는 게 아니라 최우선 요구 사항.
- 테스트 서버 프로세스를 깔끔하게 종료하기 위한 전용 `/exit` POST 엔드포인트.

## 7. WebSocket 핸드셰이크 (원래 구현에서는 동작했으나 프레임 입출력은 아니었음 — CONCEPTS.md §6 참고)

```cpp
listener.extends(websock_ep_for("/ws", [](std::shared_ptr<http_websocket> ws) {
    // on_connect: false를 반환하면 업그레이드를 거부함.
    return true;
}));
```
핸드셰이크(키 검증, `Sec-WebSocket-Accept` 계산, `101` 응답)는 동작했지만, 업그레이드 이후
프레임을 주고받는 것은 동작하지 않았습니다. 재설계판이 실제 프레임 입출력을 구현할지는 열린
결정 사항이었고(CONCEPTS.md §6 참고), 그대로 베껴올 대상이 아니었습니다.

## 8. 커넥션/요청 단위로 걸쳐지는 상태를 위한 태그

```cpp
struct http_vhost_tag { std::stack<http_vhost*> vhosts; };

// 재사용 가능한 어딘가에서, 커넥션에 붙임 (keep-alive 요청들 사이에도 유지됨):
http_vhost_tag* tag = context->link->ensured_tag<http_vhost_tag>();
tag->vhosts.push(this);
```
**보존해야 할 특성:** 어떤 확장이든 공유 컨텍스트 클래스를 수정하지 않고도, 그리고 다른 모든
확장이 서로의 상태를 알아야 할 필요 없이, 커넥션이나 요청에 자신만의 강타입 상태 조각을 붙일 수
있어야 한다.
