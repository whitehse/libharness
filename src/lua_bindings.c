/* Lua bindings — policy surface for tools, SOUL, loop, Honcho mirror rules.
 * Public consumers never need lua.h; harness_lua_init takes void*.
 */
#include "harness_internal.h"
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LUA
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

static harness_ctx_t* l_ctx(lua_State* L) {
    return (harness_ctx_t*)lua_touserdata(L, lua_upvalueindex(1));
}

static int l_enumerate_tools(lua_State* L) {
    lua_pushinteger(L, harness_tools_enumerate(l_ctx(L)));
    return 1;
}

static int l_set_soul(lua_State* L) {
    lua_pushinteger(L, harness_soul_set(l_ctx(L), luaL_checkstring(L, 1)));
    return 1;
}

static int l_should_loop(lua_State* L) {
    lua_pushboolean(L, harness_should_loop(l_ctx(L), luaL_checkstring(L, 1)) ? 1 : 0);
    return 1;
}

static int l_participant_add(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    const char* peer = luaL_checkstring(L, 1);
    int kind = (int)luaL_optinteger(L, 2, HARNESS_PARTICIPANT_HUMAN);
    int priv = lua_toboolean(L, 3);
    lua_pushinteger(L, harness_participant_add(ctx, peer, (harness_participant_kind_t)kind, priv != 0));
    return 1;
}

static int l_message_append(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    const char* peer = luaL_checkstring(L, 1);
    int role = (int)luaL_checkinteger(L, 2);
    const char* content = luaL_checkstring(L, 3);
    int secret = lua_toboolean(L, 4);
    lua_pushinteger(L, harness_message_append(ctx, peer, (harness_message_role_t)role, content, secret != 0));
    return 1;
}

static int l_context_build(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    harness_context_params_t params;
    memset(&params, 0, sizeof(params));
    params.model = luaL_optstring(L, 1, "gpt-4o");
    params.temperature = luaL_optnumber(L, 2, 0.0);
    params.include_tools = true;
    params.identity_prefix = true;
    params.redact_secrets = true;
    lua_pushinteger(L, harness_context_build(ctx, &params));
    return 1;
}

static int l_honcho_mirror(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    lua_pushinteger(L, harness_honcho_mirror_message(ctx, luaL_checkstring(L, 1),
                                                     luaL_checkstring(L, 2),
                                                     luaL_optstring(L, 3, NULL)));
    return 1;
}

static int l_session_set(lua_State* L) {
    lua_pushinteger(L, harness_session_set(l_ctx(L), luaL_checkstring(L, 1), luaL_checkstring(L, 2)));
    return 1;
}

static int l_response_parse(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    lua_pushinteger(L, harness_response_parse(l_ctx(L), (const uint8_t*)data, len));
    return 1;
}

static int l_log_interaction(lua_State* L) {
    lua_pushinteger(L, harness_log_interaction(l_ctx(L), luaL_checkstring(L, 1),
                                               luaL_optstring(L, 2, ""),
                                               luaL_optstring(L, 3, "")));
    return 1;
}

static int l_tool_register_json(lua_State* L) {
    lua_pushinteger(L, harness_tool_register_json(l_ctx(L), luaL_checkstring(L, 1)));
    return 1;
}

static int l_history_compress(lua_State* L) {
    lua_pushinteger(L, harness_history_compress(l_ctx(L), (size_t)luaL_optinteger(L, 1, 16)));
    return 1;
}

static int l_set_capabilities(lua_State* L) {
    lua_pushinteger(L, harness_participant_set_capabilities(
        l_ctx(L), luaL_checkstring(L, 1), (uint32_t)luaL_checkinteger(L, 2)));
    return 1;
}

static int l_soul_for_kind(lua_State* L) {
    lua_pushinteger(L, harness_soul_set_for_kind(
        l_ctx(L), (harness_participant_kind_t)luaL_checkinteger(L, 1),
        luaL_checkstring(L, 2)));
    return 1;
}

