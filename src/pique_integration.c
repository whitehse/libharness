/* libpique (PostgreSQL wire) integration stubs.
 * Full interaction audit + pg_vector classification (ADR 002 logging path).
 */
#include "harness_internal.h"

int harness_pique_log_interaction(harness_ctx_t* ctx,
                                  const char* model,
                                  const char* prompt,
                                  const char* response,
                                  const char* embedding) {
    (void)prompt;
    (void)response;
    (void)embedding;
    if (!ctx) return -1;
    if (!ctx->pique) {
        /* Still allow local counter / event for dialectic tests without PG */
        return harness_log_interaction(ctx, model ? model : "unknown", prompt, response);
    }
    return harness_log_interaction(ctx, model ? model : "unknown", prompt, response);
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
    /* SOUL text may be staged locally until PG write exists */
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
