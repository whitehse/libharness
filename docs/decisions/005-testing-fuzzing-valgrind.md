# ADR 005: Testing, Fuzzing, and Valgrind (Harness Adaptation)

**Date**: 2026-07-10
**Status**: Accepted
**Deciders**: Project maintainers

## Context

Sibling protocol libraries mandate ctest, fuzzing, and Valgrind for core
parser changes (shaggy ADR 003). libharness is a pure state machine that
parses untrusted provider JSON and emits events; it does not handle sockets
but still treats model/Honcho/TSV text as untrusted buffers.

## Decision

All changes that touch core library sources (`src/*.c`, `include/harness.h`)
**must** satisfy:

1. **ctest** — existing tests remain green; new behaviour adds/extends
   `tests/` (smoke, dialectic session/Honcho, history/stream, policy/pique,
   response normalize, …).
2. **Fuzz surface** — `tests/fuzz_harness.c` (libFuzzer target when
   `ENABLE_FUZZ`) exercises `harness_response_parse` and related feed paths.
   Non-trivial parser/context-builder changes should run a short fuzz campaign
   before merge when the harness is available.
3. **Valgrind** — `scripts/run_valgrind.sh` (or equivalent) on test binaries
   records no new leaks or invalid access. Absent valgrind is a soft skip with
   explicit log, not silent success.

Dialectic (buffer-only) tests are the preferred completeness tool for
multi-party and sibling-transport seams (see ADR 006).

## Consequences

- Definition of Done in AGENTS.md references this policy.
- CI and agents treat missing tests for new API surfaces as incomplete work.
- Live-network tests are intentionally out of scope for the default suite.

## Verification

- `ctest --test-dir build`
- optional: `scripts/run_valgrind.sh`
- optional: build with `-DENABLE_FUZZ=ON` and a bounded `fuzz_harness` run
