# build context is the repo root (see ../docker-compose.yml) so the build
# stage can see the actual library source, not just the benchmark/ subtree.
FROM ubuntu:24.04 AS build

RUN apt-get update -qq \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
       build-essential cmake ninja-build libssl-dev git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# builds the library only (not the nhttpd example — see bench_main.cpp's doc
# comment for why the benchmark uses its own minimal main() instead).
# -Wno-error on the two GCC -O3 false positives documented in ReadMe.md's
# Benchmarks section (maybe-uninitialized / null-dereference on coroutine
# frames wrapped in std::optional<std::function<...>> — a known GCC
# optimizer quirk with this coroutine pattern, not a real bug).
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DNHTTP_ENABLE_TLS=ON -DNHTTP_BUILD_EXAMPLES=OFF \
      -DCMAKE_CXX_FLAGS='-Wno-error=maybe-uninitialized -Wno-error=null-dereference' \
    && cmake --build build --target nhttp -j"$(nproc)"

RUN c++ -DNHTTP_HAVE_TLS -Iinclude -O3 -DNDEBUG -std=c++20 \
      benchmark/docker/nhttp/bench_main.cpp -o /tmp/nhttp_bench_server \
      build/src/libnhttp.a -lssl -lcrypto -lpthread

FROM ubuntu:24.04

RUN apt-get update -qq \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /tmp/nhttp_bench_server /usr/local/bin/nhttp_bench_server
COPY benchmark/docker/content/ /srv/www/

EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/nhttp_bench_server", "/srv/www", "8080"]
