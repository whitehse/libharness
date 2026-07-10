#!/usr/bin/env bash
# Optional Valgrind pass over unit tests (ADR 003 testing policy).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if ! command -v valgrind >/dev/null 2>&1; then
  echo "valgrind not installed; skipping"
  exit 0
fi
cmake -B build -S . >/dev/null
cmake --build build -j"$(nproc)" >/dev/null
for t in harness_smoke_test test_dialectic_session test_history_stream test_policy_pique; do
  exe="build/tests/$t"
  if [[ -x "$exe" ]]; then
    echo "== valgrind $t =="
    valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite \
      --errors-for-leak-kinds=definite "$exe"
  fi
done
echo "VALGRIND_OK"
