/* libharness - core state machine (ADR 006 plumbing, ADR 002 domain, ADR 010)
 * Pure C, syscall-free, callback-free. Lua for policy/tools/SOUL/looping.
 * All I/O and events owned by caller.
 */
#include "harness_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void harness_copy_id(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

int harness_json_escape_append(char* dest, size_t cap, size_t* used, const char* src) {
    size_t i;
    if (!dest || !used || !src) return -1;
    for (i = 0; src[i]; i++) {
        char c = src[i];
        const char* rep = NULL;
        char tmp[7];
        size_t rep_len = 0;
        size_t j;
        if (c == '\\') { rep = "\\\\"; rep_len = 2; }
        else if (c == '"') { rep = "\\\""; rep_len = 2; }
        else if (c == '\n') { rep = "\\n"; rep_len = 2; }
        else if (c == '\r') { rep = "\\r"; rep_len = 2; }
        else if (c == '\t') { rep = "\\t"; rep_len = 2; }
        else if ((unsigned char)c < 0x20) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)c);
            rep = tmp;
            rep_len = 6;
        }
        if (rep) {
            if (*used + rep_len >= cap) return -1;
            for (j = 0; j < rep_len; j++) dest[(*used)++] = rep[j];
        } else {
            if (*used + 1 >= cap) return -1;
            dest[(*used)++] = c;
        }
    }
    return 0;
}

int harness_json_append_raw(char* dest, size_t cap, size_t* used, const char* s) {
    size_t n;
    if (!s) return 0;
    n = strlen(s);
    if (*used + n >= cap) return -1;
    memcpy(dest + *used, s, n);
    *used += n;
    return 0;
}

void harness_emit(harness_ctx_t* ctx, harness_event_type_t type,
                  const char* peer_id, const char* call_id, int code, size_t index) {
    harness_event_t* ev;
    if (!ctx || ctx->state == HARNESS_STATE_DESTROYED) return;
    if (ctx->queue_tail >= ctx->queue_size) {
        if (ctx->config.event_backpressure == HARNESS_BACKPRESSURE_DROP_OLDEST &&
            ctx->queue_size > 0) {
            /* compact: drop one from head */
            if (ctx->queue_head < ctx->queue_tail) ctx->queue_head++;
            if (ctx->queue_head > 0 && ctx->queue_head == ctx->queue_tail) {
                ctx->queue_head = ctx->queue_tail = 0;
            }
            if (ctx->queue_tail >= ctx->queue_size) return;
        } else {
            return;
        }
    }
    ev = &ctx->event_queue[ctx->queue_tail++];
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    harness_copy_id(ev->peer_id, sizeof(ev->peer_id), peer_id);
    harness_copy_id(ev->call_id, sizeof(ev->call_id), call_id);
    ev->code = code;
    ev->index = index;
}

int harness_set_output(harness_ctx_t* ctx, const void* data, size_t len) {
    if (!ctx) return -1;
    if (len == 0) {
        ctx->output_len = 0;
        return 0;
    }
    if (ctx->config.caller_output_buf && ctx->config.caller_output_cap > 0) {
        size_t n = len < ctx->config.caller_output_cap ? len : ctx->config.caller_output_cap;
        memcpy(ctx->config.caller_output_buf, data, n);
        ctx->output_buf = ctx->config.caller_output_buf;
        ctx->output_cap = ctx->config.caller_output_cap;
        ctx->output_len = n;
        ctx->output_is_caller_owned = true;
        return (n == len) ? 0 : -1;
    }
    if (ctx->output_is_caller_owned) {
        ctx->output_buf = NULL;
        ctx->output_cap = 0;
        ctx->output_is_caller_owned = false;
    }
    if (len > ctx->output_cap) {
        uint8_t* n = (uint8_t*)realloc(ctx->output_buf, len);
        if (!n) return -1;
        ctx->output_buf = n;
        ctx->output_cap = len;
    }
    memcpy(ctx->output_buf, data, len);
    ctx->output_len = len;
    return 0;
}

const harness_participant_slot_t* harness_find_participant(const harness_ctx_t* ctx,
                                                          const char* peer_id) {
    size_t i;
    if (!ctx || !peer_id) return NULL;
    for (i = 0; i < ctx->participant_count; i++) {
        if (strcmp(ctx->participants[i].peer_id, peer_id) == 0)
            return &ctx->participants[i];
    }
    return NULL;
}

harness_participant_slot_t* harness_find_participant_mut(harness_ctx_t* ctx,
                                                        const char* peer_id) {
    return (harness_participant_slot_t*)harness_find_participant(ctx, peer_id);
}

