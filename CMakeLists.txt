cmake_minimum_required(VERSION 3.20)
project(libharness LANGUAGES C VERSION 0.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# libharness - AI harness state machine (Lua policy + libpique + Honcho)
add_library(harness STATIC
    src/harness.c src/lua_bindings.c src/openai_processor.c
    src/honcho_interface.c src/pique_integration.c)

target_include_directories(harness PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# Lua 5.4 (policy + tools)
find_package(Lua 5.4 QUIET)
if(LUA_FOUND)
    target_include_directories(harness PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(harness PRIVATE ${LUA_LIBRARIES})
    target_compile_definitions(harness PRIVATE HAVE_LUA=1)
    message(STATUS "Found Lua: ${LUA_LIBRARIES}")
else()
    message(WARNING "Lua not found; lua_bindings stub only")
endif()

# libpique (PG + pg_vector logging/classification)
find_path(PIQUE_INCLUDE_DIR NAMES pique.h PATHS /home/dwhite/pique/include ../pique/include)
find_library(PIQUE_LIBRARY NAMES pique PATHS /home/dwhite/pique/build ../pique/build)
if(PIQUE_INCLUDE_DIR AND PIQUE_LIBRARY)
    target_include_directories(harness PRIVATE ${PIQUE_INCLUDE_DIR})
    target_link_libraries(harness PRIVATE ${PIQUE_LIBRARY})
    message(STATUS "Found libpique: ${PIQUE_LIBRARY}")
else()
    message(WARNING "libpique not found; vector/PG stubs only")
endif()

include(GNUInstallDirs)
install(TARGETS harness EXPORT harness-targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT harness-targets FILE harness-config.cmake NAMESPACE harness:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/harness)

enable_testing()

# Dialectic + edge tests (ADR 003/004)
add_subdirectory(tests)

# Examples (CC0, always built for consumers)
add_subdirectory(examples)

# Optional fuzz (ADR 003)
option(ENABLE_FUZZ "Build fuzz harness" OFF)
if(ENABLE_FUZZ)
    add_executable(fuzz_harness tests/fuzz_harness.c)
    target_link_libraries(fuzz_harness PRIVATE harness)
    target_compile_options(fuzz_harness PRIVATE -fsanitize=fuzzer,address -g -O1)
    target_link_options(fuzz_harness PRIVATE -fsanitize=fuzzer,address)
endif()