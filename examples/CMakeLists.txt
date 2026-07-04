# Examples (CC0 licensed). Dialectic client/server style stubs.
add_executable(harness_example harness_example.c)
target_link_libraries(harness_example PRIVATE harness)
target_include_directories(harness_example PRIVATE ${CMAKE_SOURCE_DIR}/include)