int harness_ensure_message_slot(harness_ctx_t* ctx) {
    if (!ctx) return -1;
    if (ctx->message_count < ctx->message_cap) return 0;
    if (!ctx->config.drop_oldest_messages || ctx->message_cap == 0) return -1;
    memmove(&ctx->messages[0], &ctx->messages[1],
            (ctx->message_cap - 1) * sizeof(ctx->messages[0]));
    ctx->message_count--;
    memset(&ctx->messages[ctx->message_count], 0, sizeof(ctx->messages[0]));
    return 0;
}

void harness_config_init_defaults(harness_config_t* config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->event_queue_size = HARNESS_DEFAULT_QUEUE;
    config->max_participants = HARNESS_DEFAULT_PARTICIPANTS;
    config->max_messages = HARNESS_DEFAULT_MESSAGES;
    config->max_tools = HARNESS_DEFAULT_TOOLS;
    config->max_context_bytes = HARNESS_DEFAULT_CONTEXT_CAP;
    config->map_developer_to_system = true;
    config->mirror_tool_calls = false;
    config->no_identity_prefix_default = false;
    config->drop_oldest_messages = false;
    config->event_backpressure = HARNESS_BACKPRESSURE_DROP;
}

harness_ctx_t* harness_create(harness_role_t role) {
    harness_config_t cfg;
    harness_config_init_defaults(&cfg);
    return harness_create_with_config(role, &cfg);
}

harness_ctx_t* harness_create_with_config(harness_role_t role, const harness_config_t* config) {
    harness_ctx_t* ctx;
    size_t q, p, m, t;

    if (!config) return NULL;
    ctx = (harness_ctx_t*)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->role = role;
    ctx->state = HARNESS_STATE_INIT;
    ctx->config = *config;
    /* ensure map_developer default if caller zeroed struct without init */
    if (!config->event_queue_size && !config->max_messages) {
        /* still honor explicit zeros for caps via ternary below */
    }

    q = config->event_queue_size ? config->event_queue_size : HARNESS_DEFAULT_QUEUE;
    p = config->max_participants ? config->max_participants : HARNESS_DEFAULT_PARTICIPANTS;
    m = config->max_messages ? config->max_messages : HARNESS_DEFAULT_MESSAGES;
    t = config->max_tools ? config->max_tools : HARNESS_DEFAULT_TOOLS;
    if (config->max_context_bytes == 0)
        ctx->config.max_context_bytes = HARNESS_DEFAULT_CONTEXT_CAP;

    ctx->queue_size = q;
    ctx->event_queue = (harness_event_t*)calloc(q, sizeof(harness_event_t));
    ctx->participants = (harness_participant_slot_t*)calloc(p, sizeof(*ctx->participants));
    ctx->messages = (harness_message_slot_t*)calloc(m, sizeof(*ctx->messages));
    ctx->tools = (harness_tool_slot_t*)calloc(t, sizeof(*ctx->tools));
    if (!ctx->event_queue || !ctx->participants || !ctx->messages || !ctx->tools) {
        free(ctx->event_queue);
        free(ctx->participants);
        free(ctx->messages);
        free(ctx->tools);
        free(ctx);
        return NULL;
    }
    ctx->participant_cap = p;
    ctx->message_cap = m;
    ctx->tool_cap = t;

    harness_copy_id(ctx->workspace_id, sizeof(ctx->workspace_id), config->workspace_id);
    harness_copy_id(ctx->session_id, sizeof(ctx->session_id), config->session_id);
    harness_copy_id(ctx->acting_peer_id, sizeof(ctx->acting_peer_id), config->acting_peer_id);

    ctx->pique = config->pique_ctx;
    ctx->honcho = config->honcho_ctx;
    ctx->lua_state = NULL;
    ctx->session_retired = false;
    ctx->lua_should_mirror_ref = HARNESS_LUA_NOREF;
    ctx->lua_loop_criterion_ref = HARNESS_LUA_NOREF;
    ctx->lua_tool_count = 0;
    ctx->state = HARNESS_STATE_READY;
    return ctx;
}

void harness_destroy(harness_ctx_t* ctx) {
    if (!ctx) return;
    ctx->state = HARNESS_STATE_DESTROYED;
    free(ctx->event_queue);
    free(ctx->participants);
    free(ctx->messages);
    free(ctx->tools);
    if (!ctx->output_is_caller_owned)
        free(ctx->output_buf);
    free(ctx->stream_buf);
    free(ctx);
}