/* register_tool(name, schema_json, fn) */
static int l_register_tool(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    const char* name = luaL_checkstring(L, 1);
    const char* schema = luaL_optstring(L, 2, "{}");
    size_t i;
    char tool_json[HARNESS_TOOL_JSON_MAX];
    size_t used = 0;

    luaL_checktype(L, 3, LUA_TFUNCTION);
    if (ctx->lua_tool_count >= HARNESS_LUA_TOOL_MAX)
        return luaL_error(L, "too many lua tools");

    /* Build function tool JSON and register for context */
    if (harness_json_append_raw(tool_json, sizeof(tool_json), &used,
            "{\"type\":\"function\",\"function\":{\"name\":\"") != 0)
        return luaL_error(L, "tool json overflow");
    if (harness_json_escape_append(tool_json, sizeof(tool_json), &used, name) != 0)
        return luaL_error(L, "tool json overflow");
    if (harness_json_append_raw(tool_json, sizeof(tool_json), &used,
            "\",\"parameters\":") != 0)
        return luaL_error(L, "tool json overflow");
    if (harness_json_append_raw(tool_json, sizeof(tool_json), &used, schema) != 0)
        return luaL_error(L, "tool json overflow");
    if (harness_json_append_raw(tool_json, sizeof(tool_json), &used, "}}") != 0)
        return luaL_error(L, "tool json overflow");
    tool_json[used] = '\0';
    if (harness_tool_register_json(ctx, tool_json) != 0)
        return luaL_error(L, "tool_register_json failed");

    /* Store function ref */
    for (i = 0; i < HARNESS_LUA_TOOL_MAX; i++) {
        if (!ctx->lua_tools[i].in_use) {
            harness_copy_id(ctx->lua_tools[i].name, sizeof(ctx->lua_tools[i].name), name);
            lua_pushvalue(L, 3);
            ctx->lua_tools[i].ref = luaL_ref(L, LUA_REGISTRYINDEX);
            ctx->lua_tools[i].in_use = true;
            ctx->lua_tool_count++;
            lua_pushinteger(L, 0);
            return 1;
        }
    }
    return luaL_error(L, "no tool slot");
}

/* set_should_mirror(fn) — fn(content) -> bool */
static int l_set_should_mirror(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (ctx->lua_should_mirror_ref != HARNESS_LUA_NOREF && ctx->lua_should_mirror_ref >= 0)
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->lua_should_mirror_ref);
    lua_pushvalue(L, 1);
    ctx->lua_should_mirror_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushinteger(L, 0);
    return 1;
}

/* set_loop_criterion(fn) — fn(criteria_string) -> bool */
static int l_set_loop_criterion(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (ctx->lua_loop_criterion_ref != HARNESS_LUA_NOREF && ctx->lua_loop_criterion_ref >= 0)
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->lua_loop_criterion_ref);
    lua_pushvalue(L, 1);
    ctx->lua_loop_criterion_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushinteger(L, 0);
    return 1;
}

static int l_invoke_tool(lua_State* L) {
    char result[4096];
    size_t out_len = 0;
    int rc = harness_tool_policy_invoke(l_ctx(L), luaL_checkstring(L, 1),
                                        luaL_optstring(L, 2, "{}"),
                                        result, sizeof(result), &out_len);
    if (rc != 0) {
        lua_pushnil(L);
        lua_pushinteger(L, rc);
        return 2;
    }
    lua_pushlstring(L, result, out_len);
    return 1;
}

/* next_event() -> table {type, peer_id, call_id, code, index, detail} or nil if none.
 * Coroutine-friendly: policy coroutines can yield when type is "none". */
static int l_next_event(lua_State* L) {
    harness_event_t ev;
    if (harness_next_event(l_ctx(L), &ev) != 0) {
        lua_pushnil(L);
        return 1;
    }
    if (ev.type == HARNESS_EVENT_NONE) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 6);
    lua_pushstring(L, harness_event_type_name(ev.type));
    lua_setfield(L, -2, "type");
    lua_pushstring(L, ev.peer_id);
    lua_setfield(L, -2, "peer_id");
    lua_pushstring(L, ev.call_id);
    lua_setfield(L, -2, "call_id");
    lua_pushinteger(L, ev.code);
    lua_setfield(L, -2, "code");
    lua_pushinteger(L, (lua_Integer)ev.index);
    lua_setfield(L, -2, "index");
    lua_pushstring(L, ev.detail);
    lua_setfield(L, -2, "detail");
    return 1;
}

