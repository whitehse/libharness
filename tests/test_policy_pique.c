#include "harness.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

int main(void) {
    harness_config_t cfg;
    harness_ctx_t* ctx;
    harness_context_params_t params;
    char sql[2048];
    char card[512];
    char concl[512];
    size_t n = 0;
    uint8_t out[4096];
    size_t out_len = 0;
    lua_State* L;
    char result[256];
    size_t rlen = 0;

    printf("policy/pique/lua test...\n");
    harness_config_init_defaults(&cfg);
    cfg.workspace_id = "ws";
    cfg.session_id = "sess";
    cfg.acting_peer_id = "agent_a";
    ctx = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg);
    assert(ctx);

    assert(harness_participant_add(ctx, "human_a", HARNESS_PARTICIPANT_HUMAN, false) == 0);
    assert(harness_participant_add(ctx, "agent_a", HARNESS_PARTICIPANT_AGENT, true) == 0);
    assert((harness_participant_get_capabilities(ctx, "agent_a") & HARNESS_CAP_SEE_SECRETS) != 0);
    assert(harness_participant_set_capabilities(ctx, "human_a",
        HARNESS_CAP_SEE_SECRETS | HARNESS_CAP_MIRROR_HONCHO) == 0);
    assert(harness_participant_get_capabilities(ctx, "human_a") & HARNESS_CAP_SEE_SECRETS);

    assert(harness_soul_set(ctx, "Base soul.") == 0);
    assert(harness_soul_set_for_kind(ctx, HARNESS_PARTICIPANT_AGENT, "Agent-kind soul.") == 0);
    assert(strcmp(harness_soul_get_for_kind(ctx, HARNESS_PARTICIPANT_AGENT), "Agent-kind soul.") == 0);

    assert(harness_message_append(ctx, "human_a", HARNESS_MSG_USER, "m1", false) == 0);
    assert(harness_message_append(ctx, "human_a", HARNESS_MSG_USER, "m2", false) == 0);
    assert(harness_message_append(ctx, "human_a", HARNESS_MSG_USER, "m3", false) == 0);
    assert(harness_message_append(ctx, "human_a", HARNESS_MSG_USER, "m4", false) == 0);
    assert(harness_history_compress(ctx, 2) == 0);
    assert(harness_message_count(ctx) == 2);
    printf("  capabilities + kind soul + compress: OK\n");

    memset(&params, 0, sizeof(params));
    params.model = "gpt";
    params.temperature = 0;
    params.include_tools = false;
    params.identity_prefix = true;
    params.redact_secrets = true;
    assert(harness_context_build(ctx, &params) == 0);
    assert(harness_get_output(ctx, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert(strstr((char*)out, "Agent-kind soul.") != NULL);
    printf("  kind soul in context: OK\n");

    assert(harness_pique_build_log_insert(ctx, "gpt", "p", "r", sql, sizeof(sql), &n) == 0);
    assert(strstr(sql, "INSERT INTO harness_interactions") != NULL);
    assert(strstr(sql, "sess") != NULL);
    assert(harness_pique_build_session_upsert(ctx, sql, sizeof(sql), &n) == 0);
    assert(strstr(sql, "harness_sessions") != NULL);
    printf("  pique SQL builders: OK\n");

    assert(harness_honcho_build_peer_card_request(ctx, "human_a", card, sizeof(card), &n) == 0);
    assert(strstr(card, "get_peer_card") != NULL);
    assert(harness_honcho_build_conclude_request(ctx, "human_a", "likes brevity",
                                                 concl, sizeof(concl), &n) == 0);
    assert(strstr(concl, "conclude") != NULL);
    printf("  honcho peer-card/conclude: OK\n");

    L = luaL_newstate();
    assert(L);
    luaL_openlibs(L);
    assert(harness_lua_init(ctx, L) == 0);
    assert(harness_lua_load_script(ctx,
        "harness.register_tool('echo', '{ \"type\": \"object\" }', function(args) return args end)\n"
        "harness.set_should_mirror(function(c) return not c:find('SECRET') end)\n", 0) == 0);
    assert(harness_tool_policy_invoke(ctx, "echo", "{\"x\":1}", result, sizeof(result), &rlen) == 0);
    assert(strstr(result, "x") != NULL);
    assert(harness_honcho_should_mirror(ctx, "hello") == true);
    assert(harness_honcho_should_mirror(ctx, "has SECRET") == false);
    /* Expression eval (no registered criterion yet) */
    assert(harness_should_loop(ctx, "1 + 1 == 2") == true);
    assert(harness_should_loop(ctx, "false") == false);
    assert(harness_lua_load_script(ctx,
        "harness.set_loop_criterion(function(c) return c == 'go' end)\n", 0) == 0);
    assert(harness_should_loop(ctx, "go") == true);
    assert(harness_should_loop(ctx, "stop") == false);
    printf("  lua register_tool + mirror/loop policy: OK\n");

    lua_close(L);
    harness_destroy(ctx);
    printf("policy/pique/lua test PASSED\n");
    return 0;
}
