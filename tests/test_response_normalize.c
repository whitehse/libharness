/* Provider response shape normalizer tests (chat.completions vs Responses API). */
#include "harness.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void drain(harness_ctx_t* ctx) {
    harness_event_t ev;
    while (harness_next_event(ctx, &ev) == 0 && ev.type != HARNESS_EVENT_NONE) {
    }
}

static int saw_detail(harness_ctx_t* ctx, harness_event_type_t type, const char* detail_sub) {
    harness_event_t ev;
    while (harness_next_event(ctx, &ev) == 0 && ev.type != HARNESS_EVENT_NONE) {
        if (ev.type == type && detail_sub && strstr(ev.detail, detail_sub) != NULL)
            return 1;
        if (ev.type == type && (!detail_sub || detail_sub[0] == '\0'))
            return 1;
    }
    return 0;
}

int main(void) {
    harness_ctx_t* ctx;
    harness_tool_call_t tc;
    harness_usage_t usage;
    harness_event_t ev;

    const char* chat_tools =
        "{\"id\":\"chatcmpl_x\",\"object\":\"chat.completion\","
        "\"choices\":[{\"index\":0,\"finish_reason\":\"tool_calls\","
        "\"message\":{\"role\":\"assistant\",\"content\":null,"
        "\"tool_calls\":[{\"id\":\"call_escape\",\"type\":\"function\","
        "\"function\":{\"name\":\"get_stock_price\","
        "\"arguments\":\"{\\\"ticker\\\":\\\"AAPL\\\"}\"}}]}}],"
        "\"usage\":{\"prompt_tokens\":11,\"completion_tokens\":7,\"total_tokens\":18}}";

    const char* chat_done =
        "{\"id\":\"chatcmpl_y\",\"object\":\"chat.completion\","
        "\"choices\":[{\"index\":0,\"finish_reason\":\"stop\","
        "\"message\":{\"role\":\"assistant\","
        "\"content\":\"AAPL looks steady.\"}}],"
        "\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":4,\"total_tokens\":9}}";

    const char* responses_tools =
        "{\"id\":\"resp_tools\",\"object\":\"response\",\"status\":\"requires_action\","
        "\"output\":[{\"type\":\"function_call\",\"call_id\":\"call_resp_1\","
        "\"name\":\"get_weather\","
        "\"arguments\":{\"city\":\"Boston\"}}],"
        "\"usage\":{\"input_tokens\":3,\"output_tokens\":2,\"total_tokens\":5}}";

    const char* responses_done =
        "{\"id\":\"resp_ok\",\"object\":\"response\",\"status\":\"completed\","
        "\"output\":[{\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[{\"type\":\"text\",\"text\":\"It is sunny.\"}]}],"
        "\"usage\":{\"input_tokens\":10,\"output_tokens\":6}}";

    const char* responses_error =
        "{\"id\":\"resp_err\",\"object\":\"response\","
        "\"error\":{\"message\":\"quota exceeded\",\"type\":\"rate_limit\"}}";

    const char* incomplete =
        "{\"id\":\"resp_i\",\"object\":\"response\",\"status\":\"incomplete\","
        "\"output\":[{\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[{\"type\":\"text\",\"text\":\"partial\"}]}]}";

    printf("response normalize test...\n");
    ctx = harness_create(HARNESS_ROLE_PROCESSOR);
    assert(ctx);
    assert(harness_set_acting_peer(ctx, "agent_a") == 0);

    assert(harness_response_parse(ctx, (const uint8_t*)chat_tools, strlen(chat_tools)) == 0);
    assert(harness_response_status(ctx) == HARNESS_RESPONSE_REQUIRES_ACTION);
    assert(harness_response_tool_call_count(ctx) >= 1);
    assert(harness_response_tool_call_get(ctx, 0, &tc) == 0);
    assert(strcmp(tc.id, "call_escape") == 0);
    assert(strcmp(tc.name, "get_stock_price") == 0);
    assert(strstr(tc.arguments, "ticker") != NULL);
    assert(strstr(tc.arguments, "AAPL") != NULL);
    assert(harness_response_usage(ctx, &usage) == 0);
    assert(usage.total_tokens == 18);
    assert(saw_detail(ctx, HARNESS_EVENT_RESPONSE_REQUIRES_ACTION, "chat_completions"));
    printf("  chat.completions tool_calls + unescape args: OK\n");

    drain(ctx);
    assert(harness_response_parse(ctx, (const uint8_t*)chat_done, strlen(chat_done)) == 0);
    assert(harness_response_status(ctx) == HARNESS_RESPONSE_COMPLETED);
    assert(strstr(harness_response_assistant_text(ctx), "AAPL looks steady") != NULL);
    assert(saw_detail(ctx, HARNESS_EVENT_RESPONSE_COMPLETED, "chat_completions"));
    printf("  chat.completions completed text: OK\n");

    drain(ctx);
    assert(harness_response_parse(ctx, (const uint8_t*)responses_tools,
                                  strlen(responses_tools)) == 0);
    assert(harness_response_status(ctx) == HARNESS_RESPONSE_REQUIRES_ACTION);
    assert(harness_response_tool_call_get(ctx, 0, &tc) == 0);
    assert(strcmp(tc.id, "call_resp_1") == 0);
    assert(strcmp(tc.name, "get_weather") == 0);
    assert(strstr(tc.arguments, "Boston") != NULL);
    assert(harness_response_usage(ctx, &usage) == 0);
    assert(usage.prompt_tokens == 3);
    assert(usage.completion_tokens == 2);
    assert(usage.total_tokens == 5);
    assert(saw_detail(ctx, HARNESS_EVENT_RESPONSE_REQUIRES_ACTION, "responses_api"));
    printf("  Responses API function_call + input/output tokens: OK\n");

    drain(ctx);
    assert(harness_response_parse(ctx, (const uint8_t*)responses_done,
                                  strlen(responses_done)) == 0);
    assert(harness_response_status(ctx) == HARNESS_RESPONSE_COMPLETED);
    assert(strstr(harness_response_assistant_text(ctx), "sunny") != NULL);
    assert(harness_response_usage(ctx, &usage) == 0);
    assert(usage.total_tokens == 16); /* 10+6 synthesized */
    printf("  Responses API completed + usage synth: OK\n");

    drain(ctx);
    assert(harness_response_parse(ctx, (const uint8_t*)responses_error,
                                  strlen(responses_error)) == 0);
    assert(harness_response_status(ctx) == HARNESS_RESPONSE_ERROR);
    {
        int saw_err = 0;
        while (harness_next_event(ctx, &ev) == 0 && ev.type != HARNESS_EVENT_NONE) {
            if (ev.type == HARNESS_EVENT_RESPONSE_ERROR) saw_err = 1;
        }
        assert(saw_err);
    }
    printf("  error object: OK\n");

    drain(ctx);
    assert(harness_response_parse(ctx, (const uint8_t*)incomplete, strlen(incomplete)) == 0);
    assert(harness_response_status(ctx) == HARNESS_RESPONSE_INCOMPLETE);
    printf("  incomplete status: OK\n");

    harness_destroy(ctx);
    printf("response normalize test PASSED\n");
    return 0;
}