void harness_reset(harness_ctx_t* ctx) {
    if (!ctx || ctx->state == HARNESS_STATE_DESTROYED) return;
    ctx->state = HARNESS_STATE_READY;
    ctx->queue_head = ctx->queue_tail = 0;
    ctx->output_len = 0;
    ctx->participant_count = 0;
    ctx->message_count = 0;
    ctx->tool_count = 0;
    ctx->soul[0] = '\0';
    ctx->session_retired = false;
    ctx->last_response_status = HARNESS_RESPONSE_UNKNOWN;
    ctx->last_tool_call_count = 0;
    ctx->last_assistant_text[0] = '\0';
    memset(&ctx->last_usage, 0, sizeof(ctx->last_usage));
    memset(ctx->parsed_tool_calls, 0, sizeof(ctx->parsed_tool_calls));
    memset(ctx->participants, 0, ctx->participant_cap * sizeof(*ctx->participants));
    memset(ctx->messages, 0, ctx->message_cap * sizeof(*ctx->messages));
    memset(ctx->tools, 0, ctx->tool_cap * sizeof(*ctx->tools));
    memset(ctx->memory, 0, sizeof(ctx->memory));
}

int harness_session_set(harness_ctx_t* ctx, const char* workspace_id, const char* session_id) {
    if (!ctx || ctx->state == HARNESS_STATE_DESTROYED) return -1;
    harness_copy_id(ctx->workspace_id, sizeof(ctx->workspace_id), workspace_id);
    harness_copy_id(ctx->session_id, sizeof(ctx->session_id), session_id);
    ctx->session_retired = false;
    harness_emit(ctx, HARNESS_EVENT_SESSION_SET, NULL, NULL, 0, 0);
    return 0;
}

int harness_session_get(const harness_ctx_t* ctx,
                        const char** workspace_id_out,
                        const char** session_id_out) {
    if (!ctx) return -1;
    if (workspace_id_out) *workspace_id_out = ctx->workspace_id;
    if (session_id_out) *session_id_out = ctx->session_id;
    return 0;
}

int harness_session_retire(harness_ctx_t* ctx) {
    if (!ctx || ctx->state == HARNESS_STATE_DESTROYED) return -1;
    ctx->session_retired = true;
    harness_emit(ctx, HARNESS_EVENT_SESSION_RETIRED, NULL, NULL, 0, 0);
    return 0;
}

bool harness_session_is_retired(const harness_ctx_t* ctx) {
    return ctx ? ctx->session_retired : false;
}

int harness_set_acting_peer(harness_ctx_t* ctx, const char* peer_id) {
    if (!ctx || !peer_id) return -1;
    harness_copy_id(ctx->acting_peer_id, sizeof(ctx->acting_peer_id), peer_id);
    return 0;
}

int harness_participant_add(harness_ctx_t* ctx,
                            const char* peer_id,
                            harness_participant_kind_t kind,
                            bool privileged) {
    harness_participant_slot_t* slot;
    if (!ctx || !peer_id || peer_id[0] == '\0') return -1;
    if (harness_find_participant(ctx, peer_id)) return -1;
    if (ctx->participant_count >= ctx->participant_cap) return -1;
    slot = &ctx->participants[ctx->participant_count];
    harness_copy_id(slot->peer_id, sizeof(slot->peer_id), peer_id);
    slot->kind = kind;
    slot->privileged = privileged;
    slot->muted = false;
    slot->capabilities = privileged
        ? (HARNESS_CAP_SEE_SECRETS | HARNESS_CAP_MIRROR_HONCHO | HARNESS_CAP_INVOKE_TOOLS)
        : HARNESS_CAP_NONE;
    ctx->participant_count++;
    harness_emit(ctx, HARNESS_EVENT_PARTICIPANT_ADDED, peer_id, NULL, (int)kind,
                 ctx->participant_count - 1);
    return 0;
}

int harness_participant_remove(harness_ctx_t* ctx, const char* peer_id) {
    size_t i;
    if (!ctx || !peer_id) return -1;
    for (i = 0; i < ctx->participant_count; i++) {
        if (strcmp(ctx->participants[i].peer_id, peer_id) == 0) {
            if (i + 1 < ctx->participant_count) {
                memmove(&ctx->participants[i], &ctx->participants[i + 1],
                        (ctx->participant_count - i - 1) * sizeof(ctx->participants[0]));
            }
            ctx->participant_count--;
            memset(&ctx->participants[ctx->participant_count], 0, sizeof(ctx->participants[0]));
            harness_emit(ctx, HARNESS_EVENT_PARTICIPANT_REMOVED, peer_id, NULL, 0, i);
            return 0;
        }
    }
    return -1;
}

