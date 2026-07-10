# ADR 006: Dialectic Multi-Context Testing (Harness Adaptation)

**Date**: 2026-07-10
**Status**: Accepted
**Deciders**: Project maintainers

## Context

Shaggy ADR 004 requires client↔server dialectic testing for wire protocols.
libharness is not a dual-role wire endpoint, but multi-party AI sessions and
caller-owned transports form natural **paired contexts**: agent vs human view,
builder vs parser, MAIN vs MEMORY roles, two harness instances around staged
JSON/SQL without sockets.

## Decision

Features that introduce multi-party, transport-staging, or parse symmetry **must**
be covered by dialectic-style tests that:

1. Instantiate **two or more** `harness_ctx_t` (or export→import buffers between
   them).
2. Exchange **only in-memory buffers** (`get_output` → `feed_input` /
   `response_parse` / `honcho_parse_response` / SQL stage assertions).
3. Assert both builder and consumer paths (identity prefix secrets, tool
   rounds, Honcho narrative vs tool-call policy, pique SQL shape, etc.).

Current anchors: `tests/test_dialectic_session.c`,
`tests/test_dialectic_honcho.c`, and examples such as `dialectic_agents`.

## Consequences

- New transport staging APIs prefer a paired test snip over mock sockets.
- Role enum (`MAIN` / `PROCESSOR` / `MEMORY`) should be exercised where it
  changes behaviour.
- Remains compatible with ADR 004 (core remains socket-free).

## Verification

ctest includes dialectic_* tests that pass offline.
