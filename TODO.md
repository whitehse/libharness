# libharness — Implementation TODO

Current state (after ADR 002 interface alignment): bootstrap static library with
session/participant/message/SOUL/tool registration, a **stub context builder**
(identity prefix + secret redaction + tools JSON), lightweight response status
detection, structured `harness_event_t` queue, Honcho mirror hook (no transport),
Lua bind table when HAVE_LUA, smoke test + example. Builds under
`-Wall -Wextra -Wpedantic -Werror`. Network, PG wire, and Honcho HTTP remain
caller-owned (ADR 006).

This document tracks remaining work. Domain vocabulary is fixed in
**ADR 002** (`docs/decisions/002-session-model-context-and-honcho.md`) and
`docs/DOMAIN.md`. Update this file when major items complete (sibling ADR 013).

---

## High Priority: Context builder & response processor (ADR 002)

### 1.1  Full OpenAI-compatible context builder
Stub emits minimal JSON (`model`, `temperature`, `messages`, optional `tools`)
with identity prefixes and secret references. Remaining:
  - Correct multi-part content arrays (text / image_url / refusal, etc.)
  - Assistant messages carrying `tool_calls[]` (id, type, function.name, arguments string)
  - `tool` role messages with `tool_call_id` fully round-tripped in build+parse
  - `developer` role alias handling vs provider quirks
  - Configurable max context bytes / truncation policy (Lua or config)
  - Deterministic message ordering guarantees documented + tested
  - Integration with libjsparse (or sibling JSON) instead of hand-rolled escape

Files: `src/openai_processor.c`, `include/harness.h`, tests

### 1.2  Full response parser / normalizer
Current parse is substring heuristics (`requires_action`, `tool_calls`, `call_`).
Remaining:
  - Normalize Chat Completions vs Responses API vs Assistants-style payloads
  - Populate structured tool-call list (id, name, arguments) accessible via C/Lua API
  - Emit usage (`prompt_tokens`, `completion_tokens`, `total_tokens`) on events or getters
  - Streaming chunk assembly path (caller feeds chunks; library aggregates) — optional
  - Error / incomplete status events

Files: `src/openai_processor.c`, `include/harness.h`, tests

### 1.3  Tool registry completeness
  - Register tools from C structs (not only raw JSON strings)
  - Support `function`, and opaque pass-through for `code_interpreter`, `file_search`, `web_search`
  - Namespaced tool names (`python_interpreter:run_code`)
  - Lua `register_tool(name, schema, fn)` executing in policy layer (no I/O in core)
  - Enumerate tools as JSON array helper for callers

Files: `src/harness.c`, `src/lua_bindings.c`, `include/harness.h`

### 1.4  Secret / privilege redaction
  - Stable secret reference ids across rebuilds of the same message
  - Privilege matrix beyond single `privileged` bool (roles / capabilities)
  - Policy hooks in Lua for custom redaction
  - Ensure secrets never appear in Honcho mirror payloads or default PG log fields without explicit opt-in

Files: `src/openai_processor.c`, `src/lua_bindings.c`, `src/honcho_interface.c`

---

## High Priority: Session & participant plumbing

### 2.1  Durable session identity
  - Persist session/workspace ids via pique when available
  - Session “lives forever” retirement API (`harness_session_retire`) for app policy
  - Multi-session handle model vs single session per `harness_ctx_t` (decide + ADR if multi)

Files: `src/harness.c`, `src/pique_integration.c`

### 2.2  Participant lifecycle
  - Remove / mute / privilege-change APIs
  - Kind-specific defaults (agent vs human SOUL attachment)
  - Map participants 1:1 with Honcho peer ids in integration tests

Files: `include/harness.h`, `src/harness.c`

### 2.3  Message history management
  - Ring buffer / max_messages eviction policy for token control
  - Import/export history as JSON for dialectic tests
  - Vector-assisted compression hooks (`harness_classify_vector` → drop/summarize)

Files: `src/harness.c`, `src/pique_integration.c`

---

## High Priority: Honcho join (optional provider)

### 3.1  Honcho client plumbing (caller-fed)
  - Buffer-level request builder for peer messages (workspace/session/peer_id/content/metadata)
  - Parse Honcho responses into events (no sockets in core)
  - Document curl/HTTP examples only in docs/examples (not core)

Files: `src/honcho_interface.c`, `include/harness.h`, examples

### 3.2  Mirror policy
  - Default: **do not** mirror tool calls (enforced with type/role checks)
  - Mirror user/assistant narrative with peer_id
  - Metadata conventions (`chat_message`, `agent_response`, model name)
  - Lua policy: `should_mirror(message) → bool`

