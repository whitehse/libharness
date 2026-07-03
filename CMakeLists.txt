cmake_minimum_required(VERSION 3.20)
project(libharness LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Strict warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" OFF)

# Find Lua (optional for bootstrap; full harness requires it)
find_package(Lua 5.4 QUIET)

# Find sibling libraries (assume installed or sibling paths)
find_path(PIQUE_INCLUDE_DIR NAMES pique.h HINTS ../pique/include /usr/local/include)
find_library(PIQUE_LIBRARY NAMES pique HINTS ../pique/build /usr/local/lib)

if(PIQUE_INCLUDE_DIR AND PIQUE_LIBRARY)
    message(STATUS "Found libpique: ${PIQUE_LIBRARY}")
else()
    message(WARNING "libpique not found; pg_vector and PG logging features will be stubs until installed")
endif()

# Public include
include_directories(include)

# Sources
set(HARNESS_SOURCES
    src/harness.c
    src/lua_bindings.c
    src/openai_processor.c
    src/honcho_interface.c
    src/pique_integration.c
)

add_library(harness STATIC ${HARNESS_SOURCES})
target_include_directories(harness PUBLIC include)

if(LUA_FOUND)
    message(STATUS "Found Lua: ${LUA_LIBRARIES}")
    target_include_directories(harness PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(harness PRIVATE ${LUA_LIBRARIES})
    target_compile_definitions(harness PRIVATE HAVE_LUA=1)
else()
    message(WARNING "Lua not found; lua_bindings.c compiled as stub. Install liblua5.4-dev for full functionality.")
endif()

if(PIQUE_INCLUDE_DIR)
    target_include_directories(harness PRIVATE ${PIQUE_INCLUDE_DIR})
    target_link_libraries(harness PRIVATE ${PIQUE_LIBRARY})
endif()

# Install
install(TARGETS harness ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()