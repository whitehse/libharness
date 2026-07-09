#ifndef LIBHARNESS_H
#define LIBHARNESS_H

/*
 * libharness public API — ADR 002 domain model (Session, Model, Context,
 * Response, Honcho) + ADR 006 plumbing + ADR 010 opaque / FFI-friendly types.
 *
 * Core is syscall-free and callback-free. Caller owns network, PG wire,
 * Honcho transport, and event-loop scheduling.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque harness context */
typedef struct harness_ctx harness_ctx_t;

/* -------------------------------------------------------------------------
 * Instance role (which face of the library this ctx primarily serves)
 * ------------------------------------------------------------------------- */
typedef enum {
    HARNESS_ROLE_MAIN = 0,      /* orchestrator */
    HARNESS_ROLE_PROCESSOR,     /* OpenAI-compatible context/response */
    HARNESS_ROLE_MEMORY         /* Honcho + PG memory face */
} harness_role_t;

/* Participant kind (session peers) */
typedef enum {
    HARNESS_PARTICIPANT_HUMAN = 0,
    HARNESS_PARTICIPANT_APP,
    HARNESS_PARTICIPANT_AGENT
} harness_participant_kind_t;

/* Message roles in a context (OpenAI-compatible) */
typedef enum {
    HARNESS_MSG_SYSTEM = 0,     /* SOUL / privileged instructions */
    HARNESS_MSG_DEVELOPER,      /* alias of system */
    HARNESS_MSG_USER,
    HARNESS_MSG_ASSISTANT,
    HARNESS_MSG_TOOL
} harness_message_role_t;

/* Response status after parse */
typedef enum {
    HARNESS_RESPONSE_UNKNOWN = 0,
    HARNESS_RESPONSE_COMPLETED,
    HARNESS_RESPONSE_REQUIRES_ACTION
} harness_response_status_t;

/* Tool type categories (provider-dependent; unknown stays opaque) */
typedef enum {
    HARNESS_TOOL_FUNCTION = 0,
    HARNESS_TOOL_CODE_INTERPRETER,
    HARNESS_TOOL_FILE_SEARCH,
    HARNESS_TOOL_WEB_SEARCH,
    HARNESS_TOOL_OPAQUE
} harness_tool_type_t;

/* -------------------------------------------------------------------------
 * Events (structured, not bare enums — queue of these)
 * ------------------------------------------------------------------------- */
typedef enum {
    HARNESS_EVENT_NONE = 0,
    HARNESS_EVENT_SESSION_SET,
    HARNESS_EVENT_PARTICIPANT_ADDED,
    HARNESS_EVENT_MESSAGE_APPENDED,
    HARNESS_EVENT_SECRET_REFERENCED,
    HARNESS_EVENT_TOOL_REGISTERED,
    HARNESS_EVENT_TOOL_ENUMERATED,
    HARNESS_EVENT_SOUL_SET,
    HARNESS_EVENT_CONTEXT_READY,
    HARNESS_EVENT_RESPONSE_COMPLETED,
    HARNESS_EVENT_RESPONSE_REQUIRES_ACTION,
    HARNESS_EVENT_LOOP_DECISION,
    HARNESS_EVENT_INTERACTION_LOGGED,
    HARNESS_EVENT_VECTOR_CLASSIFIED,
    HARNESS_EVENT_HONCHO_MIRRORED,
    HARNESS_EVENT_EXTENSION_CALLED,
    HARNESS_EVENT_ERROR
} harness_event_type_t;

typedef struct harness_event {
    harness_event_type_t type;
    char peer_id[128];
    char call_id[64];
    int code;
    size_t index;
} harness_event_t;

/* -------------------------------------------------------------------------
 * Config (caller-owned pointers; not freed by library)
 * ------------------------------------------------------------------------- */