Files: `src/honcho_interface.c`, `src/lua_bindings.c`

### 3.3  Memory facts API
  - Replace stub `harness_honcho_get_memory` with real peer-card / conclusion surfaces
  - Align naming with Honcho peer/session/message model (ADR 002 table)

Files: `src/honcho_interface.c`

---

## Medium Priority: PostgreSQL / pg_vector (libpique)

### 4.1  Interaction logging
  - INSERT path via pique event/feed API for every model interaction
  - Store model, context hash/blob, response, usage, session_id, acting_peer_id, timestamps
  - Dialectic test with fake pique buffers if possible

Files: `src/pique_integration.c`, tests

### 4.2  Vector classification & personality storage
  - Embeddings storage + similarity search for SOUL/personality
  - Token-reduction classifier for history compression
  - Graceful no-op when libpique not linked (current warning path)

Files: `src/pique_integration.c`

---

## Medium Priority: Lua policy surface

### 5.1  Complete `harness` Lua table
  - Bind all ADR 002 C APIs (session, tools, response parse, log, classify)
  - Coroutine-friendly yield points at event boundaries (sibling ADR 014 style)
  - Load scripts from memory/PG rather than `luaL_dofile` path default

Files: `src/lua_bindings.c`

### 5.2  Loop criteria
  - Real evaluation of Lua expressions / registered criteria functions
  - Events carry loop decision reason codes

Files: `src/harness.c`, `src/lua_bindings.c`

---

## Medium Priority: Events, config, interfaces (ADR 010)

### 6.1  Event queue robustness
  - Backpressure policy (block vs drop vs compact) configurable
  - Richer event payloads without breaking FFI (side tables / getters by index)
  - `harness_event_type_name()` for debugging

Files: `include/harness.h`, `src/harness.c`

### 6.2  Expand `harness_config_t`
  - caps already present (queue, participants, messages, tools)
  - flags: `no_identity_prefix_default`, `mirror_tool_calls` (default false), `strict_json`
  - optional fixed output buffer supplied by caller (zero library realloc)

Files: `include/harness.h`, `src/harness.c`

### 6.3  Remove remaining bootstrap coupling
  - Deprecate timeline for `harness_lua_*` compatibility wrappers
  - Ensure all public headers remain FFI-friendly (no bitfields/macros required)

Files: `include/harness.h`, docs

---

## Lower Priority: Testing, fuzzing, examples, docs

### 7.1  Dialectic-style tests (ADR 003/004 spirit)
  - Multi-participant context build golden JSON tests
  - Secret redaction matrix (privileged vs not)
  - requires_action → tool result → completed loop without network
  - Honcho mirror exclusion of tool payloads

Files: `tests/`

### 7.2  Fuzz targets
  - `fuzz_harness` for response_parse and context message content
  - Enable under `ENABLE_FUZZ`

Files: `tests/fuzz_harness.c`, CMake

### 7.3  Valgrind clean
  - Ensure create/destroy and long message sequences show no leaks

### 7.4  Examples
  - Multi-agent dialectic example (two harness ctx exchanging narrative)
  - Example wiring notes for librest/shaggy transport (docs only)

Files: `examples/`

### 7.5  Documentation
  - Manpages if/when project adopts sibling manpage policy (ADR 007)
  - Keep AGENTS.md / ARCHITECTURE.md / DOMAIN.md in sync when APIs land
  - Consider adopting full common ADR set (002 event-loop, 003 testing, …) from shaggy

Files: `docs/`, `AGENTS.md`, `ARCHITECTURE.md`

---

## Interface change log (ADR 002 alignment — done in tree)

Public surface now includes (stubs unless noted):
- [x] `harness_config_t` with workspace/session/acting_peer + caps
- [x] Structured `harness_event_t` + expanded event types
- [x] Session set/get, acting peer, participant add/get/count
- [x] Message append (+ tool result), message count, identity prefix helper
- [x] SOUL set/get, tool register JSON / clear / count / enumerate
- [x] `harness_context_build` + `harness_context_params_t`
- [x] `harness_response_parse` / status / tool_call_count; `feed_input` → parse
- [x] Honcho attach / mirror_message / store+get memory stubs
- [x] Log + classify hooks; should_loop; extension register
- [x] Internal `src/harness_internal.h` shared by modules
- [x] Smoke test covering multi-party context + redaction + requires_action
- [x] Example building a session context

---

## Non-goals (remain deliberate absences)

- No sockets, TLS, or HTTP client inside core
- No automatic tool execution inside core
- No automatic Honcho or OpenAI network calls inside core
- No mandatory Honcho deployment (peer ids work offline)
