#ifndef HARNESS_INTERNAL_H
#define HARNESS_INTERNAL_H

/* Shared among src/ translation units; not installed. Opaque to public consumers. */

#include "harness.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define HARNESS_DEFAULT_QUEUE        64
#define HARNESS_DEFAULT_PARTICIPANTS 32
#define HARNESS_DEFAULT_MESSAGES    256
#define HARNESS_DEFAULT_TOOLS        32
#define HARNESS_DEFAULT_CONTEXT_CAP (64u * 1024u)
#define HARNESS_PEER_ID_MAX         HARNESS_PEER_ID_CAP
#define HARNESS_CALL_ID_MAX         HARNESS_CALL_ID_CAP
#define HARNESS_MSG_CONTENT_MAX     4096
#define HARNESS_TOOL_JSON_MAX       4096
#define HARNESS_SOUL_MAX            8192
#define HARNESS_ID_MAX              128
#define HARNESS_EXT_MAX             16
#define HARNESS_MSG_TOOL_CALLS_MAX  8
#define HARNESS_PARSED_TOOL_CALLS_MAX 16
#define HARNESS_MEMORY_SLOTS        32
#define HARNESS_MEMORY_KEY_MAX      64
#define HARNESS_MEMORY_VAL_MAX     512
#define HARNESS_LOG_SLOTS           32
#define HARNESS_STREAM_DEFAULT_CAP  (64u * 1024u)
#define HARNESS_LUA_TOOL_MAX        32
#define HARNESS_LUA_NOREF           (-2)

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

typedef struct {
    char peer_id[HARNESS_PEER_ID_MAX];
    harness_participant_kind_t kind;
    bool privileged;           /* legacy; mirrors SEE_SECRETS */
    bool muted;
    uint32_t capabilities;     /* HARNESS_CAP_* bitset */
} harness_participant_slot_t;

typedef struct {
    char id[HARNESS_CALL_ID_MAX];
    char name[HARNESS_TOOL_NAME_CAP];
    char arguments[HARNESS_TOOL_ARGS_CAP];
    bool in_use;
} harness_embedded_tool_call_t;

typedef struct {
    char peer_id[HARNESS_PEER_ID_MAX];
    char tool_call_id[HARNESS_CALL_ID_MAX];
    harness_message_role_t role;
    char content[HARNESS_MSG_CONTENT_MAX];
    bool is_secret;
    uint32_t secret_ref_id;
    bool in_use;
    bool content_is_parts; /* content holds JSON array of parts */
    harness_embedded_tool_call_t tool_calls[HARNESS_MSG_TOOL_CALLS_MAX];
    size_t tool_call_count;
} harness_message_slot_t;

typedef struct {
    char json[HARNESS_TOOL_JSON_MAX];
    char name[HARNESS_TOOL_NAME_CAP];
    harness_tool_type_t type;
    bool in_use;
} harness_tool_slot_t;

typedef struct {
    char key[HARNESS_MEMORY_KEY_MAX];
    char peer_id[HARNESS_PEER_ID_MAX];
    char value[HARNESS_MEMORY_VAL_MAX];
    bool in_use;
} harness_memory_slot_t;

struct harness_ctx {
    harness_role_t role;
    harness_state_t state;
    harness_config_t config;

    char workspace_id[HARNESS_ID_MAX];
    char session_id[HARNESS_ID_MAX];
    char acting_peer_id[HARNESS_PEER_ID_MAX];
    bool session_retired;

    harness_participant_slot_t* participants;
    size_t participant_cap;
    size_t participant_count;

    harness_message_slot_t* messages;
    size_t message_cap;
    size_t message_count;

    harness_tool_slot_t* tools;
    size_t tool_cap;
    size_t tool_count;

    char soul[HARNESS_SOUL_MAX];
    char soul_by_kind[3][HARNESS_SOUL_MAX]; /* human, app, agent */

    harness_event_t* event_queue;
    size_t queue_size;
    size_t queue_head;
    size_t queue_tail;

    void* lua_state;
    void* pique;
    void* honcho;

    struct {
        char name[64];
        harness_extension_fn fn;
    } extensions[HARNESS_EXT_MAX];
    size_t ext_count;

    uint8_t* output_buf;
    size_t output_len;
    size_t output_cap;
    bool output_is_caller_owned;

    harness_response_status_t last_response_status;
    size_t last_tool_call_count;
    harness_tool_call_t parsed_tool_calls[HARNESS_PARSED_TOOL_CALLS_MAX];
    harness_usage_t last_usage;
    char last_assistant_text[HARNESS_ASSISTANT_TEXT_CAP];

    harness_memory_slot_t memory[HARNESS_MEMORY_SLOTS];

    harness_interaction_record_t log_ring[HARNESS_LOG_SLOTS];
    size_t log_count;
    size_t log_head; /* next write index */

    uint8_t* stream_buf;
    size_t stream_len;
    size_t stream_cap;
    bool stream_active;

    uint64_t interactions_logged;
    uint32_t secret_seq;

    /* Lua policy tool refs (registry indices); -2 = none */
    struct {
        char name[HARNESS_TOOL_NAME_CAP];
        int ref;
        bool in_use;
    } lua_tools[HARNESS_LUA_TOOL_MAX];
    size_t lua_tool_count;
    int lua_should_mirror_ref;
    int lua_loop_criterion_ref;
};

void harness_emit(harness_ctx_t* ctx, harness_event_type_t type,
                  const char* peer_id, const char* call_id, int code, size_t index);
void harness_emit_ex(harness_ctx_t* ctx, harness_event_type_t type,
                     const char* peer_id, const char* call_id, int code, size_t index,
                     const char* detail);
int harness_set_output(harness_ctx_t* ctx, const void* data, size_t len);
const harness_participant_slot_t* harness_find_participant(const harness_ctx_t* ctx,
                                                          const char* peer_id);
harness_participant_slot_t* harness_find_participant_mut(harness_ctx_t* ctx,
                                                        const char* peer_id);
int harness_ensure_message_slot(harness_ctx_t* ctx);
void harness_copy_id(char* dst, size_t cap, const char* src);

int harness_json_escape_append(char* dest, size_t cap, size_t* used, const char* src);
int harness_json_append_raw(char* dest, size_t cap, size_t* used, const char* s);

const char* harness_msg_role_name(harness_message_role_t role);
int harness_msg_role_from_name(const char* name, harness_message_role_t* out);

int harness_openai_context_build_impl(harness_ctx_t* ctx, const harness_context_params_t* params);
int harness_openai_response_parse_impl(harness_ctx_t* ctx, const uint8_t* data, size_t len);

void harness_log_record_push(harness_ctx_t* ctx, const char* model);
bool harness_peer_can_see_secrets(const harness_ctx_t* ctx, const char* peer_id);
int harness_lua_eval_should_mirror(harness_ctx_t* ctx, const char* content, int* out_bool);
int harness_lua_eval_loop(harness_ctx_t* ctx, const char* criteria, int* out_bool);

#endif /* HARNESS_INTERNAL_H */