typedef struct {
    size_t event_queue_size;
    size_t max_participants;    /* 0 → default */
    size_t max_messages;        /* 0 → default */
    size_t max_tools;           /* 0 → default */
    const char* workspace_id;   /* Honcho workspace / isolation namespace */
    const char* session_id;     /* durable multi-party session id */
    const char* acting_peer_id; /* peer for whom context is built this turn */
    const char* lua_init_script;/* prefer in-memory/PG over path (policy) */
    void* pique_ctx;            /* libpique handle (optional) */
    void* honcho_ctx;           /* Honcho handle (optional) */
    void* user_data;
} harness_config_t;

/* Context build parameters (model turn) */
typedef struct {
    const char* model;          /* required for a real provider call */
    double temperature;         /* e.g. 0.0–2.0; ignored if < 0 */
    bool include_tools;         /* include registered tools array */
    bool identity_prefix;       /* prepend [peer_id]: (default true for sessions) */
    bool redact_secrets;        /* non-privileged acting peer sees references */
} harness_context_params_t;

/* Create / destroy / reset */
harness_ctx_t* harness_create(harness_role_t role);
harness_ctx_t* harness_create_with_config(harness_role_t role, const harness_config_t* config);
void harness_destroy(harness_ctx_t* ctx);
void harness_reset(harness_ctx_t* ctx);

/* Default config helper (stack-friendly) */
void harness_config_init_defaults(harness_config_t* config);

/* -------------------------------------------------------------------------
 * Session + participants (ADR 002)
 * ------------------------------------------------------------------------- */
int harness_session_set(harness_ctx_t* ctx,
                        const char* workspace_id,
                        const char* session_id);

int harness_session_get(const harness_ctx_t* ctx,
                        const char** workspace_id_out,
                        const char** session_id_out);

int harness_set_acting_peer(harness_ctx_t* ctx, const char* peer_id);

/* peer_id aligns with Honcho peer id. privileged=true may see secret values. */
int harness_participant_add(harness_ctx_t* ctx,
                            const char* peer_id,
                            harness_participant_kind_t kind,
                            bool privileged);

size_t harness_participant_count(const harness_ctx_t* ctx);

int harness_participant_get(const harness_ctx_t* ctx,
                            size_t index,
                            char* peer_id_out,
                            size_t peer_id_cap,
                            harness_participant_kind_t* kind_out,
                            bool* privileged_out);

/* -------------------------------------------------------------------------
 * Messages (session history used when building context)
 * ------------------------------------------------------------------------- */
int harness_message_append(harness_ctx_t* ctx,
                           const char* peer_id,
                           harness_message_role_t role,
                           const char* content,
                           bool is_secret);

/* Tool result message (role=tool), linked by tool_call_id */
int harness_message_append_tool_result(harness_ctx_t* ctx,
                                       const char* tool_call_id,
                                       const char* content);

size_t harness_message_count(const harness_ctx_t* ctx);

/* Format identity prefix into buf, e.g. "[human_alice]". Returns bytes needed
 * (excluding NUL) or -1 on error. Truncates if buflen too small. */
int harness_format_identity_prefix(const char* peer_id, char* buf, size_t buflen);

/* -------------------------------------------------------------------------
 * SOUL (system / developer instructions) + tools
 * ------------------------------------------------------------------------- */
int harness_soul_set(harness_ctx_t* ctx, const char* soul_text);
const char* harness_soul_get(const harness_ctx_t* ctx);

/* Register one tool as OpenAI-compatible tool JSON object string.
 * Primary path is type=function; other types stored opaquely. */
int harness_tool_register_json(harness_ctx_t* ctx, const char* tool_json);
void harness_tools_clear(harness_ctx_t* ctx);
size_t harness_tool_count(const harness_ctx_t* ctx);
int harness_tools_enumerate(harness_ctx_t* ctx); /* emits TOOL_ENUMERATED */

