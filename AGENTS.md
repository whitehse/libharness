# AGENTS.md — libharness

**Project identity**: C and Lua based AI harness library that pulls together sibling components (libpique for PostgreSQL/pg_vector, librest/shaggy for OpenAI-compatible HTTP, Honcho for memory). System-call free and callback free in the core state machine. All network I/O, file operations, and event loops live exclusively in the calling application. Domain center is a durable multi-party **session** (humans, apps, agents as peers; identity-prefixed messages; optional Honcho peer ids — ADR 002). The library exposes Lua interfaces for the identifiable characteristics of a harness: dynamic tool enumeration, dynamic personality/SOUL presentation, OpenAI API compatible context/response processing, configurable looping criteria, deterministic Lua-defined behavior, Lua-written tools using library and sibling APIs, Honcho memory interface, PostgreSQL logging of all model interactions, pg_vector classification for state reduction/token optimization, personality and local memory storage. Supports extensions at interface points in C or Lua. Light resource usage, highly flexible.

**Key commands** (run from repo root):
- `cmake -B build -S . && cmake --build build` — configure and build the static library + tests
- `ctest --test-dir build` — run verification tests
- `cmake --build build --target install` — install (optional)

**Documentation map** (progressive disclosure):
- AGENTS.md (this file) — start here for every task
- ARCHITECTURE.md — module boundaries, invariants, deliberate absences
- TODO.md — living major work items (context builder, Honcho, pique, Lua, tests)
- docs/README.md — full documentation index
- docs/DOMAIN.md — AI harness domain glossary (Session, Model, Context, Response, tools, SOUL, pg_vector, Honcho)
- docs/decisions/ — Architecture Decision Records (ADRs; see especially 002 for multi-party session model)

**Operating rules**:
- Never introduce system calls, callbacks, or hidden I/O inside the library core.
- Lua scripts define deterministic harness behavior and tools; C core provides the plumbing and state machine.
- Every change must keep the library buildable with `-Wall -Wextra -Wpedantic -Werror` (or MSVC equivalent) and pass existing tests.
- Prefer small, reviewable patches. Update relevant docs/ADRs when architecture or domain assumptions change.
- Hermes agent (or any coding agent) must consult AGENTS.md before editing code or docs.
- Local file usage is minimized; PostgreSQL (via libpique) is preferred for persistence, logging, vector storage.
- Network code (OpenAI calls via librest/shaggy) remains in the calling application.

**Definition of done** (for any ticket):
- Code compiles cleanly under strict warnings.
- Tests pass (`ctest`).
- AGENTS.md, ARCHITECTURE.md, and relevant docs remain accurate.
- No new syscalls or callbacks introduced in core.
- State machine and Lua integration remain pure (inputs → state/output only; Lua for policy).
- Harness characteristics (tools, personality, loop criteria) are exercisable via Lua interface.

**Current status**: v0.6.0-todo-impl — pique feed/embed/similarity TSV events,
optional HAVE_PIQUE (pqwire) submit_staged, Lua wait_event/poll_until. See TODO.md.

**Testing, Fuzzing & Valgrind Policy** (see ADR 003):
- Every change to core files must add or update tests in `tests/`.
- Run `ctest` before considering any change complete.
- All tests must pass under Valgrind with no leaks or memory errors.
- Lua integration tests must cover dynamic tool/personality loading and loop criteria.

**Current Interface Direction** (ADR 002):
- Opaque `harness_ctx_t`; `harness_config_t` (event_queue_size, max_participants/messages/tools, workspace_id, session_id, acting_peer_id, pique_ctx, honcho_ctx, …)
- `harness_create` / `harness_create_with_config` / destroy / reset
- Session: `harness_session_set/get`, `harness_set_acting_peer`, participant add/get/count
- Messages: append (+ tool result), identity prefix helper; SOUL set/get; tool register JSON
- Context/response: `harness_context_build`, `harness_response_parse`, status/tool_call_count; feed_input → parse; get_output
- Events: structured `harness_event_t` via `harness_next_event`
- Honcho: attach, mirror_message (narrative only), store/get memory stubs
- Lua: `harness_lua_init(ctx, void* L)` registers policy table when HAVE_LUA
- Roles: MAIN, PROCESSOR, MEMORY
- Domain rules: identity-prefixed session messages; secrets as references for non-privileged peers; tool calls not mirrored to Honcho by default

**Known Limitations / Areas for Improvement**:
- Context/response JSON is hand-escaped substring parse; real JSON library optional
- Multi-participant session plumbing is in-memory; live pique/Honcho I/O is caller-owned
- Lua 5.4 assumed; higher-level coroutine yield helpers optional
- Live embedding scoring / personality search needs linked libpique + pg_vector
- Provider response shape variance (chat completions vs Responses API) still to normalize in processor

When making changes, prefer extending the event-driven path and Lua-exposed harness characteristics.

**ADR 010 Alignment (C Interfaces and Implementations + Language Bindings)**:
- All public interfaces follow opaque type principles from Hanson's *C Interfaces and Implementations*.
- Public headers are designed to be FFI-friendly (simple types, no complex macros or bitfields).
- Consistent naming and clear ownership semantics.
- Lua bindings use standard luaL_newlib style for easy consumption from Lua.
- When adding or modifying public functions, prefer designs that are easy to consume from C and Lua.