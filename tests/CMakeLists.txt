cmake_minimum_required(VERSION 3.20)

add_executable(harness_smoke_test harness_smoke_test.c)
target_link_libraries(harness_smoke_test PRIVATE harness)
target_include_directories(harness_smoke_test PRIVATE ../include)
add_test(NAME harness_smoke COMMAND harness_smoke_test)

add_executable(test_dialectic_session test_dialectic_session.c)
target_link_libraries(test_dialectic_session PRIVATE harness)
target_include_directories(test_dialectic_session PRIVATE ../include)
add_test(NAME dialectic_session COMMAND test_dialectic_session)
