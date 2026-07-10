# libharness — Implementation TODO

Current state (v0.7.0-todo-impl): Honcho buffer dialectic (feed peer-card/conclude +
cross-ctx parse), score→keep_mask history compress, `parse_data_rows` alias for
app-flattened pqwire DATA_ROW text, Lua helpers, docs/ADR alignment with
plumbing philosophy. Builds under `-Wall -Wextra -Wpedantic -Werror`.

Domain vocabulary: **ADR 002**. Session model: **ADR 003** (one session per ctx).
Plumbing philosophy: **ADR 004** (sibling-aligned).

---

## High Priority: Context builder & response processor (ADR 002)

### 1.1–1.2  Context / response
- [x] Identity prefix, secrets, tool_calls, multipart, stream, nested args, usage
- [ ] Integration with a real JSON library if/when available (libjsparse is DOM-ref
      scanner, not a general JSON library — leave hand path until a sibling exists)

### 1.3  Tool registry
- [x] C registration + tools_to_json
- [x] Lua `register_tool(name, schema, fn)` + `invoke_tool` / `harness_tool_policy_invoke`

### 1.4  Secret / privilege redaction
- [x] Stable secret refs
- [x] Capability matrix (`HARNESS_CAP_*`, set/get capabilities)
- [x] Lua custom mirror hook (`set_should_mirror`)
- [x] PG log field redaction (`redact_secrets_in_log`, `harness_redact_text_for_log`, feed_log)

---

## High Priority: Session & participant plumbing

### 2.1  Durable session identity
- [x] retire / is_retired
- [x] Single session per ctx (ADR 003)
- [x] Session upsert SQL builder for pique
- [x] Session upsert staged via `harness_pique_feed_session` (caller feeds libpique)

### 2.2  Participant lifecycle
- [x] Remove / privilege / mute / capabilities
- [x] Kind-specific SOUL defaults
- [x] Honcho buffer dialectic tests (caller-owned HTTP mock via buffers)

### 2.3  Message history
- [x] Drop-oldest, export/import JSON
- [x] `harness_history_compress` (heuristic keep-last; vector classify event)
- [x] `harness_history_compress_select` (caller keep-mask after embedding scores)
- [x] `harness_history_keep_mask_from_scores` / `compress_by_scores` (caller-supplied floats)
- [ ] True live embedding scoring via real vectors + DB (optional; needs remote PG)

---

## High Priority: Honcho join

### 3.1–3.2  Plumbing & mirror
- [x] Request builder, parse, metadata, should_mirror, transport notes
- [x] Lua should_mirror override
- [x] feed_peer_card / feed_conclude stage path

### 3.3  Memory facts
- [x] In-process KV
- [x] Peer-card + conclude request builders
- [x] Buffer dialectic (no network) for multi-party request/parse
- [ ] Live Honcho HTTP against a real deployment (caller-owned; outside core)

---

## Medium Priority: PostgreSQL / pg_vector

### 4.1  Interaction logging
- [x] Local ring + export
- [x] SQL INSERT builder (`harness_pique_build_log_insert`)
- [x] `harness_pique_feed_sql` / `feed_log` / `feed_session` stage for caller→libpique
- [x] Optional compile-time link against libpqwire (`HAVE_PIQUE`, `harness_pique_submit_staged`)

### 4.2  Vectors
- [x] Compress hook emits VECTOR_CLASSIFIED
- [x] Embedding insert + similarity search SQL builders
- [x] Feed embedding/similarity + parse similarity TSV into `VECTOR_HIT` events
- [x] `harness_pique_parse_data_rows` (DATA_ROW text handoff after app unpack)
- [ ] Binary pqwire DATA_ROW decoding inside core (deferred; keep unpack in app)

---

## Medium Priority: Lua policy surface

### 5.1  Complete table
- [x] register_tool, invoke_tool, set_should_mirror, set_loop_criterion,
      history_compress, set_capabilities, soul_for_kind, load_script, …
- [x] `next_event` / `drain_events` (coroutine-friendly event poll at boundaries)
- [x] `wait_event` / `poll_until` yield helpers (return nil, "would_yield")
- [x] history_compress_by_scores, honcho_feed_*, parse_data_rows

### 5.2  Loop criteria
- [x] Built-in true/false + Lua expression + registered criterion fn

---

## Medium Priority: Events / config / interfaces

### 6.x
- [x] Event names, backpressure, caller output buffer, caps/flags
- [x] Deprecation note on bootstrap `harness_lua_*` wrappers
- [x] Event `detail[]` field + `harness_emit_ex`
- [x] PIQUE_SQL_READY / PIQUE_FEED_STAGED / VECTOR_HIT events

---

## Lower Priority: Testing / docs

### 7.x
- [x] Smoke, dialectic session, dialectic Honcho, history_stream, policy_pique tests
- [x] fuzz_harness, dialectic_agents example
- [x] `scripts/run_valgrind.sh` (skips if valgrind absent)
- [x] ADR 001–004 documented (bootstrap, session, single-session, plumbing)
- [x] AGENTS/ARCHITECTURE/DOMAIN aligned with v0.7 surface
- [ ] Adopt remaining common ADR set from shaggy (005+) as needed for protocol steps

---

## Interface change log

### v0.7 batch
- [x] harness_history_keep_mask_from_scores / compress_by_scores
- [x] harness_pique_parse_data_rows
- [x] harness_honcho_feed_peer_card / feed_conclude
- [x] tests/test_dialectic_honcho.c
- [x] ADR 004 plumbing alignment + doc polish

### v0.6 batch
- [x] CMake finds libpqwire (`pqwire.h` / `libpqwire.a`) as HAVE_PIQUE
- [x] harness_pique_feed_embedding / feed_similarity
- [x] harness_pique_parse_similarity_tsv → VECTOR_HIT events
- [x] harness_pique_submit_staged (pqwire_send_query when linked + handle set)
- [x] Lua wait_event, poll_until, pique_feed_embedding/similarity, parse tsv,
      history_compress_select

### v0.5 batch
- [x] harness_event_t.detail + emit_ex
- [x] redact_secrets_in_log + harness_redact_text_for_log
- [x] harness_pique_feed_sql / feed_log / feed_session
- [x] embedding insert + similarity search SQL builders
- [x] harness_history_compress_select
- [x] Lua next_event / drain_events / pique_feed_*

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
- No binary PostgreSQL wire row decoder inside core (app flattens to TSV)
