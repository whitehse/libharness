# libharness — Implementation TODO

Current state (v0.2.0-todo-impl): multi-party session APIs, context builder with
assistant `tool_calls[]` + stable secret refs + developer→system mapping,
structured response parse (tool call list + usage + assistant text), C tool
registration, session retire / participant lifecycle, Honcho request builder +
mirror policy, dialectic + smoke tests. Builds under
`-Wall -Wextra -Wpedantic -Werror`. Network, PG wire, and Honcho HTTP remain
caller-owned (ADR 006).

Domain vocabulary: **ADR 002**. Update this file when major items complete.

---

## High Priority: Context builder & response processor (ADR 002)

### 1.1  Full OpenAI-compatible context builder
- [x] Identity prefixes + secret references (stable `secret_ref_id`)
- [x] Assistant messages carrying `tool_calls[]` (id, type, function.name, arguments)
- [x] `tool` role messages with `tool_call_id` round-tripped in build
- [x] `developer` role → `system` when `map_developer_to_system` (default true)
- [x] Configurable `max_context_bytes`
- [x] Deterministic append-order messages (tested)
- [ ] Multi-part content arrays (text / image_url / refusal, etc.)
- [ ] Integration with a real JSON library if/when available (libjsparse is JS DOM, not JSON)

Files: `src/openai_processor.c`, `include/harness.h`, tests

### 1.2  Full response parser / normalizer
- [x] Chat Completions tool_calls extract + Responses-style `call_id`/`function_call` heuristics
- [x] Structured tool-call list (`harness_response_tool_call_get`)
- [x] Usage getters (`prompt_tokens` / `completion_tokens` / `total_tokens`)
- [x] Assistant text getter
- [x] Error / incomplete status events (basic)
- [ ] Streaming chunk assembly path (optional)
- [ ] Hardening: nested JSON arguments object (non-string) full re-serialize

Files: `src/openai_processor.c`, `include/harness.h`, tests

### 1.3  Tool registry completeness
- [x] Register tools from C structs (`harness_tool_register` / `harness_tool_def_t`)
- [x] Opaque pass-through types (`code_interpreter`, `file_search`, `web_search`)
- [x] Namespaced tool names allowed (string name as-is)
- [x] Enumerate tools as JSON array (`harness_tools_to_json`)
- [ ] Lua `register_tool(name, schema, fn)` with callable policy fn

Files: `src/harness.c`, `src/lua_bindings.c`, `include/harness.h`

### 1.4  Secret / privilege redaction
- [x] Stable secret reference ids across rebuilds
- [x] Privileged vs non-privileged acting peer
- [ ] Privilege matrix beyond single `privileged` bool
- [ ] Lua custom redaction hooks
- [ ] PG log field redaction policy

Files: `src/openai_processor.c`, `src/lua_bindings.c`, `src/honcho_interface.c`

---

## High Priority: Session & participant plumbing

### 2.1  Durable session identity
- [x] `harness_session_retire` + `harness_session_is_retired` (blocks appends)
- [ ] Persist session/workspace ids via pique when available
- [ ] Multi-session handle model vs single session per ctx (decide + ADR if multi)

Files: `src/harness.c`, `src/pique_integration.c`

### 2.2  Participant lifecycle
- [x] Remove / privilege-change APIs
- [ ] Mute API wired into context build (slot has `muted` unused)
- [ ] Kind-specific SOUL defaults
- [ ] Honcho peer id integration tests with live Honcho (optional)

Files: `include/harness.h`, `src/harness.c`

### 2.3  Message history management
- [x] Drop-oldest when `drop_oldest_messages` and cap hit
- [ ] Import/export history as JSON for dialectic tests
- [ ] Vector-assisted compression hooks

Files: `src/harness.c`, `src/pique_integration.c`

---

## High Priority: Honcho join (optional provider)

### 3.1  Honcho client plumbing (caller-fed)
- [x] Buffer-level request builder (`harness_honcho_build_messages_request`)
- [ ] Parse Honcho responses into events
- [ ] Document curl/HTTP examples in docs/examples

Files: `src/honcho_interface.c`, `include/harness.h`, examples

