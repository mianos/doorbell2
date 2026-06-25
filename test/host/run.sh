#!/usr/bin/env bash
# Build and run the host-side unit tests (no hardware / ESP-IDF required).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$(mktemp -d)"

g++ -std=c++17 -Wall -Wextra \
    -I"$ROOT/components/radar/include" \
    "$HERE/test_ld2450.cpp" \
    "$ROOT/components/radar/src/Ld2450Decoder.cpp" \
    -o "$OUT/test_ld2450"

"$OUT/test_ld2450"
