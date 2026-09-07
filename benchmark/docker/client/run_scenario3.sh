#!/usr/bin/env bash
# Scenario 3: a dynamic, per-request read-modify-write endpoint (see
# ReadMe.md's Benchmarks section) -- same rationale as run.sh (every request
# crosses the real docker bridge network), but hitting counter.php /
# bench_counter_main.cpp's handler instead of a static file, so every
# request also does a real file read+write and (php-fpm/Apache side)
# process-level lock contention, not just socket I/O.
set -euo pipefail

THREADS="${THREADS:-8}"
CONNECTIONS="${CONNECTIONS:-200}"
DURATION="${DURATION:-30s}"
WARMUP_DURATION="${WARMUP_DURATION:-5s}"

TARGETS=(
  "nginx+php-fpm|http://bench-nginx-php:80/counter.php"
  "apache+mod_php|http://bench-apache-php:80/counter.php"
  "nhttp|http://bench-nhttp-counter:8080/"
)

for target in "${TARGETS[@]}"; do
  name="${target%%|*}"
  url="${target##*|}"

  echo "waiting for ${name} (${url}) ..."
  for _ in $(seq 1 30); do
    if curl -fsS -o /dev/null "${url}"; then
      break
    fi
    sleep 1
  done
done

for target in "${TARGETS[@]}"; do
  name="${target%%|*}"
  url="${target##*|}"

  echo ""
  echo "=== warmup: ${name} ==="
  wrk -t2 -c50 -d"${WARMUP_DURATION}" "${url}" > /dev/null

  echo "=== ${name} (${THREADS}t/${CONNECTIONS}c/${DURATION}) ==="
  wrk -t"${THREADS}" -c"${CONNECTIONS}" -d"${DURATION}" --latency "${url}"
done
