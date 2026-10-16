# ---------------------------------------------------------------------------
# mcp/ — the local MCP server (merged.md §7). A frontend: it links core and
# core knows nothing about it.
# ---------------------------------------------------------------------------
add_library(klend_ario_mcp STATIC
    src/json.cc
    src/server.cc)

target_include_directories(klend_ario_mcp PUBLIC src)
target_link_libraries(klend_ario_mcp PUBLIC klend_ario_core)

add_executable(klend_ario_mcp_server src/main.cc)
target_link_libraries(klend_ario_mcp_server PRIVATE klend_ario_mcp)

if(MSVC)
    target_compile_options(klend_ario_mcp PRIVATE /W3 /utf-8)
    target_compile_options(klend_ario_mcp_server PRIVATE /W3 /utf-8)
else()
    target_compile_options(klend_ario_mcp PRIVATE -Wall)
    target_compile_options(klend_ario_mcp_server PRIVATE -Wall)
endif()

include(TestHelpers)

# json_test — the parser/serializer alone, no core.
klend_ario_add_test(json_test
    SOURCES  tests/json_test.cc src/json.cc
    INCLUDES src)

# server_test — JSON-RPC dispatch and the five tools, over an in-memory DB.
klend_ario_add_test(server_test
    SOURCES  tests/server_test.cc src/json.cc src/server.cc
    INCLUDES src
    LIBS     klend_ario_core)
