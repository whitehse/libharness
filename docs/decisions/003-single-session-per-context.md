# ADR 003: Single Session per harness_ctx_t

**Date**: 2026-07-09
**Status**: Accepted
**Deciders**: Project maintainers

## Context

ADR 002 defines durable multi-party sessions. Implementers asked whether one
`harness_ctx_t` should multiplex many sessions or bind to one session at a time.

## Decision

Each `harness_ctx_t` holds **exactly one** active `(workspace_id, session_id)`
pair (plus participants/messages for that session).

- Switch sessions with `harness_session_set` (clears retired flag).
- Applications that need concurrent sessions create multiple contexts.
- Session identity may be persisted via `harness_pique_build_session_upsert`
  for caller-fed libpique; the library does not open PG connections.

## Consequences

- Simpler ownership and testing (one message list, one acting peer).
- Dialectic multi-agent examples use two contexts (already the pattern).
- A future multi-session handle would require a new ADR if needed.