### 3.2  Mirror policy
- [x] Default: do not mirror tool-call-like payloads
- [x] Narrative mirror event
- [ ] Metadata conventions helpers (typed builders)
- [ ] Lua `should_mirror(message) → bool`

Files: `src/honcho_interface.c`, `src/lua_bindings.c`

### 3.3  Memory facts API
- [x] In-process key/value store per peer (stub until real Honcho)
- [ ] Peer-card / conclusion surfaces against live Honcho API shapes

Files: `src/honcho_interface.c`, `src/harness.c`

---

## Medium Priority: PostgreSQL / pg_vector (libpique)

### 4.1  Interaction logging
- [ ] INSERT path via pique event/feed API
- [ ] Store model, context, response, usage, session_id, acting_peer_id
- [ ] Dialectic test with fake pique buffers

Files: `src/pique_integration.c`, tests

### 4.2  Vector classification & personality storage
- [ ] Embeddings + similarity search for SOUL
- [ ] Token-reduction classifier
- [x] Graceful no-op when libpique not linked

Files: `src/pique_integration.c`

---

## Medium Priority: Lua policy surface

### 5.1  Complete `harness` Lua table
- [x] Expanded binds: session_set, response_parse, log_interaction, tool_register_json, …
- [ ] Bind remaining ADR 002 APIs
- [ ] Coroutine-friendly yield points
- [ ] Load scripts from memory/PG rather than path

Files: `src/lua_bindings.c`

### 5.2  Loop criteria
- [x] Simple string criteria (`false`/`0`/`never` stop)
- [ ] Real Lua expression / registered criteria functions
- [x] Events carry loop decision code (0/1)

Files: `src/harness.c`, `src/lua_bindings.c`

---

## Medium Priority: Events, config, interfaces (ADR 010)

### 6.1  Event queue robustness
- [x] `harness_event_type_name()`
- [x] Backpressure: drop new vs drop oldest (`event_backpressure`)
- [ ] Richer event payloads / side tables

Files: `include/harness.h`, `src/harness.c`

### 6.2  Expand `harness_config_t`
- [x] caps + `max_context_bytes`
- [x] flags: `no_identity_prefix_default`, `mirror_tool_calls`, `map_developer_to_system`, `drop_oldest_messages`
- [ ] optional fixed output buffer supplied by caller

Files: `include/harness.h`, `src/harness.c`

### 6.3  Remove remaining bootstrap coupling
- [ ] Deprecate timeline for `harness_lua_*` wrappers
- [x] Public headers remain FFI-friendly

---

## Lower Priority: Testing, fuzzing, examples, docs

### 7.1  Dialectic-style tests
- [x] Multi-participant context build + redaction matrix
- [x] requires_action → tool result → completed (no network)
- [x] Honcho mirror exclusion of tool payloads
- [x] Buffer handoff between two harness contexts

Files: `tests/test_dialectic_session.c`

### 7.2  Fuzz targets
- [ ] `fuzz_harness` for response_parse and context message content

### 7.3  Valgrind clean
- [ ] Explicit Valgrind job for long sequences

### 7.4  Examples
- [ ] Multi-agent dialectic example binary
- [ ] librest/shaggy transport wiring notes

### 7.5  Documentation
- [ ] Keep AGENTS/ARCHITECTURE/DOMAIN in sync (partially done)
- [ ] Consider full common ADR set from shaggy

---

## Interface change log

### ADR 002 alignment (earlier)
- [x] Session/participant/message/SOUL/tools, context_build, response_parse stubs, events

### TODO implementation batch (this work)
- [x] Assistant tool_calls in history + context JSON
- [x] Structured response tool_calls + usage + assistant text
- [x] `harness_tool_register` / `harness_tools_to_json`
- [x] Stable secret refs; developer→system mapping; max_context_bytes
- [x] Session retire; participant remove/set_privileged; drop-oldest messages
- [x] Honcho build request + mirror tool rejection; in-process memory slots
- [x] Config flags + event_backpressure + event type names
- [x] Dialectic test target

---

## Non-goals (remain deliberate absences)

- No sockets, TLS, or HTTP client inside core
- No automatic tool execution inside core
- No automatic Honcho or OpenAI network calls inside core
- No mandatory Honcho deployment (peer ids work offline)
