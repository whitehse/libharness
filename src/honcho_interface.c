#include "harness.h"

/* Honcho memory interface.
   Wraps Honcho peer memory (facts, conclusions, session context) for harness personalities and long-term memory.
   In full impl, calls into Honcho C API or FFI (honcho_profile, honcho_search, honcho_conclude, etc.).
   Since Honcho is part of Hermes runtime, this provides the abstraction for Lua scripts.
*/

int harness_honcho_init(harness_ctx_t* ctx, void* honcho_handle) {
    if (!ctx) return -1;
    ctx->honcho = honcho_handle;
    return 0;
}

int harness_honcho_store_memory(harness_ctx_t* ctx, const char* peer, const char* key, const char* fact) {
    /* TODO: honcho_conclude or equivalent */
    return harness_lua_honcho_store(ctx, key, fact);
}

const char* harness_honcho_get_memory(harness_ctx_t* ctx, const char* peer, const char* key) {
    return harness_lua_honcho_retrieve(ctx, key);
}