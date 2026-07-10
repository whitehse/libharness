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

Caller supplies the vector **SQL literal** (already formatted), never a float
array from inside the library:

```c
harness_pique_feed_embedding(ctx, "soul", text, "'[0.1,0.2,0.3]'::vector");
harness_pique_feed_similarity(ctx, "soul", "'[0.1,0.2,0.3]'::vector", 8);
/* or the build_* helpers if you want SQL without staging */
```

After the caller obtains similarity scores, apply compression or parse rows:

```c
uint8_t keep[N]; /* 1 = keep message i */
harness_history_compress_select(ctx, keep, message_count);

/* Or parse TSV-like results from the app's pg client: */
harness_pique_parse_similarity_tsv(ctx,
    "0.91\tpreference for brevity\n0.40|12|noise\n", 0);
/* → VECTOR_HIT (code = score*1000, detail = text) + VECTOR_CLASSIFIED */
```

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
