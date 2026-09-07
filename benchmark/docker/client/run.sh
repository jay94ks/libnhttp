#!/usr/bin/env bash
# Drives wrk against each benchmark target *by its container DNS name*, so
# every request crosses the docker bridge network's veth pair + Linux bridge
# — the same kernel code paths a real NIC deployment exercises — instead of
# the loopback shortcut a same-host/same-kernel benchmark takes. See
# ReadMe.md's "Docker network-stack benchmark" section for why this matters.
set -euo pipefail

THREADS="${THREADS:-8}"
CONNECTIONS="${CONNECTIONS:-200}"
DURATION="${DURATION:-30s}"
WARMUP_DURATION="${WARMUP_DURATION:-5s}"

TARGETS=(
  "nginx|http://bench-nginx:80/"
  "apache2|http://bench-apache:80/"
  "node|http://bench-node:8080/"
  "nhttp|http://bench-nhttp:8080/"
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
