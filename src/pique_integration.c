/* libpique / libpqwire integration helpers (ADR 002 logging path).
 * No sockets here: SQL is staged via harness_pique_feed_* for the caller.
 * When HAVE_PIQUE, config.pique_ctx may be a pqwire client for submit_staged.
 */
#include "harness_internal.h"
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_PIQUE
#include "pqwire.h"
#endif

int harness_pique_log_interaction(harness_ctx_t* ctx,
                                  const char* model,
                                  const char* prompt,
                                  const char* response,
                                  const char* embedding) {
    (void)embedding;
    if (!ctx) return -1;
    /* Prefer staged SQL feed path (redaction + output + local ring). */
    return harness_pique_feed_log(ctx, model ? model : "unknown", prompt, response);
}

int harness_pique_classify(harness_ctx_t* ctx,
                           const char* text,
                           const char* collection,
                           float* vector_out,
                           size_t dim) {
    (void)vector_out;
    (void)dim;
    return harness_classify_vector(ctx, text, collection);
}

int harness_pique_store_personality(harness_ctx_t* ctx,
                                    const char* personality_id,
                                    const char* json,
                                    const float* embedding) {
    (void)embedding;
    if (!ctx || !personality_id || !json) return -1;
    /* Stage SOUL locally; when embeddings present caller uses feed_embedding. */
    return harness_soul_set(ctx, json);
}

/* Dialectics: build SQL for caller-fed libpique; no sockets here. */
int harness_pique_prepare_log(harness_ctx_t* ctx,
                              const char* model,
                              const char* prompt,
                              const char* response,
                              char* sql_buf,
                              size_t cap,
                              size_t* out_len) {
    int rc = harness_pique_build_log_insert(ctx, model, prompt, response, sql_buf, cap, out_len);
    if (rc == 0)
        (void)harness_log_interaction(ctx, model ? model : "unknown", prompt, response);
    return rc;
}

int harness_pique_submit_staged(harness_ctx_t* ctx) {
    char* sql = NULL;
    size_t len = 0;
    if (!ctx) return -1;
    if (!ctx->pique && !ctx->config.pique_ctx) return -1;

    /* Snapshot staged SQL from output buffer. */
    if (!ctx->output_buf || ctx->output_len == 0) return -1;
    len = ctx->output_len;
    sql = (char*)malloc(len + 1);
    if (!sql) return -1;
    memcpy(sql, ctx->output_buf, len);
    sql[len] = '\0';

#ifdef HAVE_PIQUE
    {
        void* handle = ctx->pique ? ctx->pique : ctx->config.pique_ctx;
        pqwire_ctx_t* pq = (pqwire_ctx_t*)handle;
        if (pqwire_send_query(pq, sql) != 0) {
            free(sql);
            return -1;
        }
        harness_emit_ex(ctx, HARNESS_EVENT_PIQUE_FEED_STAGED, NULL, NULL, 0, len,
                        "submit_query");
        free(sql);
        return 0;
    }
#else
    /* No linked pqwire: leave SQL staged, emit diagnostic only. */
    free(sql);
    harness_emit_ex(ctx, HARNESS_EVENT_ERROR, NULL, NULL, -1, 0, "no_pique_link");
    return -1;
#endif
}