int harness_participant_set_privileged(harness_ctx_t* ctx,
                                       const char* peer_id,
                                       bool privileged) {
    harness_participant_slot_t* slot = harness_find_participant_mut(ctx, peer_id);
    if (!slot) return -1;
    slot->privileged = privileged;
    if (privileged)
        slot->capabilities |= HARNESS_CAP_SEE_SECRETS;
    else
        slot->capabilities &= ~HARNESS_CAP_SEE_SECRETS;
    return 0;
}

size_t harness_participant_count(const harness_ctx_t* ctx) {
    return ctx ? ctx->participant_count : 0;
}

int harness_participant_get(const harness_ctx_t* ctx,
                            size_t index,
                            char* peer_id_out,
                            size_t peer_id_cap,
                            harness_participant_kind_t* kind_out,
                            bool* privileged_out) {
    const harness_participant_slot_t* slot;
    if (!ctx || index >= ctx->participant_count) return -1;
    slot = &ctx->participants[index];
    if (peer_id_out && peer_id_cap > 0) harness_copy_id(peer_id_out, peer_id_cap, slot->peer_id);
    if (kind_out) *kind_out = slot->kind;
    if (privileged_out) *privileged_out = slot->privileged;
    return 0;
}

int harness_message_append(harness_ctx_t* ctx,
                           const char* peer_id,
                           harness_message_role_t role,
                           const char* content,
                           bool is_secret) {
    harness_message_slot_t* slot;
    if (!ctx || !content) return -1;
    if (ctx->session_retired) return -1;
    if (harness_ensure_message_slot(ctx) != 0) return -1;
    slot = &ctx->messages[ctx->message_count];
    memset(slot, 0, sizeof(*slot));
    harness_copy_id(slot->peer_id, sizeof(slot->peer_id), peer_id ? peer_id : "");
    slot->role = role;
    harness_copy_id(slot->content, sizeof(slot->content), content);
    slot->is_secret = is_secret;
    slot->in_use = true;
    if (is_secret) {
        ctx->secret_seq++;
        slot->secret_ref_id = ctx->secret_seq;
        harness_emit(ctx, HARNESS_EVENT_SECRET_REFERENCED, peer_id, NULL,
                     (int)slot->secret_ref_id, ctx->message_count);
    }
    ctx->message_count++;
    harness_emit(ctx, HARNESS_EVENT_MESSAGE_APPENDED, peer_id, NULL, (int)role,
                 ctx->message_count - 1);
    return 0;
}

int harness_message_append_assistant(harness_ctx_t* ctx,
                                     const char* peer_id,
                                     const char* content) {
    return harness_message_append(ctx, peer_id, HARNESS_MSG_ASSISTANT,
                                  content ? content : "", false);
}

int harness_message_assistant_add_tool_call(harness_ctx_t* ctx,
                                            const char* call_id,
                                            const char* name,
                                            const char* arguments_json) {
    harness_message_slot_t* slot;
    harness_embedded_tool_call_t* tc;
    if (!ctx || !call_id || !name) return -1;
    if (ctx->message_count == 0) return -1;
    slot = &ctx->messages[ctx->message_count - 1];
    if (slot->role != HARNESS_MSG_ASSISTANT) return -1;
    if (slot->tool_call_count >= HARNESS_MSG_TOOL_CALLS_MAX) return -1;
    tc = &slot->tool_calls[slot->tool_call_count];
    memset(tc, 0, sizeof(*tc));
    harness_copy_id(tc->id, sizeof(tc->id), call_id);
    harness_copy_id(tc->name, sizeof(tc->name), name);
    harness_copy_id(tc->arguments, sizeof(tc->arguments),
                    arguments_json ? arguments_json : "{}");
    tc->in_use = true;
    slot->tool_call_count++;
    return 0;
}

int harness_message_append_tool_result(harness_ctx_t* ctx,
                                       const char* tool_call_id,
                                       const char* content) {
    harness_message_slot_t* slot;
    if (!ctx || !tool_call_id || !content) return -1;
    if (ctx->session_retired) return -1;
    if (harness_ensure_message_slot(ctx) != 0) return -1;
    slot = &ctx->messages[ctx->message_count];
    memset(slot, 0, sizeof(*slot));
    slot->role = HARNESS_MSG_TOOL;
    harness_copy_id(slot->tool_call_id, sizeof(slot->tool_call_id), tool_call_id);
    harness_copy_id(slot->content, sizeof(slot->content), content);
    slot->in_use = true;
    ctx->message_count++;
    harness_emit(ctx, HARNESS_EVENT_MESSAGE_APPENDED, NULL, tool_call_id,
                 (int)HARNESS_MSG_TOOL, ctx->message_count - 1);
    return 0;
}

