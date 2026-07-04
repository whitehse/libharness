/* libharness - core state machine (ADR 006 plumbing, ADR 010 interfaces)
 * Pure C, syscall-free, callback-free. Lua for policy/tools/personality/looping.
 * All I/O and events owned by caller. Matches sibling lib* patterns.
 */
#include "harness.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

 /* Internal state machine states (libassh style, explicit, deterministic) */
typedef enum {
    HARNESS_STATE_INIT = 0,
    HARNESS_STATE_READY,
    HARNESS_STATE_PROCESSING_OPENAI,
    HARNESS_STATE_TOOL_CALL,
    HARNESS_STATE_LOOPING,
    HARNESS_STATE_LOGGING,
    HARNESS_STATE_VECTOR_OP,
    HARNESS_STATE_ERROR,
    HARNESS_STATE_DESTROYED
} harness_state_t;

struct harness_ctx {
    harness_role_t role;
    harness_state_t state;
    harness_config_t config;
    /* event queue stub */
    harness_event_t* event_queue;
    size_t queue_size;
    size_t queue_head;
    size_t queue_tail;
    /* Lua state placeholder (real lua_State* in lua_bindings) */
    void* lua_state;
    /* pique and honcho handles */
    void* pique;
    void* honcho;
    /* extension registry stub */
    struct {
        char name[64];
        harness_extension_fn fn;
    } extensions[16];
    size_t ext_count;
    /* output buffer */
    uint8_t* output_buf;
    size_t output_len;
    /* simple stats */
    uint64_t interactions_logged;
};

harness_ctx_t* harness_create(harness_role_t role) {
    harness_config_t default_cfg = {
        .event_queue_size = 64,
        .lua_init_script = NULL,
        .pique_ctx = NULL,
        .honcho_ctx = NULL,
        .user_data = NULL
    };
    return harness_create_with_config(role, &default_cfg);
}

harness_ctx_t* harness_create_with_config(harness_role_t role, const harness_config_t* config) {
    if (!config) return NULL;
    harness_ctx_t* ctx = calloc(1, sizeof(harness_ctx_t));
    if (!ctx) return NULL;
    ctx->role = role;
    ctx->state = HARNESS_STATE_INIT;
    ctx->config = *config;
    ctx->queue_size = config->event_queue_size ? config->event_queue_size : 64;
    ctx->event_queue = calloc(ctx->queue_size, sizeof(harness_event_t));
    if (!ctx->event_queue) {
        free(ctx);
        return NULL;
    }
    ctx->lua_state = NULL; /* populated by lua_bindings init */
    ctx->pique = config->pique_ctx;
    ctx->honcho = config->honcho_ctx;
    ctx->ext_count = 0;
    ctx->output_buf = NULL;
    ctx->output_len = 0;
    ctx->interactions_logged = 0;
    ctx->state = HARNESS_STATE_READY;
    return ctx;
}

void harness_destroy(harness_ctx_t* ctx) {
    if (!ctx) return;
    ctx->state = HARNESS_STATE_DESTROYED;
    free(ctx->event_queue);
    free(ctx->output_buf);
    /* Note: do not free pique/honcho/lua_state; caller owns */
    free(ctx);
}

void harness_reset(harness_ctx_t* ctx) {
    if (!ctx || ctx->state == HARNESS_STATE_DESTROYED) return;
    ctx->state = HARNESS_STATE_READY;
    ctx->queue_head = ctx->queue_tail = 0;
    ctx->output_len = 0;
}

int harness_feed_input(harness_ctx_t* ctx, const uint8_t* data, size_t len) {
    (void)data; (void)len;
    if (!ctx || ctx->state == HARNESS_STATE_DESTROYED) return -1;
    /* Stub: in real impl, parse OpenAI JSON or Lua results, advance state machine */
    /* For now, just acknowledge and emit a synthetic event */
    if (ctx->queue_tail < ctx->queue_size) {
        ctx->event_queue[ctx->queue_tail++] = HARNESS_EVENT_PERSONALITY_SET; /* example */
    }
    ctx->state = HARNESS_STATE_READY;
    return 0;
}

int harness_next_event(harness_ctx_t* ctx, harness_event_t* event) {
    if (!ctx || !event || ctx->state == HARNESS_STATE_DESTROYED) return -1;
    if (ctx->queue_head >= ctx->queue_tail) {
        *event = HARNESS_EVENT_NONE;
        return 0;
    }
    *event = ctx->event_queue[ctx->queue_head++];
    return 0;
}