/* -------------------------------------------------------------------------
 * Context builder + response parser (Model: context → response)
 * Prepared JSON available via harness_get_output after CONTEXT_READY.
 * ------------------------------------------------------------------------- */
int harness_context_build(harness_ctx_t* ctx, const harness_context_params_t* params);

/* Feed provider response JSON (caller obtained via network). Emits
 * RESPONSE_COMPLETED or RESPONSE_REQUIRES_ACTION. */
int harness_response_parse(harness_ctx_t* ctx, const uint8_t* data, size_t len);

harness_response_status_t harness_response_status(const harness_ctx_t* ctx);

/* Number of function_call items from last requires_action parse (stub-level). */
size_t harness_response_tool_call_count(const harness_ctx_t* ctx);

/* -------------------------------------------------------------------------
 * Classic plumbing: feed_input / next_event / get_output
 * feed_input currently treats buffer as a provider response (parse).
 * ------------------------------------------------------------------------- */
int harness_feed_input(harness_ctx_t* ctx, const uint8_t* data, size_t len);
int harness_next_event(harness_ctx_t* ctx, harness_event_t* event);
int harness_get_output(harness_ctx_t* ctx, uint8_t* buf, size_t max_len, size_t* out_len);

/* -------------------------------------------------------------------------
 * Loop policy hook (Lua evaluates criteria; C only surfaces decision event)
 * ------------------------------------------------------------------------- */
bool harness_should_loop(harness_ctx_t* ctx, const char* criteria);

/* -------------------------------------------------------------------------
 * Logging + vectors (PG via pique) — full interaction audit
 * ------------------------------------------------------------------------- */
int harness_log_interaction(harness_ctx_t* ctx,
                            const char* model,
                            const char* prompt,
                            const char* response);

int harness_classify_vector(harness_ctx_t* ctx,
                            const char* data,
                            const char* collection);

/* -------------------------------------------------------------------------
 * Honcho (optional provider; peer ids still first-class without it)
 * Narrative messages only — tool calls are not mirrored by default (ADR 002).
 * ------------------------------------------------------------------------- */
int harness_honcho_attach(harness_ctx_t* ctx, void* honcho_handle);

int harness_honcho_mirror_message(harness_ctx_t* ctx,
                                  const char* peer_id,
                                  const char* content,
                                  const char* metadata_json);

int harness_honcho_store_memory(harness_ctx_t* ctx,
                                const char* peer_id,
                                const char* key,
                                const char* fact);

const char* harness_honcho_get_memory(harness_ctx_t* ctx,
                                      const char* peer_id,
                                      const char* key);

/* -------------------------------------------------------------------------
 * Lua policy surface (C entry points used by bindings)
 * lua_State* is void* so public header stays free of lua.h.
 * ------------------------------------------------------------------------- */
int harness_lua_init(harness_ctx_t* ctx, void* lua_state);

/* Compatibility wrappers (bootstrap names → ADR 002 surfaces) */
int harness_lua_enumerate_tools(harness_ctx_t* ctx);
int harness_lua_set_personality(harness_ctx_t* ctx, const char* personality_json_or_id);
int harness_lua_process_openai_compat(harness_ctx_t* ctx, const char* request_json);
bool harness_lua_should_loop(harness_ctx_t* ctx, const char* criteria);
int harness_lua_log_interaction(harness_ctx_t* ctx, const char* model, const char* prompt, const char* response);
int harness_lua_classify_vector(harness_ctx_t* ctx, const char* data, const char* collection);
int harness_lua_honcho_store(harness_ctx_t* ctx, const char* key, const char* value);
const char* harness_lua_honcho_retrieve(harness_ctx_t* ctx, const char* key);

/* Extension registration (registration only; no core callbacks on I/O) */
typedef int (*harness_extension_fn)(harness_ctx_t* ctx, void* arg);
int harness_register_extension(harness_ctx_t* ctx, const char* name, harness_extension_fn fn);

const char* harness_version(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBHARNESS_H */