size_t harness_message_count(const harness_ctx_t* ctx) {
    return ctx ? ctx->message_count : 0;
}

uint32_t harness_message_secret_ref(const harness_ctx_t* ctx, size_t index) {
    if (!ctx || index >= ctx->message_count) return 0;
    return ctx->messages[index].secret_ref_id;
}

int harness_format_identity_prefix(const char* peer_id, char* buf, size_t buflen) {
    int n;
    if (!peer_id || !buf || buflen == 0) return -1;
    n = snprintf(buf, buflen, "[%s]", peer_id);
    if (n < 0) return -1;
    return n;
}

int harness_soul_set(harness_ctx_t* ctx, const char* soul_text) {
    if (!ctx || !soul_text) return -1;
    harness_copy_id(ctx->soul, sizeof(ctx->soul), soul_text);
    harness_emit(ctx, HARNESS_EVENT_SOUL_SET, NULL, NULL, 0, 0);
    return 0;
}

const char* harness_soul_get(const harness_ctx_t* ctx) {
    if (!ctx) return NULL;
    return ctx->soul;
}

int harness_tool_register_json(harness_ctx_t* ctx, const char* tool_json) {
    harness_tool_slot_t* slot;
    if (!ctx || !tool_json) return -1;
    if (ctx->tool_count >= ctx->tool_cap) return -1;
    slot = &ctx->tools[ctx->tool_count];
    memset(slot, 0, sizeof(*slot));
    harness_copy_id(slot->json, sizeof(slot->json), tool_json);
    slot->type = HARNESS_TOOL_OPAQUE;
    /* best-effort name extract */
    {
        const char* n = strstr(tool_json, "\"name\"");
        if (n) {
            const char* q1 = strchr(n + 6, '"');
            if (q1) {
                const char* q2 = strchr(q1 + 1, '"');
                if (q2 && (size_t)(q2 - q1 - 1) < sizeof(slot->name)) {
                    memcpy(slot->name, q1 + 1, (size_t)(q2 - q1 - 1));
                    slot->name[q2 - q1 - 1] = '\0';
                }
            }
        }
    }
    slot->in_use = true;
    ctx->tool_count++;
    harness_emit(ctx, HARNESS_EVENT_TOOL_REGISTERED, NULL, NULL, 0, ctx->tool_count - 1);
    return 0;
}

int harness_tool_register(harness_ctx_t* ctx, const harness_tool_def_t* def) {
    char json[HARNESS_TOOL_JSON_MAX];
    size_t used = 0;
    const char* type_str = "function";
    if (!ctx || !def || !def->name) return -1;

    switch (def->type) {
    case HARNESS_TOOL_FUNCTION: type_str = "function"; break;
    case HARNESS_TOOL_CODE_INTERPRETER: type_str = "code_interpreter"; break;
    case HARNESS_TOOL_FILE_SEARCH: type_str = "file_search"; break;
    case HARNESS_TOOL_WEB_SEARCH: type_str = "web_search"; break;
    default: type_str = "function"; break;
    }

    if (def->type != HARNESS_TOOL_FUNCTION && def->type != HARNESS_TOOL_OPAQUE) {
        /* opaque provider tools: {"type":"..."} */
        if (harness_json_append_raw(json, sizeof(json), &used, "{\"type\":\"") != 0) return -1;
        if (harness_json_escape_append(json, sizeof(json), &used, type_str) != 0) return -1;
        if (harness_json_append_raw(json, sizeof(json), &used, "\"}") != 0) return -1;
        json[used] = '\0';
        if (harness_tool_register_json(ctx, json) != 0) return -1;
        ctx->tools[ctx->tool_count - 1].type = def->type;
        harness_copy_id(ctx->tools[ctx->tool_count - 1].name,
                        sizeof(ctx->tools[0].name), def->name);
        return 0;
    }

    if (harness_json_append_raw(json, sizeof(json), &used,
            "{\"type\":\"function\",\"function\":{\"name\":\"") != 0) return -1;
    if (harness_json_escape_append(json, sizeof(json), &used, def->name) != 0) return -1;
    if (harness_json_append_raw(json, sizeof(json), &used, "\"") != 0) return -1;
    if (def->description && def->description[0]) {
        if (harness_json_append_raw(json, sizeof(json), &used, ",\"description\":\"") != 0)
            return -1;
        if (harness_json_escape_append(json, sizeof(json), &used, def->description) != 0)
            return -1;
        if (harness_json_append_raw(json, sizeof(json), &used, "\"") != 0) return -1;
    }
    if (harness_json_append_raw(json, sizeof(json), &used, ",\"parameters\":") != 0) return -1;
    if (harness_json_append_raw(json, sizeof(json), &used,
            def->parameters_json && def->parameters_json[0] ? def->parameters_json
                                                          : "{\"type\":\"object\",\"properties\":{}}") != 0)
        return -1;
    if (harness_json_append_raw(json, sizeof(json), &used, "}}") != 0) return -1;
    json[used] = '\0';
    if (harness_tool_register_json(ctx, json) != 0) return -1;
    ctx->tools[ctx->tool_count - 1].type = HARNESS_TOOL_FUNCTION;
    harness_copy_id(ctx->tools[ctx->tool_count - 1].name, sizeof(ctx->tools[0].name), def->name);
    return 0;
}

