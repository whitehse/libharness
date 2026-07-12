# libpique transport notes (caller-owned)

libharness never opens sockets or speaks the PostgreSQL wire protocol itself.
It builds SQL text and stages it for the calling application to feed into
libpique / libpqwire (or any other PG client).

## Session upsert

```c
harness_pique_feed_session(ctx);
/* PIQUE_SQL_READY + PIQUE_FEED_STAGED events; SQL in harness_get_output */
```

Or build without staging:

```c
char sql[2048];
size_t n;
harness_pique_build_session_upsert(ctx, sql, sizeof(sql), &n);
/* caller: pique_feed_input(pique, sql, n); ... */
```

## Interaction log (with secret redaction)

When `config.redact_secrets_in_log` is true (default via
`harness_config_init_defaults`), `harness_pique_feed_log` replaces known secret
message contents with `[secret_ref:N]` before embedding them in the INSERT.

```c
harness_pique_feed_log(ctx, "gpt-4o", prompt_json, response_json);
```

## Embeddings / similarity (pg_vector shape)

Caller may supply a ready SQL vector literal, or format real float dimensions:

```c
float dims[] = {0.1f, 0.2f, 0.3f};
char lit[128];
size_t n;
harness_pique_format_vector_literal(dims, 3, lit, sizeof(lit), &n);
/* lit == "'[0.1,0.2,0.3]'::vector" */

harness_pique_feed_embedding(ctx, "soul", text, lit);
harness_pique_feed_similarity(ctx, "soul", lit, 8);
/* or the build_* helpers if you want SQL without staging */
```

Message text for embedding is available without JSON export:

```c
char content[4096];
harness_message_get(ctx, i, NULL, 0, NULL, content, sizeof(content), NULL);
```

After the caller obtains similarity scores, apply compression or parse rows:

```c
/* Preferred: let the library pick top-K by score */
float scores[N]; /* parallel to message indices */
harness_history_compress_by_scores(ctx, scores, message_count, keep_k);

/* Or parse TSV scores only (no VECTOR_HIT events) */
size_t n_scores = 0;
harness_pique_parse_similarity_scores(tsv, 0, scores, N, &n_scores);

/* Or build the mask yourself */
uint8_t keep[N];
harness_history_keep_mask_from_scores(scores, N, keep_k, keep, N);
harness_history_compress_select(ctx, keep, message_count);

/* Or parse TSV-like results from the app's pg client / DATA_ROW flatten: */
const char* rows = "0.91\tpreference for brevity\n0.40|12|noise\n";
harness_pique_parse_data_rows(ctx, rows, strlen(rows));
/* → VECTOR_HIT (code = score*1000, detail = text) + VECTOR_CLASSIFIED */
```

Live remote PG remains caller-owned: stage SQL → app feeds pqwire/libpique →
flatten DATA_ROW → parse scores → compress. See `tests/test_vector_scoring.c`.

## Optional HAVE_PIQUE (libpqwire)

When CMake finds `/home/dwhite/pique` (`pqwire.h` + `libpqwire.a`), the library
is built with `HAVE_PIQUE=1`. With `config.pique_ctx` set to a client
`pqwire_ctx_t*`:

```c
harness_pique_feed_session(ctx);
harness_pique_submit_staged(ctx); /* pqwire_send_query(ctx->pique, sql) */
```

This only stages a Simple Query on the pqwire context; the application still
owns network I/O (`pqwire_get_output` / `feed_input`).

## Events

| Event | Meaning |
|-------|---------|
| `pique_sql_ready` | SQL bytes available via `get_output` |
| `pique_feed_staged` | feed_log / feed_session / embed / similarity completed; `detail` is kind |
| `vector_hit` | one similarity row (`code` ≈ score×1000, `detail` = text) |
| `vector_classified` | summary after compress or TSV parse |

## Dialectic testing without PG

Use two buffers: harness stages SQL → fake pique accepts bytes → assert shape.
No network required. TSV parse needs no PG either.
