# ---------------------------------------------------------------------------
# klend_ario_cli — the MVP frontend. A thin shell: parse args, call a core
# function, print. It links core; core never links it.
# ---------------------------------------------------------------------------
add_executable(klend_ario_cli
    src/main.cc
    src/args.cc
    src/commands.cc
    src/format.cc
    src/completion.cc)

target_link_libraries(klend_ario_cli PRIVATE klend_ario_core)

if(MSVC)
    target_compile_options(klend_ario_cli PRIVATE /W3 /utf-8)
else()
    target_compile_options(klend_ario_cli PRIVATE -Wall)
endif()

# ---------------------------------------------------------------------------
# args_test — argument parsing only. Links src/args.cc and nothing else, which
# is the property to protect: parsing must not need core, sqlite, or a database.
# ---------------------------------------------------------------------------
include(TestHelpers)

klend_ario_add_test(args_test
    SOURCES  tests/args_test.cc src/args.cc
    INCLUDES src)

# ---------------------------------------------------------------------------
# format_test — the bytes the user reads, without capturing stdout. It links
# the renderer and the model functions it formats, not the whole library.
# ---------------------------------------------------------------------------
klend_ario_add_test(format_test
    SOURCES  tests/format_test.cc src/format.cc ${CMAKE_SOURCE_DIR}/core/src/event.cc
    INCLUDES src ${CMAKE_SOURCE_DIR}/core/include
    LIBS     pe_holiday_calc arc_util)

# ---------------------------------------------------------------------------
# e2e_test — arg parsing + dispatch + core over a temporary database file. This
# is the one CLI test that links klend_ario_core (TESTING.md's "CLI end-to-end"
# row); it drives runCommand() rather than parsing args in isolation, so it
# would fail if a verb stopped reaching core.
# ---------------------------------------------------------------------------
klend_ario_add_test(e2e_test
    SOURCES  tests/e2e_test.cc src/args.cc src/commands.cc src/format.cc src/completion.cc
    INCLUDES src
    LIBS     klend_ario_core)

# completion_test — the three scripts alone. No core, no database, no shell.
klend_ario_add_test(completion_test
    SOURCES  tests/completion_test.cc src/completion.cc
    INCLUDES src)