/* drain_events() -> array of event tables (empty table if none). */
static int l_drain_events(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    int n = 0;
    lua_newtable(L);
    for (;;) {
        harness_event_t ev;
        if (harness_next_event(ctx, &ev) != 0) break;
        if (ev.type == HARNESS_EVENT_NONE) break;
        n++;
        lua_createtable(L, 0, 6);
        lua_pushstring(L, harness_event_type_name(ev.type));
        lua_setfield(L, -2, "type");
        lua_pushstring(L, ev.peer_id);
        lua_setfield(L, -2, "peer_id");
        lua_pushstring(L, ev.call_id);
        lua_setfield(L, -2, "call_id");
        lua_pushinteger(L, ev.code);
        lua_setfield(L, -2, "code");
        lua_pushinteger(L, (lua_Integer)ev.index);
        lua_setfield(L, -2, "index");
        lua_pushstring(L, ev.detail);
        lua_setfield(L, -2, "detail");
        lua_rawseti(L, -2, n);
    }
    return 1;
}

static int l_pique_feed_session(lua_State* L) {
    lua_pushinteger(L, harness_pique_feed_session(l_ctx(L)));
    return 1;
}

static int l_pique_feed_log(lua_State* L) {
    lua_pushinteger(L, harness_pique_feed_log(l_ctx(L),
                                              luaL_checkstring(L, 1),
                                              luaL_optstring(L, 2, ""),
                                              luaL_optstring(L, 3, "")));
    return 1;
}

static int l_pique_feed_embedding(lua_State* L) {
    lua_pushinteger(L, harness_pique_feed_embedding(l_ctx(L),
                                                    luaL_checkstring(L, 1),
                                                    luaL_checkstring(L, 2),
                                                    luaL_checkstring(L, 3)));
    return 1;
}

static int l_pique_feed_similarity(lua_State* L) {
    lua_pushinteger(L, harness_pique_feed_similarity(l_ctx(L),
                                                     luaL_checkstring(L, 1),
                                                     luaL_checkstring(L, 2),
                                                     (size_t)luaL_optinteger(L, 3, 8)));
    return 1;
}

static int l_pique_parse_similarity_tsv(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    lua_pushinteger(L, harness_pique_parse_similarity_tsv(l_ctx(L), data, len));
    return 1;
}

static int l_history_compress_select(lua_State* L) {
    /* history_compress_select(keep_table) — keep_table[i]=truthy keeps message i (1-based). */
    harness_ctx_t* ctx = l_ctx(L);
    size_t mc;
    size_t i;
    uint8_t* mask;
    int rc;
    luaL_checktype(L, 1, LUA_TTABLE);
    mc = harness_message_count(ctx);
    mask = (uint8_t*)malloc(mc ? mc : 1);
    if (!mask) {
        lua_pushinteger(L, -1);
        return 1;
    }
    for (i = 0; i < mc; i++) {
        lua_rawgeti(L, 1, (lua_Integer)(i + 1));
        mask[i] = lua_toboolean(L, -1) ? 1 : 0;
        lua_pop(L, 1);
    }
    rc = harness_history_compress_select(ctx, mask, mc);
    free(mask);
    lua_pushinteger(L, rc);
    return 1;
}

/* Install optional yield helpers on the global table after bind:
 * wait_event() — poll next; if none, hold a sentinel so coroutines can yield.
 * poll_until(type_name?) — drain until matching type (or any event) or nil.
 * Higher-level Lua helpers always use coroutine.yield(nil) when empty so apps
 * can wrap: while true do local e = harness.wait_event() if e then ... else
 * coroutine.yield() end end
 */
static int l_wait_event(lua_State* L) {
    harness_event_t ev;
    if (harness_next_event(l_ctx(L), &ev) != 0 || ev.type == HARNESS_EVENT_NONE) {
        lua_pushnil(L);
        lua_pushstring(L, "would_yield");
        return 2;
    }
    lua_createtable(L, 0, 6);
    lua_pushstring(L, harness_event_type_name(ev.type));
    lua_setfield(L, -2, "type");
    lua_pushstring(L, ev.peer_id);
    lua_setfield(L, -2, "peer_id");
    lua_pushstring(L, ev.call_id);
    lua_setfield(L, -2, "call_id");
    lua_pushinteger(L, ev.code);
    lua_setfield(L, -2, "code");
    lua_pushinteger(L, (lua_Integer)ev.index);
    lua_setfield(L, -2, "index");
    lua_pushstring(L, ev.detail);
    lua_setfield(L, -2, "detail");
    return 1;
}

