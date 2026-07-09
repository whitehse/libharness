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
    harness_ctx_t* ctx = l_ctx(L);
    lua_pushinteger(L, harness_tools_enumerate(ctx));
    return 1;
}

static int l_set_soul(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    const char* soul = luaL_checkstring(L, 1);
    lua_pushinteger(L, harness_soul_set(ctx, soul));
    return 1;
}

static int l_should_loop(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    const char* crit = luaL_checkstring(L, 1);
    lua_pushboolean(L, harness_should_loop(ctx, crit) ? 1 : 0);
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
    const char* model = luaL_optstring(L, 1, "gpt-4o");
    double temp = luaL_optnumber(L, 2, 0.0);
    memset(&params, 0, sizeof(params));
    params.model = model;
    params.temperature = temp;
    params.include_tools = true;
    params.identity_prefix = true;
    params.redact_secrets = true;
    lua_pushinteger(L, harness_context_build(ctx, &params));
    return 1;
}

static int l_honcho_mirror(lua_State* L) {
    harness_ctx_t* ctx = l_ctx(L);
    const char* peer = luaL_checkstring(L, 1);
    const char* content = luaL_checkstring(L, 2);
    const char* meta = luaL_optstring(L, 3, NULL);
    lua_pushinteger(L, harness_honcho_mirror_message(ctx, peer, content, meta));
    return 1;
}

static void register_fn(lua_State* L, harness_ctx_t* ctx, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

static int harness_lua_bind(harness_ctx_t* ctx, lua_State* L) {
    if (!ctx || !L) return -1;
    ctx->lua_state = L;
    lua_newtable(L);
    register_fn(L, ctx, "enumerate_tools", l_enumerate_tools);
    register_fn(L, ctx, "set_soul", l_set_soul);
    register_fn(L, ctx, "set_personality", l_set_soul);
    register_fn(L, ctx, "should_loop", l_should_loop);
    register_fn(L, ctx, "participant_add", l_participant_add);
    register_fn(L, ctx, "message_append", l_message_append);
    register_fn(L, ctx, "context_build", l_context_build);
    register_fn(L, ctx, "honcho_mirror", l_honcho_mirror);
    lua_setglobal(L, "harness");

    if (ctx->config.lua_init_script && ctx->config.lua_init_script[0]) {
        if (luaL_dofile(L, ctx->config.lua_init_script) != LUA_OK) {
            return -1;
        }
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

#else /* !HAVE_LUA */

int harness_lua_init(harness_ctx_t* ctx, void* lua_state) {
    if (!ctx) return -1;
    ctx->lua_state = lua_state;
    return 0;
}

#endif /* HAVE_LUA */
