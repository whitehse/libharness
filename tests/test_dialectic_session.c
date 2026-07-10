/* Dialectic-style multi-participant session tests (ADR 002 / TODO 7.1).
 * No network: two harness contexts share narrative via buffer handoff.
 */
#include "harness.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

static void drain(harness_ctx_t* ctx) {
    harness_event_t ev;
    while (harness_next_event(ctx, &ev) == 0 && ev.type != HARNESS_EVENT_NONE) {
    }
}

static void assert_strstr(const char* hay, const char* needle) {
    if (!hay || !needle || !strstr(hay, needle)) {
        fprintf(stderr, "missing substring: %s\nin: %s\n", needle, hay ? hay : "(null)");
        assert(0);
    }
}

int main(void) {
    harness_config_t cfg;
    harness_ctx_t* agent;
    harness_ctx_t* human_view;
    harness_context_params_t params;
    harness_tool_def_t tool;
    harness_tool_call_t tc;
    harness_usage_t usage;
    uint8_t out[16384];
    size_t out_len = 0;
    char tools_json[2048];
    size_t tools_len = 0;
    const char* chat_resp =
        "{\"id\":\"chatcmpl_1\",\"choices\":[{\"message\":{"
        "\"role\":\"assistant\",\"content\":null,"
        "\"tool_calls\":[{\"id\":\"call_stock_1\",\"type\":\"function\","
        "\"function\":{\"name\":\"get_stock_price\",\"arguments\":\"{\\\"ticker\\\":\\\"AAPL\\\"}\"}}]}"
        "}],\"usage\":{\"prompt_tokens\":12,\"completion_tokens\":8,\"total_tokens\":20}}";
    const char* final_resp =
        "{\"id\":\"chatcmpl_2\",\"choices\":[{\"message\":{"
        "\"role\":\"assistant\",\"content\":\"AAPL is trading near target.\"}}],"
        "\"usage\":{\"prompt_tokens\":30,\"completion_tokens\":10,\"total_tokens\":40}}";
    uint32_t secret_ref;

    printf("dialectic session test...\n");

    harness_config_init_defaults(&cfg);
    cfg.workspace_id = "ws_dial";
    cfg.session_id = "sess_dial";
    cfg.acting_peer_id = "agent_devbot";
    cfg.drop_oldest_messages = true;
    cfg.max_messages = 8;
    cfg.map_developer_to_system = true;

    agent = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg);
    assert(agent);

    human_view = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg);
    assert(human_view);

    assert(harness_participant_add(agent, "human_alice", HARNESS_PARTICIPANT_HUMAN, true) == 0);
    assert(harness_participant_add(agent, "human_bob", HARNESS_PARTICIPANT_HUMAN, false) == 0);
    assert(harness_participant_add(agent, "agent_devbot", HARNESS_PARTICIPANT_AGENT, true) == 0);

    assert(harness_soul_set(agent, "You are DevBot.") == 0);
    assert(harness_message_append(agent, NULL, HARNESS_MSG_DEVELOPER,
                                  "Prefer concise answers.", false) == 0);

    memset(&tool, 0, sizeof(tool));
    tool.name = "get_stock_price";
    tool.description = "Lookup ticker price";
    tool.parameters_json = "{\"type\":\"object\",\"properties\":{\"ticker\":{\"type\":\"string\"}},\"required\":[\"ticker\"]}";
    tool.type = HARNESS_TOOL_FUNCTION;
    assert(harness_tool_register(agent, &tool) == 0);
    assert(harness_tools_to_json(agent, tools_json, sizeof(tools_json), &tools_len) == 0);
    assert_strstr(tools_json, "get_stock_price");
    printf("  tool_register + tools_to_json: OK\n");

    assert(harness_message_append(agent, "human_alice", HARNESS_MSG_USER,
                                  "What is AAPL doing?", false) == 0);
    assert(harness_message_append(agent, "human_bob", HARNESS_MSG_USER,
                                  "api_token=sekrit-value", true) == 0);
    secret_ref = harness_message_secret_ref(agent, 2); /* developer + alice + bob? */
    /* messages: developer, alice, bob → bob is index 2 */
    assert(secret_ref != 0);
    /* rebuild twice — stable secret ref */
    assert(harness_message_secret_ref(agent, 2) == secret_ref);
    printf("  stable secret_ref: %u\n", (unsigned)secret_ref);

    memset(&params, 0, sizeof(params));
    params.model = "gpt-4o";
    params.temperature = 0.0;
    params.include_tools = true;
    params.identity_prefix = true;
    params.redact_secrets = true;

    assert(harness_set_acting_peer(agent, "human_bob") == 0);
    assert(harness_context_build(agent, &params) == 0);
    assert(harness_get_output(agent, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert_strstr((char*)out, "[human_alice]");
    assert_strstr((char*)out, "secret:ref_");
    assert(!strstr((char*)out, "sekrit-value"));
    assert_strstr((char*)out, "\"role\":\"system\""); /* developer mapped */
    assert_strstr((char*)out, "get_stock_price");
    printf("  context redaction + developer map: OK\n");

    assert(harness_set_acting_peer(agent, "agent_devbot") == 0);
    assert(harness_response_parse(agent, (const uint8_t*)chat_resp, strlen(chat_resp)) == 0);
    assert(harness_response_status(agent) == HARNESS_RESPONSE_REQUIRES_ACTION);
    assert(harness_response_tool_call_count(agent) >= 1);
    assert(harness_response_tool_call_get(agent, 0, &tc) == 0);
    assert(strcmp(tc.id, "call_stock_1") == 0);
    assert(strcmp(tc.name, "get_stock_price") == 0);
    assert(harness_response_usage(agent, &usage) == 0);
    assert(usage.total_tokens == 20);
    printf("  parse tool_calls + usage: OK (%s)\n", tc.name);

    /* Append assistant tool call turn + tool result into history */
    assert(harness_message_append_assistant(agent, "agent_devbot", "") == 0);
    assert(harness_message_assistant_add_tool_call(agent, tc.id, tc.name, tc.arguments) == 0);
    assert(harness_message_append_tool_result(agent, tc.id, "{\"price\":190.5}") == 0);

    assert(harness_context_build(agent, &params) == 0);
    assert(harness_get_output(agent, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert_strstr((char*)out, "tool_calls");
    assert_strstr((char*)out, "call_stock_1");
    assert_strstr((char*)out, "tool_call_id");
    printf("  assistant tool_calls in context: OK\n");

    assert(harness_response_parse(agent, (const uint8_t*)final_resp, strlen(final_resp)) == 0);
    assert(harness_response_status(agent) == HARNESS_RESPONSE_COMPLETED);
    assert_strstr(harness_response_assistant_text(agent), "AAPL");

    /* Honcho: narrative ok, tool payload rejected */
    assert(harness_honcho_mirror_message(agent, "agent_devbot",
                                         "AAPL is trading near target.",
                                         "{\"type\":\"agent_response\"}") == 0);
    assert(harness_honcho_mirror_message(agent, "agent_devbot", chat_resp, NULL) != 0);
    assert(harness_honcho_build_messages_request(agent, "agent_devbot",
                                                 "AAPL is trading near target.",
                                                 "{\"type\":\"agent_response\",\"model\":\"gpt-4o\"}") == 0);
    assert(harness_get_output(agent, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert_strstr((char*)out, "peer_id");
    assert_strstr((char*)out, "sess_dial");
    printf("  honcho mirror policy + request builder: OK\n");

    /* Memory store/get */
    assert(harness_honcho_store_memory(agent, "human_alice", "pref", "likes concise") == 0);
    assert(strcmp(harness_honcho_get_memory(agent, "human_alice", "pref"), "likes concise") == 0);

    /* Participant remove / privilege */
    assert(harness_participant_set_privileged(agent, "human_bob", true) == 0);
    assert(harness_participant_remove(agent, "human_bob") == 0);
    assert(harness_participant_count(agent) == 2);

    /* Session retire blocks appends */
    assert(harness_session_retire(agent) == 0);
    assert(harness_session_is_retired(agent));
    assert(harness_message_append(agent, "human_alice", HARNESS_MSG_USER, "late", false) != 0);
    assert(harness_session_set(agent, "ws_dial", "sess_dial2") == 0);
    assert(!harness_session_is_retired(agent));
    printf("  session retire + participant lifecycle: OK\n");

    /* Dialectic handoff: copy agent narrative into human_view as peer message */
    assert(harness_participant_add(human_view, "agent_devbot", HARNESS_PARTICIPANT_AGENT, true) == 0);
    assert(harness_message_append(human_view, "agent_devbot", HARNESS_MSG_ASSISTANT,
                                  harness_response_assistant_text(agent), false) == 0);
    assert(harness_set_acting_peer(human_view, "agent_devbot") == 0);
    memset(&params, 0, sizeof(params));
    params.model = "gpt-4o-mini";
    params.temperature = 0.0;
    params.include_tools = false;
    params.identity_prefix = true;
    params.redact_secrets = true;
    assert(harness_context_build(human_view, &params) == 0);
    assert(harness_get_output(human_view, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert_strstr((char*)out, "[agent_devbot]");
    printf("  dialectic buffer handoff: OK\n");

    assert(harness_should_loop(agent, "false") == false);
    assert(strcmp(harness_event_type_name(HARNESS_EVENT_CONTEXT_READY), "context_ready") == 0);

    drain(agent);
    drain(human_view);
    harness_destroy(agent);
    harness_destroy(human_view);
    printf("dialectic session test PASSED\n");
    return 0;
}