static int l_poll_until(lua_State* L) {
    const char* want = luaL_optstring(L, 1, NULL);
    int limit = (int)luaL_optinteger(L, 2, 64);
    int n = 0;
    while (n < limit) {
        harness_event_t ev;
        if (harness_next_event(l_ctx(L), &ev) != 0 || ev.type == HARNESS_EVENT_NONE) {
            lua_pushnil(L);
            lua_pushstring(L, "would_yield");
            return 2;
        }
        n++;
        if (!want || want[0] == '\0' ||
            strcmp(harness_event_type_name(ev.type), want) == 0) {
            lua_createtable(L, 0, 6);
            lua_pushstring(L, harness_event_type_name(ev.type));
            lua_setfield(L, -2, "type");
            lua_pushstring(L, ev.peer_id);
            lua_setfield(L, -2, "peer_id");
            lua_pushstring(L, ev.call_id);
            lua_setfield(L, -2, "call_id");
            lua_pushinteger(L, ev.code);
            lua_setfield(L, -2, "code");
            lua_pushinteger(L, (lua_Integer)ev.index);
            lua_setfield(L, -2, "index");
            lua_pushstring(L, ev.detail);
            lua_setfield(L, -2, "detail");
            return 1;
        }
    }
    lua_pushnil(L);
    lua_pushstring(L, "limit");
    return 2;
}

static void register_fn(lua_State* L, harness_ctx_t* ctx, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

static int harness_lua_bind(harness_ctx_t* ctx, lua_State* L) {
    if (!ctx || !L) return -1;
    ctx->lua_state = L;
    ctx->lua_should_mirror_ref = HARNESS_LUA_NOREF;
    ctx->lua_loop_criterion_ref = HARNESS_LUA_NOREF;
    lua_newtable(L);
    register_fn(L, ctx, "enumerate_tools", l_enumerate_tools);
    register_fn(L, ctx, "set_soul", l_set_soul);
    register_fn(L, ctx, "set_personality", l_set_soul);
    register_fn(L, ctx, "should_loop", l_should_loop);
    register_fn(L, ctx, "participant_add", l_participant_add);
    register_fn(L, ctx, "message_append", l_message_append);
    register_fn(L, ctx, "context_build", l_context_build);
    register_fn(L, ctx, "honcho_mirror", l_honcho_mirror);
    register_fn(L, ctx, "session_set", l_session_set);
    register_fn(L, ctx, "response_parse", l_response_parse);
    register_fn(L, ctx, "log_interaction", l_log_interaction);
    register_fn(L, ctx, "tool_register_json", l_tool_register_json);
    register_fn(L, ctx, "register_tool", l_register_tool);
    register_fn(L, ctx, "invoke_tool", l_invoke_tool);
    register_fn(L, ctx, "set_should_mirror", l_set_should_mirror);
    register_fn(L, ctx, "set_loop_criterion", l_set_loop_criterion);
    register_fn(L, ctx, "history_compress", l_history_compress);
    register_fn(L, ctx, "set_capabilities", l_set_capabilities);
    register_fn(L, ctx, "soul_for_kind", l_soul_for_kind);
    register_fn(L, ctx, "next_event", l_next_event);
    register_fn(L, ctx, "drain_events", l_drain_events);
    register_fn(L, ctx, "wait_event", l_wait_event);
    register_fn(L, ctx, "poll_until", l_poll_until);
    register_fn(L, ctx, "pique_feed_session", l_pique_feed_session);
    register_fn(L, ctx, "pique_feed_log", l_pique_feed_log);
    register_fn(L, ctx, "pique_feed_embedding", l_pique_feed_embedding);
    register_fn(L, ctx, "pique_feed_similarity", l_pique_feed_similarity);
    register_fn(L, ctx, "pique_parse_similarity_tsv", l_pique_parse_similarity_tsv);
    register_fn(L, ctx, "history_compress_select", l_history_compress_select);
    lua_setglobal(L, "harness");

    if (ctx->config.lua_init_script && ctx->config.lua_init_script[0]) {
        if (luaL_dofile(L, ctx->config.lua_init_script) != LUA_OK)
            return -1;
    }
    return 0;
}

int harness_lua_init(harness_ctx_t* ctx, void* lua_state) {
    if (!ctx) return -1;
    if (!lua_state) {
        ctx->lua_state = NULL;
        return 0;
    }
    return harness_lua_bind(ctx, (lua_State*)lua_state);
}

int harness_lua_load_script(harness_ctx_t* ctx, const char* source, size_t len) {
    lua_State* L;
    if (!ctx || !source) return -1;
    L = (lua_State*)ctx->lua_state;
    if (!L) return -1;
    if (luaL_loadbuffer(L, source, len ? len : strlen(source), "harness_script") != LUA_OK)
        return -1;
    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
        return -1;
    return 0;
}

int harness_tool_policy_invoke(harness_ctx_t* ctx,
                               const char* name,
                               const char* arguments_json,
                               char* result_buf,
                               size_t result_cap,
                               size_t* out_len) {
    lua_State* L;
    size_t i;
    size_t n;
    const char* s;
    if (!ctx || !name || !result_buf || result_cap == 0) return -1;
    L = (lua_State*)ctx->lua_state;
    if (!L) return -1;
    for (i = 0; i < HARNESS_LUA_TOOL_MAX; i++) {
        if (!ctx->lua_tools[i].in_use) continue;
        if (strcmp(ctx->lua_tools[i].name, name) != 0) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->lua_tools[i].ref);
        lua_pushstring(L, arguments_json ? arguments_json : "{}");
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            return -1;
        }
        s = lua_tolstring(L, -1, &n);
        if (!s) s = "";
        if (n >= result_cap) n = result_cap - 1;
        memcpy(result_buf, s, n);
        result_buf[n] = '\0';
        if (out_len) *out_len = n;
        lua_pop(L, 1);
        return 0;
    }
    return -1;
}