int harness_get_output(harness_ctx_t* ctx, uint8_t* buf, size_t max_len, size_t* out_len) {
    if (!ctx || !buf || !out_len) return -1;
    size_t to_copy = ctx->output_len > max_len ? max_len : ctx->output_len;
    if (ctx->output_buf) memcpy(buf, ctx->output_buf, to_copy);
    *out_len = to_copy;
    return 0;
}

int harness_register_extension(harness_ctx_t* ctx, const char* name, harness_extension_fn fn) {
    if (!ctx || !name || !fn || ctx->ext_count >= 16) return -1;
    strncpy(ctx->extensions[ctx->ext_count].name, name, 63);
    ctx->extensions[ctx->ext_count].fn = fn;
    ctx->ext_count++;
    /* Emit event */
    if (ctx->queue_tail < ctx->queue_size) {
        ctx->event_queue[ctx->queue_tail++] = HARNESS_EVENT_EXTENSION_CALLED;
    }
    return 0;
}

const char* harness_version(void) {
    return "0.1.0-bootstrap";
}

/* Stubs for Lua-called functions (real impl in lua_bindings + pique_integration) */
int harness_lua_enumerate_tools(harness_ctx_t* ctx) {
    if (!ctx) return -1;
    /* TODO: query Lua registry or PG for tools, emit event */
    if (ctx->queue_tail < ctx->queue_size) {
        ctx->event_queue[ctx->queue_tail++] = HARNESS_EVENT_TOOL_ENUMERATED;
    }
    return 0;
}

int harness_lua_set_personality(harness_ctx_t* ctx, const char* personality_json_or_id) {
    if (!ctx || !personality_json_or_id) return -1;
    /* TODO: store in PG via pique + pg_vector, or Honcho */
    if (ctx->queue_tail < ctx->queue_size) {
        ctx->event_queue[ctx->queue_tail++] = HARNESS_EVENT_PERSONALITY_SET;
    }
    return 0;
}

int harness_lua_process_openai_compat(harness_ctx_t* ctx, const char* request_json) {
    if (!ctx || !request_json) return -1;
    ctx->state = HARNESS_STATE_PROCESSING_OPENAI;
    /* TODO: prepare request buffer for caller to send via librest/shaggy; parse response */
    return 0;
}

bool harness_lua_should_loop(harness_ctx_t* ctx, const char* criteria) {
    if (!ctx || !criteria) return false;
    /* TODO: evaluate Lua criteria or simple state */
    ctx->state = HARNESS_STATE_LOOPING;
    if (ctx->queue_tail < ctx->queue_size) {
        ctx->event_queue[ctx->queue_tail++] = HARNESS_EVENT_LOOP_DECISION;
    }
    return true; /* stub */
}

int harness_lua_log_interaction(harness_ctx_t* ctx, const char* model, const char* prompt, const char* response) {
    (void)prompt; (void)response;
    if (!ctx || !model) return -1;
    ctx->interactions_logged++;
    ctx->state = HARNESS_STATE_LOGGING;
    /* TODO: INSERT into PG via libpique, with vector embedding if possible */
    if (ctx->queue_tail < ctx->queue_size) {
        ctx->event_queue[ctx->queue_tail++] = HARNESS_EVENT_INTERACTION_LOGGED;
    }
    return 0;
}

int harness_lua_classify_vector(harness_ctx_t* ctx, const char* data, const char* collection) {
    (void)collection;
    if (!ctx || !data) return -1;
    ctx->state = HARNESS_STATE_VECTOR_OP;
    /* TODO: use libpique + pg_vector to embed/classify, reduce tokens */
    if (ctx->queue_tail < ctx->queue_size) {
        ctx->event_queue[ctx->queue_tail++] = HARNESS_EVENT_VECTOR_CLASSIFIED;
    }
    return 0;
}

int harness_lua_honcho_store(harness_ctx_t* ctx, const char* key, const char* value) {
    if (!ctx || !key || !value) return -1;
    /* TODO: delegate to Honcho interface */
    return 0;
}

const char* harness_lua_honcho_retrieve(harness_ctx_t* ctx, const char* key) {
    if (!ctx || !key) return NULL;
    /* TODO: retrieve from Honcho */
    return "stub-memory-value";
}