void harness_tools_clear(harness_ctx_t* ctx) {
    if (!ctx) return;
    memset(ctx->tools, 0, ctx->tool_cap * sizeof(*ctx->tools));
    ctx->tool_count = 0;
}

size_t harness_tool_count(const harness_ctx_t* ctx) {
    return ctx ? ctx->tool_count : 0;
}

int harness_tools_enumerate(harness_ctx_t* ctx) {
    if (!ctx) return -1;
    harness_emit(ctx, HARNESS_EVENT_TOOL_ENUMERATED, NULL, NULL, 0, ctx->tool_count);
    return 0;
}

int harness_tools_to_json(const harness_ctx_t* ctx,
                          char* buf,
                          size_t cap,
                          size_t* out_len) {
    size_t used = 0;
    size_t i;
    if (!ctx || !buf || cap == 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "[") != 0) return -1;
    for (i = 0; i < ctx->tool_count; i++) {
        if (i > 0 && harness_json_append_raw(buf, cap, &used, ",") != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, ctx->tools[i].json) != 0) return -1;
    }
    if (harness_json_append_raw(buf, cap, &used, "]") != 0) return -1;
    if (used >= cap) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}

int harness_context_build(harness_ctx_t* ctx, const harness_context_params_t* params) {
    harness_context_params_t local;
    if (!ctx || !params) return -1;
    local = *params;
    if (params->use_config_identity_default)
        local.identity_prefix = !ctx->config.no_identity_prefix_default;
    ctx->state = HARNESS_STATE_PROCESSING_OPENAI;
    return harness_openai_context_build_impl(ctx, &local);
}

int harness_response_parse(harness_ctx_t* ctx, const uint8_t* data, size_t len) {
    if (!ctx || !data) return -1;
    return harness_openai_response_parse_impl(ctx, data, len);
}

harness_response_status_t harness_response_status(const harness_ctx_t* ctx) {
    return ctx ? ctx->last_response_status : HARNESS_RESPONSE_UNKNOWN;
}

size_t harness_response_tool_call_count(const harness_ctx_t* ctx) {
    return ctx ? ctx->last_tool_call_count : 0;
}

int harness_response_tool_call_get(const harness_ctx_t* ctx,
                                   size_t index,
                                   harness_tool_call_t* out) {
    if (!ctx || !out || index >= ctx->last_tool_call_count) return -1;
    *out = ctx->parsed_tool_calls[index];
    return 0;
}

int harness_response_usage(const harness_ctx_t* ctx, harness_usage_t* out) {
    if (!ctx || !out) return -1;
    *out = ctx->last_usage;
    return 0;
}

const char* harness_response_assistant_text(const harness_ctx_t* ctx) {
    return ctx ? ctx->last_assistant_text : "";
}

int harness_feed_input(harness_ctx_t* ctx, const uint8_t* data, size_t len) {
    return harness_response_parse(ctx, data, len);
}

int harness_next_event(harness_ctx_t* ctx, harness_event_t* event) {
    if (!ctx || !event || ctx->state == HARNESS_STATE_DESTROYED) return -1;
    if (ctx->queue_head >= ctx->queue_tail) {
        memset(event, 0, sizeof(*event));
        event->type = HARNESS_EVENT_NONE;
        return 0;
    }
    *event = ctx->event_queue[ctx->queue_head++];
    return 0;
}

