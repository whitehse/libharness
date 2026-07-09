#ifndef HARNESS_INTERNAL_H
#define HARNESS_INTERNAL_H

/* Shared among src/ translation units; not installed. Opaque to public consumers. */

#include "harness.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define HARNESS_DEFAULT_QUEUE       64
#define HARNESS_DEFAULT_PARTICIPANTS 32
#define HARNESS_DEFAULT_MESSAGES    256
#define HARNESS_DEFAULT_TOOLS       32
#define HARNESS_PEER_ID_MAX         128
#define HARNESS_CALL_ID_MAX         64
#define HARNESS_MSG_CONTENT_MAX     4096
#define HARNESS_TOOL_JSON_MAX       2048
#define HARNESS_SOUL_MAX            8192
#define HARNESS_ID_MAX              128
#define HARNESS_EXT_MAX             16

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
    bool privileged;
} harness_participant_slot_t;

typedef struct {
    char peer_id[HARNESS_PEER_ID_MAX];
    char tool_call_id[HARNESS_CALL_ID_MAX];
    harness_message_role_t role;
    char content[HARNESS_MSG_CONTENT_MAX];
    bool is_secret;
    bool in_use;
} harness_message_slot_t;

typedef struct {
    char json[HARNESS_TOOL_JSON_MAX];
    bool in_use;
} harness_tool_slot_t;

struct harness_ctx {
    harness_role_t role;
    harness_state_t state;
    harness_config_t config;

    char workspace_id[HARNESS_ID_MAX];
    char session_id[HARNESS_ID_MAX];
    char acting_peer_id[HARNESS_PEER_ID_MAX];

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

    harness_response_status_t last_response_status;
    size_t last_tool_call_count;

    uint64_t interactions_logged;
    uint32_t secret_seq;
};

/* Internal helpers (defined in harness.c) */
void harness_emit(harness_ctx_t* ctx, harness_event_type_t type,
                  const char* peer_id, const char* call_id, int code, size_t index);
int harness_set_output(harness_ctx_t* ctx, const void* data, size_t len);
const harness_participant_slot_t* harness_find_participant(const harness_ctx_t* ctx,
                                                          const char* peer_id);

/* Module entry points implemented outside harness.c */
int harness_openai_context_build_impl(harness_ctx_t* ctx, const harness_context_params_t* params);
int harness_openai_response_parse_impl(harness_ctx_t* ctx, const uint8_t* data, size_t len);

#endif /* HARNESS_INTERNAL_H */
