# ADR 004: libharness as Pure Plumbing (Sibling Common Alignment)

**Date**: 2026-07-10
**Status**: Accepted
**Deciders**: Project maintainers

## Context

Sibling protocol libraries (shaggy/libhttp2 lineage, librest, pique/libpqwire)
adopt a **plumbing** posture: the core transforms bytes ↔ structured data and
emits events; it never owns sockets, automatic server replies, or hidden I/O
(see shaggy ADR 006 for the HTTP family statement).

libharness is not a wire-protocol PDU parser, but it sits as a **stack layer**
over OpenAI-compatible JSON, Honcho-shaped JSON, and PostgreSQL SQL text. Early
stubs risked reintroducing “active” behaviour (implicit network helpers,
log-only fake PEF paths without explicit stage events).

## Decision

libharness core (`src/*.c` excluding application examples/tests) shall remain
**plumbing relative to AI harness domain objects**:

1. **No syscalls / sockets / files** in core. Callers own HTTP (librest/shaggy),
   PQ wire (libpqwire), and Honcho POSTs.
2. **Event-driven surface**: feed / get_output / next_event; structured
   `harness_event_t` (with optional `detail[]`).
3. **Builders vs. actors**: SQL/JSON builders and `*_feed_*` paths stage buffers
   and emit readiness events; they do not commit wallets, round-trip networks,
   or execute tools.
4. **Lua is policy**: loop criteria, tool bodies, mirror overrides live in Lua
   (or C registration hooks) without the C core becoming an interpreter I/O edge.
5. **Optional links are transport adapters only**: `HAVE_PIQUE` may call
   `pqwire_send_query` on a caller-provided handle (buffer stage on the pqwire
   context). It does not create sockets or block on PG.
6. **Vector / DATA_ROW decoding stops at text**: apps unpack wire rows to
   `score\\ttext` or `score|id|text`; core parses and emits `VECTOR_HIT`.

Domain multi-party session semantics remain ADR 002; one session per ctx remains
ADR 003.

## Consequences

- New features that need network or file system stay in examples, tests, or
  calling applications.
- Dialectic tests (session, Honcho buffer, history export, pique SQL stage)
  prove progress without services.
- Full adoption of later shaggy ADRs (HTTP/1.1, MITM, etc.) only when libharness
  gains corresponding wire modules — not as blank copies.

## Verification

- Core sources contain no intentional socket/file APIs.
- ctest suite remains offline.
- CONTRIBUTING surface (AGENTS/ARCHITECTURE) restates this decision.
