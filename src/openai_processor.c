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
    if (!ctx || ctx->acting_peer_id[0] == '\0') return true;
    return harness_peer_can_see_secrets(ctx, ctx->acting_peer_id);
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

    /* Kind-specific SOUL for acting peer (additional system message) */
    {
        const harness_participant_slot_t* ap =
            ctx->acting_peer_id[0] ? harness_find_participant(ctx, ctx->acting_peer_id) : NULL;
        if (ap && (int)ap->kind >= 0 && (int)ap->kind <= 2 &&
            ctx->soul_by_kind[ap->kind][0] != '\0') {
            if (!first_msg && harness_json_append_raw(buf, cap, &used, ",") != 0) goto fail;
            if (harness_json_append_raw(buf, cap, &used, "{\"role\":\"system\",\"content\":\"") != 0)
                goto fail;
            if (harness_json_escape_append(buf, cap, &used, ctx->soul_by_kind[ap->kind]) != 0)
                goto fail;
            if (harness_json_append_raw(buf, cap, &used, "\"}") != 0) goto fail;
            first_msg = false;
        }
    }

    for (i = 0; i < ctx->message_count; i++) {
        const harness_message_slot_t* msg = &ctx->messages[i];
        const char* body = msg->content;
        bool content_null = false;
        const harness_participant_slot_t* peer;

        /* Skip muted participants (keep tool results and system/developer) */
        if (msg->peer_id[0] && msg->role != HARNESS_MSG_SYSTEM &&
            msg->role != HARNESS_MSG_DEVELOPER && msg->role != HARNESS_MSG_TOOL) {
            peer = harness_find_participant(ctx, msg->peer_id);
            if (peer && peer->muted)
                continue;
        }

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
        if (!msg->content_is_parts && prefix && msg->peer_id[0] != '\0' &&
            msg->role != HARNESS_MSG_SYSTEM && msg->role != HARNESS_MSG_DEVELOPER &&
            msg->role != HARNESS_MSG_TOOL &&
            !(msg->role == HARNESS_MSG_ASSISTANT && msg->tool_call_count > 0 && body[0] == '\0')) {
            snprintf(temp, sizeof(temp), "[%s]: %s", msg->peer_id, body);
            body = temp;
        }

        /* Assistant with only tool_calls: content null */
        if (msg->role == HARNESS_MSG_ASSISTANT && msg->tool_call_count > 0 &&
            (body[0] == '\0') && !msg->content_is_parts) {
            content_null = true;
        }

        if (content_null) {
            if (harness_json_append_raw(buf, cap, &used, ",\"content\":null") != 0) goto fail;
        } else if (msg->content_is_parts && !(msg->is_secret && redact)) {
            if (harness_json_append_raw(buf, cap, &used, ",\"content\":") != 0) goto fail;
            if (harness_json_append_raw(buf, cap, &used, body) != 0) goto fail;
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
    const char* p = json;
    size_t klen;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    klen = strlen(pattern);
    while (p && *p) {
        const char* hit = strstr(p, pattern);
        const char* after;
        if (!hit) return NULL;
        after = hit + klen;
        while (*after && (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r'))
            after++;
        /* Require ':' after quoted name → treat as object key, not a value */
        if (*after == ':')
            return hit;
        p = hit + klen;
    }
    return NULL;
}

/* Copy JSON string contents with minimal unescape (\\ \" \/ n t). */
static void copy_json_string_contents(const char* p, const char* end,
                                      char* out, size_t out_cap) {
    size_t w = 0;
    while (p < end && w + 1 < out_cap) {
        if (*p == '\\' && p + 1 < end) {
            char n = p[1];
            if (n == '"' || n == '\\' || n == '/') {
                out[w++] = n;
                p += 2;
                continue;
            }
            if (n == 'n') { out[w++] = '\n'; p += 2; continue; }
            if (n == 't') { out[w++] = '\t'; p += 2; continue; }
            if (n == 'r') { out[w++] = '\r'; p += 2; continue; }
            /* drop unknown escape prefix, keep next char */
            p++;
            continue;
        }
        out[w++] = *p++;
    }
    out[w] = '\0';
}

static int extract_string_after_key(const char* json, const char* key,
                                    char* out, size_t out_cap) {
    const char* p = find_key(json, key);
    const char* q;
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
    copy_json_string_contents(p, q, out, out_cap);
    return 0;
}

/* Read "status":"…" or null-terminated key form into out. */
static int extract_status_string(const char* json, char* out, size_t out_cap) {
    return extract_string_after_key(json, "status", out, out_cap);
}

static int extract_finish_reason(const char* json, char* out, size_t out_cap) {
    if (extract_string_after_key(json, "finish_reason", out, out_cap) == 0)
        return 0;
    return extract_string_after_key(json, "stop_reason", out, out_cap);
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
    /* Chat Completions: prompt_tokens / completion_tokens
     * Responses API / Anthropic-ish: input_tokens / output_tokens */
    ctx->last_usage.prompt_tokens = extract_u32_after_key(json, "prompt_tokens");
    if (ctx->last_usage.prompt_tokens == 0)
        ctx->last_usage.prompt_tokens = extract_u32_after_key(json, "input_tokens");
    ctx->last_usage.completion_tokens = extract_u32_after_key(json, "completion_tokens");
    if (ctx->last_usage.completion_tokens == 0)
        ctx->last_usage.completion_tokens = extract_u32_after_key(json, "output_tokens");
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

/* Extract balanced {…} object starting at brace into out (truncated). */
static int copy_balanced_object(const char* brace, char* out, size_t out_cap) {
    int depth = 0;
    const char* e;
    size_t n;
    if (!brace || *brace != '{' || !out || out_cap == 0) return -1;
    e = brace;
    do {
        if (*e == '{') depth++;
        else if (*e == '}') depth--;
        else if (*e == '"' ) {
            e++;
            while (*e && *e != '"') {
                if (*e == '\\' && e[1]) e += 2;
                else e++;
            }
        }
        if (*e) e++;
    } while (*e && depth > 0);
    if (depth != 0) return -1;
    n = (size_t)(e - brace);
    if (n >= out_cap) n = out_cap - 1;
    memcpy(out, brace, n);
    out[n] = '\0';
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
        const char* type_fc;

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

        /* search forward for function name within next ~500 chars */
        fn = strstr(start, "\"function\"");
        type_fc = strstr(start, "\"function_call\"");
        name_p = start;
        if (fn && fn < start + 500) name_p = fn;
        else if (type_fc && type_fc < start + 500) name_p = type_fc;
        if (extract_string_after_key(name_p, "name", name, sizeof(name)) != 0) {
            /* Responses API may place "name" as sibling of call_id */
            if (extract_string_after_key(start, "name", name, sizeof(name)) != 0) {
                p = start + 4;
                continue;
            }
        }

        args_p = name_p;
        if (extract_string_after_key(args_p, "arguments", args, sizeof(args)) != 0) {
            /* Non-string arguments object: copy balanced braces */
            const char* ak = strstr(args_p, "\"arguments\"");
            args[0] = '\0';
            if (ak) {
                const char* brace = strchr(ak, '{');
                if (brace && copy_balanced_object(brace, args, sizeof(args)) == 0) {
                    /* ok */
                } else {
                    harness_copy_id(args, sizeof(args), "{}");
                }
            }
            if (!args[0])
                harness_copy_id(args, sizeof(args), "{}");
        }

        (void)push_tool_call(ctx, id, name, args, "function");
        p = start + 4;
    }
}

/* Prefer message-shaped assistant text over incidental other "content" keys. */
static void parse_assistant_text(harness_ctx_t* ctx, const char* json) {
    const char* msg;
    const char* choice_msg;
    char refusal[HARNESS_ASSISTANT_TEXT_CAP];

    ctx->last_assistant_text[0] = '\0';
    refusal[0] = '\0';

    /* Chat Completions: choices[…].message.content */
    choice_msg = strstr(json, "\"message\"");
    if (choice_msg) {
        if (extract_string_after_key(choice_msg, "content",
                ctx->last_assistant_text, sizeof(ctx->last_assistant_text)) == 0) {
            if (strcmp(ctx->last_assistant_text, "null") == 0)
                ctx->last_assistant_text[0] = '\0';
            if (ctx->last_assistant_text[0] != '\0')
                return;
        }
        if (extract_string_after_key(choice_msg, "refusal", refusal, sizeof(refusal)) == 0 &&
            refusal[0] && strcmp(refusal, "null") != 0) {
            harness_copy_id(ctx->last_assistant_text, sizeof(ctx->last_assistant_text),
                            refusal);
            return;
        }
    }

    /* Responses API: "type":"message" then nested "text":"..." */
    msg = strstr(json, "\"type\":\"message\"");
    if (!msg)
        msg = strstr(json, "\"type\": \"message\"");
    if (msg) {
        if (extract_string_after_key(msg, "text",
                ctx->last_assistant_text, sizeof(ctx->last_assistant_text)) == 0 &&
            ctx->last_assistant_text[0] != '\0')
            return;
    }

    /* Fallback: first top-ish "text" then "content" string */
    if (extract_string_after_key(json, "text", ctx->last_assistant_text,
                                 sizeof(ctx->last_assistant_text)) == 0) {
        if (strcmp(ctx->last_assistant_text, "null") == 0)
            ctx->last_assistant_text[0] = '\0';
        if (ctx->last_assistant_text[0] != '\0')
            return;
    }
    if (extract_string_after_key(json, "content", ctx->last_assistant_text,
                                 sizeof(ctx->last_assistant_text)) == 0) {
        if (strcmp(ctx->last_assistant_text, "null") == 0)
            ctx->last_assistant_text[0] = '\0';
    }
}

static int has_error_object(const char* json) {
    const char* err = strstr(json, "\"error\"");
    if (!err) return 0;
    /* Avoid matching "error" keys inside unrelated strings: require nearby object/colon */
    err = strchr(err, ':');
    if (!err) return 0;
    err++;
    while (*err && isspace((unsigned char)*err)) err++;
    if (*err == '{' || *err == '"') return 1;
    return 0;
}

int harness_openai_response_parse_impl(harness_ctx_t* ctx, const uint8_t* data, size_t len) {
    char* tmp;
    int requires = 0;
    int is_error = 0;
    int incomplete = 0;
    char status[64];
    char finish_reason[64];
    const char* shape_detail = "normalized";

    if (!ctx || !data || len == 0) return -1;

    tmp = (char*)malloc(len + 1);
    if (!tmp) return -1;
    memcpy(tmp, data, len);
    tmp[len] = '\0';

    clear_parse_state(ctx);
    status[0] = finish_reason[0] = '\0';
    (void)extract_status_string(tmp, status, sizeof(status));
    (void)extract_finish_reason(tmp, finish_reason, sizeof(finish_reason));
    parse_usage(ctx, tmp);

    if (has_error_object(tmp))
        is_error = 1;
    if (strcmp(status, "failed") == 0 || strcmp(status, "error") == 0)
        is_error = 1;
    if (strcmp(status, "incomplete") == 0 ||
        strstr(tmp, "\"status\":\"incomplete\"") != NULL ||
        strstr(tmp, "\"status\": \"incomplete\"") != NULL)
        incomplete = 1;

    parse_tool_calls(ctx, tmp);
    parse_assistant_text(ctx, tmp);

    /* Shape hint for event detail */
    if (strstr(tmp, "\"object\":\"chat.completion\"") != NULL ||
        strstr(tmp, "\"choices\"") != NULL)
        shape_detail = "chat_completions";
    else if (strstr(tmp, "\"object\":\"response\"") != NULL ||
             strstr(tmp, "\"output\"") != NULL)
        shape_detail = "responses_api";

    if (strcmp(status, "requires_action") == 0 ||
        strcmp(finish_reason, "tool_calls") == 0 ||
        strcmp(finish_reason, "tool_use") == 0 ||
        strstr(tmp, "\"tool_calls\"") != NULL ||
        strstr(tmp, "function_call") != NULL ||
        ctx->last_tool_call_count > 0) {
        requires = 1;
    }
    /* Explicit completed status without tool payload wins over false positives */
    if (strcmp(status, "completed") == 0 && ctx->last_tool_call_count == 0 &&
        strcmp(finish_reason, "tool_calls") != 0)
        requires = 0;
    if (strcmp(finish_reason, "stop") == 0 && ctx->last_tool_call_count == 0)
        requires = 0;

    if (is_error) {
        ctx->last_response_status = HARNESS_RESPONSE_ERROR;
        ctx->state = HARNESS_STATE_ERROR;
        harness_emit_ex(ctx, HARNESS_EVENT_RESPONSE_ERROR, ctx->acting_peer_id, NULL,
                        -1, 0, shape_detail);
    } else if (incomplete && !requires) {
        ctx->last_response_status = HARNESS_RESPONSE_INCOMPLETE;
        ctx->state = HARNESS_STATE_READY;
        harness_emit_ex(ctx, HARNESS_EVENT_RESPONSE_ERROR, ctx->acting_peer_id, NULL,
                        1, 0, "incomplete");
    } else if (requires) {
        if (ctx->last_tool_call_count == 0) {
            /* heuristics fired without extract — placeholder for loop policy */
            (void)push_tool_call(ctx, "call_unknown", "unknown", "{}", "function");
        }
        ctx->last_response_status = HARNESS_RESPONSE_REQUIRES_ACTION;
        ctx->state = HARNESS_STATE_TOOL_CALL;
        harness_emit_ex(ctx, HARNESS_EVENT_RESPONSE_REQUIRES_ACTION, ctx->acting_peer_id,
                        ctx->parsed_tool_calls[0].id, 0, ctx->last_tool_call_count,
                        shape_detail);
    } else {
        ctx->last_response_status = HARNESS_RESPONSE_COMPLETED;
        ctx->state = HARNESS_STATE_READY;
        harness_emit_ex(ctx, HARNESS_EVENT_RESPONSE_COMPLETED, ctx->acting_peer_id, NULL,
                        0, 0, shape_detail);
    }

    (void)harness_set_output(ctx, data, len);
    free(tmp);
    return 0;
}
