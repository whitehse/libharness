/* OpenAI-compatible context builder + response parser (ADR 002).
 * Builds/parses JSON only. Network transport stays in the caller.
 */
#include "harness_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char* msg_role_str(const harness_ctx_t* ctx, harness_message_role_t role) {
    if (role == HARNESS_MSG_DEVELOPER && ctx->config.map_developer_to_system)
        return "system";
    switch (role) {
    case HARNESS_MSG_SYSTEM: return "system";
    case HARNESS_MSG_DEVELOPER: return "developer";
    case HARNESS_MSG_USER: return "user";
    case HARNESS_MSG_ASSISTANT: return "assistant";
    case HARNESS_MSG_TOOL: return "tool";
    default: return "user";
    }
}

static bool acting_peer_privileged(const harness_ctx_t* ctx) {
    const harness_participant_slot_t* p;
    if (!ctx || ctx->acting_peer_id[0] == '\0') return true;
    p = harness_find_participant(ctx, ctx->acting_peer_id);
    if (!p) return false;
    return p->privileged;
}

static int append_tool_calls_json(char* buf, size_t cap, size_t* used,
                                  const harness_message_slot_t* msg) {
    size_t t;
    if (msg->tool_call_count == 0) return 0;
    if (harness_json_append_raw(buf, cap, used, ",\"tool_calls\":[") != 0) return -1;
    for (t = 0; t < msg->tool_call_count; t++) {
        const harness_embedded_tool_call_t* tc = &msg->tool_calls[t];
        if (t > 0 && harness_json_append_raw(buf, cap, used, ",") != 0) return -1;
        if (harness_json_append_raw(buf, cap, used, "{\"id\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, used, tc->id) != 0) return -1;
        if (harness_json_append_raw(buf, cap, used,
                "\",\"type\":\"function\",\"function\":{\"name\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, used, tc->name) != 0) return -1;
        if (harness_json_append_raw(buf, cap, used, "\",\"arguments\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, used, tc->arguments) != 0) return -1;
        if (harness_json_append_raw(buf, cap, used, "\"}}") != 0) return -1;
    }
    if (harness_json_append_raw(buf, cap, used, "]") != 0) return -1;
    return 0;
}

int harness_openai_context_build_impl(harness_ctx_t* ctx, const harness_context_params_t* params) {
    char* buf;
    size_t cap;
    size_t used = 0;
    size_t i;
    bool prefix = params->identity_prefix;
    bool redact = params->redact_secrets && !acting_peer_privileged(ctx);
    char temp[HARNESS_MSG_CONTENT_MAX + HARNESS_PEER_ID_MAX + 16];
    char secret_ref[64];
    bool first_msg = true;

    if (!ctx || !params) return -1;
    cap = ctx->config.max_context_bytes ? ctx->config.max_context_bytes
                                        : HARNESS_DEFAULT_CONTEXT_CAP;
    if (cap < 256) cap = 256;

    buf = (char*)malloc(cap);
    if (!buf) return -1;

    if (harness_json_append_raw(buf, cap, &used, "{") != 0) goto fail;

    if (params->model && params->model[0]) {
        if (harness_json_append_raw(buf, cap, &used, "\"model\":\"") != 0) goto fail;
        if (harness_json_escape_append(buf, cap, &used, params->model) != 0) goto fail;
        if (harness_json_append_raw(buf, cap, &used, "\",") != 0) goto fail;
    }

    if (params->temperature >= 0.0) {
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "\"temperature\":%.4g,", params->temperature);
        if (harness_json_append_raw(buf, cap, &used, tbuf) != 0) goto fail;
    }

    if (harness_json_append_raw(buf, cap, &used, "\"messages\":[") != 0) goto fail;

    if (ctx->soul[0] != '\0') {
        if (harness_json_append_raw(buf, cap, &used, "{\"role\":\"system\",\"content\":\"") != 0)
            goto fail;
        if (harness_json_escape_append(buf, cap, &used, ctx->soul) != 0) goto fail;
        if (harness_json_append_raw(buf, cap, &used, "\"}") != 0) goto fail;
        first_msg = false;
    }

    for (i = 0; i < ctx->message_count; i++) {
        const harness_message_slot_t* msg = &ctx->messages[i];
        const char* body = msg->content;
        bool content_null = false;

        if (!first_msg) {
            if (harness_json_append_raw(buf, cap, &used, ",") != 0) goto fail;
        }
        first_msg = false;

        if (harness_json_append_raw(buf, cap, &used, "{\"role\":\"") != 0) goto fail;
        if (harness_json_append_raw(buf, cap, &used, msg_role_str(ctx, msg->role)) != 0)
            goto fail;
        if (harness_json_append_raw(buf, cap, &used, "\"") != 0) goto fail;

        if (msg->role == HARNESS_MSG_TOOL && msg->tool_call_id[0]) {
            if (harness_json_append_raw(buf, cap, &used, ",\"tool_call_id\":\"") != 0)
                goto fail;
            if (harness_json_escape_append(buf, cap, &used, msg->tool_call_id) != 0) goto fail;
            if (harness_json_append_raw(buf, cap, &used, "\"") != 0) goto fail;
        }

        if (msg->is_secret && redact) {
            snprintf(secret_ref, sizeof(secret_ref), "[secret:ref_%u]",
                     (unsigned)msg->secret_ref_id);
            body = secret_ref;
            harness_emit(ctx, HARNESS_EVENT_SECRET_REFERENCED, msg->peer_id, NULL,
                         (int)msg->secret_ref_id, i);
        }

        temp[0] = '\0';
        if (prefix && msg->peer_id[0] != '\0' &&
            msg->role != HARNESS_MSG_SYSTEM && msg->role != HARNESS_MSG_DEVELOPER &&
            msg->role != HARNESS_MSG_TOOL &&
            !(msg->role == HARNESS_MSG_ASSISTANT && msg->tool_call_count > 0 && body[0] == '\0')) {
            snprintf(temp, sizeof(temp), "[%s]: %s", msg->peer_id, body);
            body = temp;
        }

        /* Assistant with only tool_calls: content null */
        if (msg->role == HARNESS_MSG_ASSISTANT && msg->tool_call_count > 0 &&
            (body[0] == '\0')) {
            content_null = true;
        }

        if (content_null) {
            if (harness_json_append_raw(buf, cap, &used, ",\"content\":null") != 0) goto fail;
        } else {
            if (harness_json_append_raw(buf, cap, &used, ",\"content\":\"") != 0) goto fail;
            if (harness_json_escape_append(buf, cap, &used, body) != 0) goto fail;
            if (harness_json_append_raw(buf, cap, &used, "\"") != 0) goto fail;
        }

        if (msg->role == HARNESS_MSG_ASSISTANT) {
            if (append_tool_calls_json(buf, cap, &used, msg) != 0) goto fail;
        }

        if (harness_json_append_raw(buf, cap, &used, "}") != 0) goto fail;
    }

    if (harness_json_append_raw(buf, cap, &used, "]") != 0) goto fail;

    if (params->include_tools && ctx->tool_count > 0) {
        if (harness_json_append_raw(buf, cap, &used, ",\"tools\":[") != 0) goto fail;
        for (i = 0; i < ctx->tool_count; i++) {
            if (i > 0 && harness_json_append_raw(buf, cap, &used, ",") != 0) goto fail;
            if (harness_json_append_raw(buf, cap, &used, ctx->tools[i].json) != 0) goto fail;
        }
        if (harness_json_append_raw(buf, cap, &used, "]") != 0) goto fail;
    }

    if (harness_json_append_raw(buf, cap, &used, "}") != 0) goto fail;
    buf[used] = '\0';

    if (harness_set_output(ctx, buf, used) != 0) goto fail;
    free(buf);
    harness_emit(ctx, HARNESS_EVENT_CONTEXT_READY, ctx->acting_peer_id, NULL, 0, used);
    ctx->state = HARNESS_STATE_READY;
    return 0;

fail:
    free(buf);
    harness_emit(ctx, HARNESS_EVENT_ERROR, NULL, NULL, -1, 0);
    ctx->state = HARNESS_STATE_ERROR;
    return -1;
}

/* ---- lightweight JSON field extractors (no full parser) ---- */

static const char* find_key(const char* json, const char* key) {
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(json, pattern);
}

static int extract_string_after_key(const char* json, const char* key,
                                    char* out, size_t out_cap) {
    const char* p = find_key(json, key);
    const char* q;
    size_t n;
    if (!p || !out || out_cap == 0) return -1;
    /* Move past "key" */
    p += strlen(key) + 2;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p != ':') return -1;
    p++;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p != '"') return -1;
    p++; /* first character of string value */
    q = p;
    while (*q && *q != '"') {
        if (*q == '\\' && q[1]) q += 2;
        else q++;
    }
    if (*q != '"') return -1;
    n = (size_t)(q - p);
    if (n >= out_cap) n = out_cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

static uint32_t extract_u32_after_key(const char* json, const char* key) {
    const char* p = find_key(json, key);
    if (!p) return 0;
    p = strchr(p + strlen(key) + 2, ':');
    if (!p) return 0;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    return (uint32_t)strtoul(p, NULL, 10);
}

static void clear_parse_state(harness_ctx_t* ctx) {
    ctx->last_tool_call_count = 0;
    memset(ctx->parsed_tool_calls, 0, sizeof(ctx->parsed_tool_calls));
    memset(&ctx->last_usage, 0, sizeof(ctx->last_usage));
    ctx->last_assistant_text[0] = '\0';
    ctx->last_response_status = HARNESS_RESPONSE_UNKNOWN;
}

static void parse_usage(harness_ctx_t* ctx, const char* json) {
    ctx->last_usage.prompt_tokens = extract_u32_after_key(json, "prompt_tokens");
    ctx->last_usage.completion_tokens = extract_u32_after_key(json, "completion_tokens");
    ctx->last_usage.total_tokens = extract_u32_after_key(json, "total_tokens");
    if (ctx->last_usage.total_tokens == 0 &&
        (ctx->last_usage.prompt_tokens || ctx->last_usage.completion_tokens)) {
        ctx->last_usage.total_tokens =
            ctx->last_usage.prompt_tokens + ctx->last_usage.completion_tokens;
    }
}

static int push_tool_call(harness_ctx_t* ctx, const char* id, const char* name,
                          const char* args, const char* type) {
    harness_tool_call_t* tc;
    if (ctx->last_tool_call_count >= HARNESS_PARSED_TOOL_CALLS_MAX) return -1;
    tc = &ctx->parsed_tool_calls[ctx->last_tool_call_count];
    memset(tc, 0, sizeof(*tc));
    harness_copy_id(tc->id, sizeof(tc->id), id ? id : "");
    harness_copy_id(tc->name, sizeof(tc->name), name ? name : "");
    harness_copy_id(tc->arguments, sizeof(tc->arguments), args ? args : "{}");
    harness_copy_id(tc->type, sizeof(tc->type), type ? type : "function");
    ctx->last_tool_call_count++;
    return 0;
}

/* Extract tool_calls from Chat Completions style or Responses API style. */
static void parse_tool_calls(harness_ctx_t* ctx, const char* json) {
    const char* p = json;
    /* Pattern: "id":"call_..." then later "name":"..." and "arguments":"..." or object */
    while (ctx->last_tool_call_count < HARNESS_PARSED_TOOL_CALLS_MAX && p && *p) {
        const char* id_key = strstr(p, "\"id\"");
        const char* call_id_key = strstr(p, "\"call_id\"");
        const char* start;
        char id[HARNESS_CALL_ID_CAP];
        char name[HARNESS_TOOL_NAME_CAP];
        char args[HARNESS_TOOL_ARGS_CAP];
        const char* name_p;
        const char* args_p;
        const char* fn;

        id[0] = name[0] = args[0] = '\0';
        start = NULL;
        if (call_id_key && (!id_key || call_id_key < id_key)) {
            start = call_id_key;
            if (extract_string_after_key(start, "call_id", id, sizeof(id)) != 0) {
                p = start + 8;
                continue;
            }
        } else if (id_key) {
            start = id_key;
            if (extract_string_after_key(start, "id", id, sizeof(id)) != 0) {
                p = start + 4;
                continue;
            }
            /* skip non-call ids */
            if (strncmp(id, "call_", 5) != 0 && strncmp(id, "toolu_", 6) != 0 &&
                strstr(id, "call") == NULL) {
                p = start + 4;
                continue;
            }
        } else {
            break;
        }

        /* search forward for function name within next ~400 chars */
        fn = strstr(start, "\"function\"");
        name_p = start;
        if (fn && fn < start + 500) name_p = fn;
        if (extract_string_after_key(name_p, "name", name, sizeof(name)) != 0) {
            p = start + 4;
            continue;
        }

        args_p = name_p;
        if (extract_string_after_key(args_p, "arguments", args, sizeof(args)) != 0) {
            /* Responses API may use object arguments — store empty object */
            harness_copy_id(args, sizeof(args), "{}");
        }

        (void)push_tool_call(ctx, id, name, args, "function");
        p = start + 4;
    }
}

static void parse_assistant_text(harness_ctx_t* ctx, const char* json) {
    /* Prefer "content":"text" simple string; else nested "text":"..." */
    if (extract_string_after_key(json, "content", ctx->last_assistant_text,
                                 sizeof(ctx->last_assistant_text)) == 0) {
        if (strcmp(ctx->last_assistant_text, "null") == 0)
            ctx->last_assistant_text[0] = '\0';
        return;
    }
    (void)extract_string_after_key(json, "text", ctx->last_assistant_text,
                                   sizeof(ctx->last_assistant_text));
}

int harness_openai_response_parse_impl(harness_ctx_t* ctx, const uint8_t* data, size_t len) {
    char* tmp;
    int requires = 0;
    int is_error = 0;
    int incomplete = 0;

    if (!ctx || !data || len == 0) return -1;

    tmp = (char*)malloc(len + 1);
    if (!tmp) return -1;
    memcpy(tmp, data, len);
    tmp[len] = '\0';

    clear_parse_state(ctx);
    parse_usage(ctx, tmp);

    if (strstr(tmp, "\"error\"") != NULL && strstr(tmp, "\"message\"") != NULL)
        is_error = 1;
    if (strstr(tmp, "\"status\":\"incomplete\"") != NULL ||
        strstr(tmp, "\"status\": \"incomplete\"") != NULL)
        incomplete = 1;

    parse_tool_calls(ctx, tmp);
    parse_assistant_text(ctx, tmp);

    if (strstr(tmp, "requires_action") != NULL ||
        strstr(tmp, "\"tool_calls\"") != NULL ||
        strstr(tmp, "function_call") != NULL ||
        ctx->last_tool_call_count > 0) {
        requires = 1;
    }

    if (is_error) {
        ctx->last_response_status = HARNESS_RESPONSE_ERROR;
        ctx->state = HARNESS_STATE_ERROR;
        harness_emit(ctx, HARNESS_EVENT_RESPONSE_ERROR, ctx->acting_peer_id, NULL, -1, 0);
    } else if (incomplete && !requires) {
        ctx->last_response_status = HARNESS_RESPONSE_INCOMPLETE;
        ctx->state = HARNESS_STATE_READY;
        harness_emit(ctx, HARNESS_EVENT_RESPONSE_ERROR, ctx->acting_peer_id, NULL, 1, 0);
    } else if (requires) {
        if (ctx->last_tool_call_count == 0) {
            /* ensure at least a placeholder if heuristics fired without extract */
            (void)push_tool_call(ctx, "call_unknown", "unknown", "{}", "function");
        }
        ctx->last_response_status = HARNESS_RESPONSE_REQUIRES_ACTION;
        ctx->state = HARNESS_STATE_TOOL_CALL;
        harness_emit(ctx, HARNESS_EVENT_RESPONSE_REQUIRES_ACTION, ctx->acting_peer_id,
                     ctx->parsed_tool_calls[0].id, 0, ctx->last_tool_call_count);
    } else {
        ctx->last_response_status = HARNESS_RESPONSE_COMPLETED;
        ctx->state = HARNESS_STATE_READY;
        harness_emit(ctx, HARNESS_EVENT_RESPONSE_COMPLETED, ctx->acting_peer_id, NULL, 0, 0);
    }

    (void)harness_set_output(ctx, data, len);
    free(tmp);
    return 0;
}
