# libharness — Implementation TODO

Current state (v0.4.0-todo-impl): privilege capability matrix, kind-specific SOUL,
history compress, pique SQL builders, Lua register_tool / loop / should_mirror
hooks, Honcho peer-card+conclude builders, ADR 003 single-session-per-ctx,
valgrind script, full test suite. Builds under `-Wall -Wextra -Wpedantic -Werror`.

Domain vocabulary: **ADR 002**. Session model: **ADR 003** (one session per ctx).

---

## High Priority: Context builder & response processor (ADR 002)

### 1.1–1.2  Context / response
- [x] Identity prefix, secrets, tool_calls, multipart, stream, nested args, usage
- [ ] Integration with a real JSON library if/when available

### 1.3  Tool registry
- [x] C registration + tools_to_json
- [x] Lua `register_tool(name, schema, fn)` + `invoke_tool` / `harness_tool_policy_invoke`

### 1.4  Secret / privilege redaction
- [x] Stable secret refs
- [x] Capability matrix (`HARNESS_CAP_*`, set/get capabilities)
- [x] Lua custom mirror hook (`set_should_mirror`)
- [ ] PG log field redaction policy (when real pique path lands)

---

## High Priority: Session & participant plumbing

### 2.1  Durable session identity
- [x] retire / is_retired
- [x] Single session per ctx (ADR 003)
- [x] Session upsert SQL builder for pique
- [ ] Live pique feed of session upsert

### 2.2  Participant lifecycle
- [x] Remove / privilege / mute / capabilities
- [x] Kind-specific SOUL defaults
- [ ] Live Honcho peer integration tests (optional)

### 2.3  Message history
- [x] Drop-oldest, export/import JSON
- [x] `harness_history_compress` (heuristic keep-last; vector classify event)
- [ ] True embedding-based compression via pique/pg_vector

---

## High Priority: Honcho join

### 3.1–3.2  Plumbing & mirror
- [x] Request builder, parse, metadata, should_mirror, transport notes
- [x] Lua should_mirror override

### 3.3  Memory facts
- [x] In-process KV
- [x] Peer-card + conclude request builders
- [ ] Live Honcho HTTP dialectic (caller-owned)

---

## Medium Priority: PostgreSQL / pg_vector

### 4.1  Interaction logging
- [x] Local ring + export
- [x] SQL INSERT builder (`harness_pique_build_log_insert`)
- [ ] Real libpique feed_input of generated SQL

### 4.2  Vectors
- [x] Compress hook emits VECTOR_CLASSIFIED
- [ ] Embeddings + similarity search when pique linked

---

## Medium Priority: Lua policy surface

### 5.1  Complete table
- [x] register_tool, invoke_tool, set_should_mirror, set_loop_criterion,
      history_compress, set_capabilities, soul_for_kind, load_script, …
- [ ] Coroutine yield helpers at event boundaries

### 5.2  Loop criteria
- [x] Built-in true/false + Lua expression + registered criterion fn

---

## Medium Priority: Events / config / interfaces

### 6.x
- [x] Event names, backpressure, caller output buffer, caps/flags
- [x] Deprecation note on bootstrap `harness_lua_*` wrappers
- [ ] Richer event payloads / side tables

---

## Lower Priority: Testing / docs

### 7.x
- [x] Smoke, dialectic, history_stream, policy_pique tests
- [x] fuzz_harness, dialectic_agents example
- [x] `scripts/run_valgrind.sh` (skips if valgrind absent)
- [x] ADR 003 documented
- [ ] Keep AGENTS/ARCHITECTURE/DOMAIN fully polished
- [ ] Adopt full common ADR set from shaggy (optional)

---

## Interface change log

### v0.4 batch
- [x] HARNESS_CAP_* + set/get capabilities
- [x] soul_set/get_for_kind + inject into context for acting peer kind
- [x] history_compress
- [x] pique SQL builders (log insert + session upsert)
- [x] Honcho peer_card / conclude builders
- [x] Lua register_tool / invoke / should_mirror / loop criterion / expression eval
- [x] ADR 003 + valgrind script + policy_pique test

---

## Non-goals

- No sockets/HTTP/tool execution inside core
- No mandatory Honcho or libpique at link time
