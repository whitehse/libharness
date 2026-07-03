#include "harness.h"
#ifdef HAVE_LUA
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#endif
#include <stdlib.h>

/* Lua bindings implementation: registers harness functions into a Lua state.
   The harness Lua interface allows scripts to define behavior, tools, loop criteria.
   Caller (or init script) creates lua_State and calls harness_lua_init(ctx, L).
*/

int harness_lua_init(harness_ctx_t* ctx, lua_State* L) {
    if (!ctx || !L) return -1;
    ctx->lua_state = L;

    /* Register C functions as Lua globals or in 'harness' table */
    lua_newtable(L);  /* harness table */

    /* Example: harness.enumerate_tools() */
    lua_pushcfunction(L, (lua_CFunction)harness_lua_enumerate_tools); /* but need wrapper */
    /* For simplicity in bootstrap, direct binding via lightuserdata or full wrapper later */

    lua_setglobal(L, "harness");

    /* Load any init script from config or PG */
    if (ctx->config.lua_init_script) {
        if (luaL_dofile(L, ctx->config.lua_init_script) != LUA_OK) {
            /* error handling */
            return -1;
        }
    }

    /* In real impl: expose full API:
       - harness.enumerate_tools()
       - harness.set_personality(json)
       - harness.process_openai(request)
       - harness.should_loop(criteria_lua_expr)
       - harness.log_interaction(...)
       - harness.classify_vector(data, collection)
       - harness.honcho.store/retrieve
       - harness.register_tool(name, lua_fn)
       - etc.
    */

    return 0;
}

/* Placeholder for full Lua C API wrappers. Real ones would use luaL_checkstring etc. */
static int l_enumerate_tools(lua_State* L) {
    harness_ctx_t* ctx = (harness_ctx_t*)lua_touserdata(L, lua_upvalueindex(1));
    int rc = harness_lua_enumerate_tools(ctx);
    lua_pushinteger(L, rc);
    return 1;
}

/* More wrappers would be added in full implementation for dynamic tools, personalities, etc. */