int harness_get_output(harness_ctx_t* ctx, uint8_t* buf, size_t max_len, size_t* out_len) {
    size_t to_copy;
    if (!ctx || !buf || !out_len) return -1;
    to_copy = ctx->output_len > max_len ? max_len : ctx->output_len;
    if (to_copy && ctx->output_buf) memcpy(buf, ctx->output_buf, to_copy);
    *out_len = to_copy;
    return 0;
}

const char* harness_event_type_name(harness_event_type_t type) {
    switch (type) {
    case HARNESS_EVENT_NONE: return "none";
    case HARNESS_EVENT_SESSION_SET: return "session_set";
    case HARNESS_EVENT_SESSION_RETIRED: return "session_retired";
    case HARNESS_EVENT_PARTICIPANT_ADDED: return "participant_added";
    case HARNESS_EVENT_PARTICIPANT_REMOVED: return "participant_removed";
    case HARNESS_EVENT_MESSAGE_APPENDED: return "message_appended";
    case HARNESS_EVENT_SECRET_REFERENCED: return "secret_referenced";
    case HARNESS_EVENT_TOOL_REGISTERED: return "tool_registered";
    case HARNESS_EVENT_TOOL_ENUMERATED: return "tool_enumerated";
    case HARNESS_EVENT_SOUL_SET: return "soul_set";
    case HARNESS_EVENT_CONTEXT_READY: return "context_ready";
    case HARNESS_EVENT_RESPONSE_COMPLETED: return "response_completed";
    case HARNESS_EVENT_RESPONSE_REQUIRES_ACTION: return "response_requires_action";
    case HARNESS_EVENT_RESPONSE_ERROR: return "response_error";
    case HARNESS_EVENT_LOOP_DECISION: return "loop_decision";
    case HARNESS_EVENT_INTERACTION_LOGGED: return "interaction_logged";
    case HARNESS_EVENT_VECTOR_CLASSIFIED: return "vector_classified";
    case HARNESS_EVENT_HONCHO_MIRRORED: return "honcho_mirrored";
    case HARNESS_EVENT_HONCHO_REQUEST_READY: return "honcho_request_ready";
    case HARNESS_EVENT_HONCHO_RESPONSE_PARSED: return "honcho_response_parsed";
    case HARNESS_EVENT_HISTORY_READY: return "history_ready";
    case HARNESS_EVENT_STREAM_FINISHED: return "stream_finished";
    case HARNESS_EVENT_PARTICIPANT_MUTED: return "participant_muted";
    case HARNESS_EVENT_HISTORY_COMPRESSED: return "history_compressed";
    case HARNESS_EVENT_EXTENSION_CALLED: return "extension_called";
    case HARNESS_EVENT_ERROR: return "error";
    default: return "unknown";
    }
}

bool harness_should_loop(harness_ctx_t* ctx, const char* criteria) {
    int lua_bool = -1;
    if (!ctx || !criteria) return false;
    ctx->state = HARNESS_STATE_LOOPING;
    if (strcmp(criteria, "false") == 0 || strcmp(criteria, "0") == 0 ||
        strcmp(criteria, "never") == 0) {
        harness_emit(ctx, HARNESS_EVENT_LOOP_DECISION, NULL, NULL, 0, 0);
        return false;
    }
    if (strcmp(criteria, "true") == 0 || strcmp(criteria, "1") == 0 ||
        strcmp(criteria, "always") == 0) {
        harness_emit(ctx, HARNESS_EVENT_LOOP_DECISION, NULL, NULL, 1, 0);
        return true;
    }
    /* Lua expression / registered criterion when available */
    if (harness_lua_eval_loop(ctx, criteria, &lua_bool) == 0) {
        harness_emit(ctx, HARNESS_EVENT_LOOP_DECISION, NULL, NULL, lua_bool ? 1 : 0, 0);
        return lua_bool != 0;
    }
    /* Unknown criteria default to continue once (policy can refine) */
    harness_emit(ctx, HARNESS_EVENT_LOOP_DECISION, NULL, NULL, 1, 0);
    return true;
}

/* declared in harness_extra.c */
void harness_log_record_push(harness_ctx_t* ctx, const char* model);

int harness_log_interaction(harness_ctx_t* ctx,
                            const char* model,
                            const char* prompt,
                            const char* response) {
    (void)prompt;
    (void)response;
    if (!ctx || !model) return -1;
    ctx->interactions_logged++;
    ctx->state = HARNESS_STATE_LOGGING;
    harness_log_record_push(ctx, model);
    harness_emit(ctx, HARNESS_EVENT_INTERACTION_LOGGED, NULL, NULL, 0,
                 (size_t)ctx->interactions_logged);
    return 0;
}

