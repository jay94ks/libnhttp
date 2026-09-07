# build context is the repo root (see ../docker-compose.yml), same as
# ../nhttp/Dockerfile -- see bench_counter_main.cpp's doc comment for why
# this uses its own minimal main() instead of examples/nhttpd.
FROM ubuntu:24.04 AS build

RUN apt-get update -qq \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
       build-essential cmake ninja-build libssl-dev git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# see benchmark/docker/nhttp/Dockerfile for why -Wno-error on these two.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DNHTTP_ENABLE_TLS=ON -DNHTTP_BUILD_EXAMPLES=OFF \
      -DCMAKE_CXX_FLAGS='-Wno-error=maybe-uninitialized -Wno-error=null-dereference' \
    && cmake --build build --target nhttp -j"$(nproc)"

RUN c++ -DNHTTP_HAVE_TLS -Iinclude -O3 -DNDEBUG -std=c++20 \
      benchmark/docker/nhttp/bench_counter_main.cpp -o /tmp/nhttp_counter_bench_server \
      build/src/libnhttp.a -lssl -lcrypto -lpthread

FROM ubuntu:24.04

RUN apt-get update -qq \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /tmp/nhttp_counter_bench_server /usr/local/bin/nhttp_counter_bench_server

EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/nhttp_counter_bench_server", "8080", "/tmp/nhttp_counter.txt"]
