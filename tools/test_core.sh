#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -Ilib/AlarmCore tests/core_tests.cpp -o "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary"
