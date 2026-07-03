#ifndef LIBHARNESS_H
#define LIBHARNESS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque harness context */
typedef struct harness_ctx harness_ctx_t;

/* Config for harness creation (caller owned) */
typedef struct {
    size_t event_queue_size;
    const char* lua_init_script;  /* path or embedded, but prefer in-memory or PG */
    /* pique config, honcho config, etc. passed via context */
    void* pique_ctx;   /* from libpique */
    void* honcho_ctx;  /* Honcho interface handle */
    /* extension points */
    void* user_data;
} harness_config_t;

/* Roles for the harness */
typedef enum {
    HARNESS_ROLE_MAIN = 0,
    HARNESS_ROLE_PROCESSOR,
    HARNESS_ROLE_MEMORY
} harness_role_t;

/* Event types emitted by harness (for event-driven use) */
typedef enum {
    HARNESS_EVENT_NONE = 0,
    HARNESS_EVENT_TOOL_ENUMERATED,
    HARNESS_EVENT_PERSONALITY_SET,
    HARNESS_EVENT_LOOP_DECISION,
    HARNESS_EVENT_INTERACTION_LOGGED,
    HARNESS_EVENT_VECTOR_CLASSIFIED,
    HARNESS_EVENT_EXTENSION_CALLED
} harness_event_t;

/* Create/destroy */
harness_ctx_t* harness_create(harness_role_t role);
harness_ctx_t* harness_create_with_config(harness_role_t role, const harness_config_t* config);
void harness_destroy(harness_ctx_t* ctx);
void harness_reset(harness_ctx_t* ctx);

/* Feed input (buffers from caller, e.g. OpenAI JSON responses via librest) */
int harness_feed_input(harness_ctx_t* ctx, const uint8_t* data, size_t len);

/* Get next event (event-driven) */
int harness_next_event(harness_ctx_t* ctx, harness_event_t* event);

/* Output buffer (prepared requests or Lua results) */
int harness_get_output(harness_ctx_t* ctx, uint8_t* buf, size_t max_len, size_t* out_len);

/* Lua interface entry points (callable from Lua via bindings) */
int harness_lua_enumerate_tools(harness_ctx_t* ctx);
int harness_lua_set_personality(harness_ctx_t* ctx, const char* personality_json_or_id);
int harness_lua_process_openai_compat(harness_ctx_t* ctx, const char* request_json);
bool harness_lua_should_loop(harness_ctx_t* ctx, const char* criteria);
int harness_lua_log_interaction(harness_ctx_t* ctx, const char* model, const char* prompt, const char* response);
int harness_lua_classify_vector(harness_ctx_t* ctx, const char* data, const char* collection);
int harness_lua_honcho_store(harness_ctx_t* ctx, const char* key, const char* value);
const char* harness_lua_honcho_retrieve(harness_ctx_t* ctx, const char* key);

/* Extension registration (C or Lua loaded) */
typedef int (*harness_extension_fn)(harness_ctx_t* ctx, void* arg);
int harness_register_extension(harness_ctx_t* ctx, const char* name, harness_extension_fn fn);

/* Status */
const char* harness_version(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBHARNESS_H */