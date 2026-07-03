#include "harness.h"

/* libpique (PostgreSQL wire) integration.
   All model interactions logged to PG.
   pg_vector used for:
   - classifying state/data to reduce tokens sent to model
   - storing/retrieving personalities (vector similarity search)
   - local memory embeddings
   Caller provides pique_ctx (connected libpique state machine).
   This module issues queries via pique_feed / pique_next_event pattern.
*/

int harness_pique_log_interaction(harness_ctx_t* ctx, const char* model, const char* prompt, const char* response, const char* embedding) {
    if (!ctx || !ctx->pique) return -1;
    /* TODO: use libpique to execute INSERT INTO interactions (model, prompt, response, ts) */
    /* For vector: INSERT INTO memory (embedding, metadata) using pg_vector */
    ctx->interactions_logged++;
    return 0;
}

int harness_pique_classify(harness_ctx_t* ctx, const char* text, const char* collection, float* vector_out, size_t dim) {
    if (!ctx || !ctx->pique) return -1;
    /* TODO: embed text (or call external embedder), store in pg_vector table, return nearest */
    return 0;
}

int harness_pique_store_personality(harness_ctx_t* ctx, const char* personality_id, const char* json, const float* embedding) {
    if (!ctx || !ctx->pique) return -1;
    /* Store in personalities table with vector column for similarity search */
    return 0;
}