int harness_lua_eval_should_mirror(harness_ctx_t* ctx, const char* content, int* out_bool) {
    lua_State* L;
    if (!ctx || !out_bool) return -1;
    L = (lua_State*)ctx->lua_state;
    if (!L || ctx->lua_should_mirror_ref < 0) return -1;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->lua_should_mirror_ref);
    lua_pushstring(L, content ? content : "");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return -1;
    }
    *out_bool = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return 0;
}

int harness_lua_eval_loop(harness_ctx_t* ctx, const char* criteria, int* out_bool) {
    lua_State* L;
    char chunk[1024];
    if (!ctx || !criteria || !out_bool) return -1;
    L = (lua_State*)ctx->lua_state;
    if (!L) return -1;

    if (ctx->lua_loop_criterion_ref >= 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->lua_loop_criterion_ref);
        lua_pushstring(L, criteria);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            return -1;
        }
        *out_bool = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return 0;
    }

    /* Treat criteria as Lua expression: return (<criteria>) */
    if (strncmp(criteria, "return ", 7) == 0)
        snprintf(chunk, sizeof(chunk), "%s", criteria);
    else
        snprintf(chunk, sizeof(chunk), "return (%s)", criteria);
    if (luaL_loadstring(L, chunk) != LUA_OK) {
        lua_pop(L, 1);
        return -1;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return -1;
    }
    *out_bool = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return 0;
}

#else /* !HAVE_LUA */

int harness_lua_init(harness_ctx_t* ctx, void* lua_state) {
    if (!ctx) return -1;
    ctx->lua_state = lua_state;
    return 0;
}

int harness_lua_load_script(harness_ctx_t* ctx, const char* source, size_t len) {
    (void)ctx; (void)source; (void)len;
    return -1;
}

int harness_tool_policy_invoke(harness_ctx_t* ctx,
                               const char* name,
                               const char* arguments_json,
                               char* result_buf,
                               size_t result_cap,
                               size_t* out_len) {
    (void)ctx; (void)name; (void)arguments_json; (void)result_buf; (void)result_cap; (void)out_len;
    return -1;
}

int harness_lua_eval_should_mirror(harness_ctx_t* ctx, const char* content, int* out_bool) {
    (void)ctx; (void)content; (void)out_bool;
    return -1;
}

int harness_lua_eval_loop(harness_ctx_t* ctx, const char* criteria, int* out_bool) {
    (void)ctx; (void)criteria; (void)out_bool;
    return -1;
}

#endif /* HAVE_LUA */
