/**
 * Register the fiber_explain_fill tool that AI authors should call.
 * Host executes template fill via libanim (edgehost /api/v1/explain/render).
 */
#include "harness.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *PARAMS_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"template\":{\"type\":\"string\",\"description\":\"Template id e.g. optical_path\"},"
    "\"params\":{\"type\":\"object\",\"description\":\"key/value plan parameters\"}"
    "},"
    "\"required\":[\"template\",\"params\"]"
    "}";

int main(void)
{
    harness_config_t cfg;
    harness_ctx_t *ctx;
    harness_tool_def_t tool;
    harness_context_params_t params;
    uint8_t out[8192];
    size_t out_len = 0;
    char tools_json[4096];
    size_t tools_len = 0;

    harness_config_init_defaults(&cfg);
    cfg.workspace_id = "ecoec";
    cfg.session_id = "explain_test";
    cfg.acting_peer_id = "agent_explain";

    ctx = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg);
    assert(ctx);

    memset(&tool, 0, sizeof(tool));
    tool.name = "fiber_explain_fill";
    tool.description =
        "Fill a vetted fiber scene template with parameters and return a "
        "validated libanim ASCII plan. Prefer this over freeform drawing code.";
    tool.parameters_json = PARAMS_SCHEMA;
    tool.type = HARNESS_TOOL_FUNCTION;
    assert(harness_tool_register(ctx, &tool) == 0);

    assert(harness_participant_add(ctx, "employee", HARNESS_PARTICIPANT_HUMAN,
                                   true) == 0);
    assert(harness_participant_add(ctx, "agent_explain", HARNESS_PARTICIPANT_AGENT,
                                   true) == 0);
    assert(harness_soul_set(ctx,
                            "You explain fiber plant issues for cooperative "
                            "employees. Always use fiber_explain_fill with a "
                            "known template id; never invent topology.") == 0);
    assert(harness_message_append(
               ctx, "employee", HARNESS_MSG_USER,
               "Why is ONT-12 dark after the 1:32 splitter?", false) == 0);

    memset(&params, 0, sizeof(params));
    params.model = "gpt-4o-mini";
    params.temperature = 0.0;
    params.include_tools = true;
    params.identity_prefix = true;
    params.redact_secrets = true;

    assert(harness_context_build(ctx, &params) == 0);
    assert(harness_get_output(ctx, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert(strstr((char *)out, "fiber_explain_fill") != NULL);

    assert(harness_tools_to_json(ctx, tools_json, sizeof(tools_json),
                                 &tools_len) == 0);
    if (tools_len >= sizeof(tools_json)) {
        tools_len = sizeof(tools_json) - 1;
    }
    tools_json[tools_len] = '\0';
    assert(strstr(tools_json, "fiber_explain_fill") != NULL);
    printf("  tools_to_json: OK (%zu bytes)\n", tools_len);

    harness_destroy(ctx);
    printf("  fiber_explain_fill tool registration: OK\n");
    printf("all passed\n");
    return 0;
}