int harness_classify_vector(harness_ctx_t* ctx,
                            const char* data,
                            const char* collection) {
    (void)collection;
    if (!ctx || !data) return -1;
    ctx->state = HARNESS_STATE_VECTOR_OP;
    harness_emit(ctx, HARNESS_EVENT_VECTOR_CLASSIFIED, NULL, NULL, 0, 0);
    return 0;
}

int harness_honcho_store_memory(harness_ctx_t* ctx,
                                const char* peer_id,
                                const char* key,
                                const char* fact) {
    size_t i;
    if (!ctx || !key || !fact) return -1;
    for (i = 0; i < HARNESS_MEMORY_SLOTS; i++) {
        if (ctx->memory[i].in_use && strcmp(ctx->memory[i].key, key) == 0 &&
            ((peer_id && strcmp(ctx->memory[i].peer_id, peer_id) == 0) ||
             (!peer_id && ctx->memory[i].peer_id[0] == '\0'))) {
            harness_copy_id(ctx->memory[i].value, sizeof(ctx->memory[i].value), fact);
            return 0;
        }
    }
    for (i = 0; i < HARNESS_MEMORY_SLOTS; i++) {
        if (!ctx->memory[i].in_use) {
            ctx->memory[i].in_use = true;
            harness_copy_id(ctx->memory[i].key, sizeof(ctx->memory[i].key), key);
            harness_copy_id(ctx->memory[i].peer_id, sizeof(ctx->memory[i].peer_id),
                            peer_id ? peer_id : "");
            harness_copy_id(ctx->memory[i].value, sizeof(ctx->memory[i].value), fact);
            return 0;
        }
    }
    return -1;
}

const char* harness_honcho_get_memory(harness_ctx_t* ctx,
                                      const char* peer_id,
                                      const char* key) {
    size_t i;
    if (!ctx || !key) return NULL;
    for (i = 0; i < HARNESS_MEMORY_SLOTS; i++) {
        if (!ctx->memory[i].in_use) continue;
        if (strcmp(ctx->memory[i].key, key) != 0) continue;
        if (peer_id && peer_id[0] && strcmp(ctx->memory[i].peer_id, peer_id) != 0) continue;
        return ctx->memory[i].value;
    }
    return NULL;
}

int harness_register_extension(harness_ctx_t* ctx, const char* name, harness_extension_fn fn) {
    if (!ctx || !name || !fn || ctx->ext_count >= HARNESS_EXT_MAX) return -1;
    harness_copy_id(ctx->extensions[ctx->ext_count].name, sizeof(ctx->extensions[0].name), name);
    ctx->extensions[ctx->ext_count].fn = fn;
    ctx->ext_count++;
    harness_emit(ctx, HARNESS_EVENT_EXTENSION_CALLED, NULL, NULL, 0, ctx->ext_count - 1);
    return 0;
}

const char* harness_version(void) {
    return "0.4.0-todo-impl";
}

/* ---- Compatibility wrappers ---- */

int harness_lua_enumerate_tools(harness_ctx_t* ctx) {
    return harness_tools_enumerate(ctx);
}

int harness_lua_set_personality(harness_ctx_t* ctx, const char* personality_json_or_id) {
    return harness_soul_set(ctx, personality_json_or_id);
}

int harness_lua_process_openai_compat(harness_ctx_t* ctx, const char* request_json) {
    if (!ctx || !request_json) return -1;
    if (harness_set_output(ctx, request_json, strlen(request_json)) != 0) return -1;
    harness_emit(ctx, HARNESS_EVENT_CONTEXT_READY, NULL, NULL, 0, 0);
    ctx->state = HARNESS_STATE_PROCESSING_OPENAI;
    return 0;
}

bool harness_lua_should_loop(harness_ctx_t* ctx, const char* criteria) {
    return harness_should_loop(ctx, criteria);
}

int harness_lua_log_interaction(harness_ctx_t* ctx, const char* model, const char* prompt, const char* response) {
    return harness_log_interaction(ctx, model, prompt, response);
}

int harness_lua_classify_vector(harness_ctx_t* ctx, const char* data, const char* collection) {
    return harness_classify_vector(ctx, data, collection);
}

int harness_lua_honcho_store(harness_ctx_t* ctx, const char* key, const char* value) {
    return harness_honcho_store_memory(ctx, ctx ? ctx->acting_peer_id : NULL, key, value);
}

const char* harness_lua_honcho_retrieve(harness_ctx_t* ctx, const char* key) {
    return harness_honcho_get_memory(ctx, ctx ? ctx->acting_peer_id : NULL, key);
}
