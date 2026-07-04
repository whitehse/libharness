#include "harness.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("libharness smoke test starting...\n");

    harness_ctx_t* ctx = harness_create(HARNESS_ROLE_MAIN);
    assert(ctx != NULL);
    printf("  create: OK\n");

    const char* ver = harness_version();
    assert(ver != NULL);
    printf("  version: %s\n", ver);

    int rc = harness_lua_enumerate_tools(ctx);
    assert(rc == 0);
    printf("  enumerate_tools: OK\n");

    rc = harness_lua_set_personality(ctx, "{\"name\":\"test-harness\",\"traits\":[\"helpful\"]}");
    assert(rc == 0);
    printf("  set_personality: OK\n");

    bool loop = harness_lua_should_loop(ctx, "max_loops < 5");
    /* stub always true */
    printf("  should_loop: %s\n", loop ? "true" : "false");

    rc = harness_lua_log_interaction(ctx, "stub-model", "hello", "hi there");
    assert(rc == 0);
    printf("  log_interaction: OK\n");

    harness_event_t ev;
    rc = harness_next_event(ctx, &ev);
    assert(rc == 0);
    printf("  next_event: %d\n", ev);

    harness_destroy(ctx);
    printf("  destroy: OK\n");

    printf("libharness smoke test PASSED\n");
    return 0;
}