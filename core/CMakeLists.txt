# ---------------------------------------------------------------------------
# klend_ario_core — ALL calendar logic.
#
# Portable by construction: no OS SDK headers, no JNI, no #ifdef _WIN32 /
# __ANDROID__, and no knowledge that a frontend exists. It links arc_fs/arc_util
# for the storage vocabulary and the C sqlite3 API, and nothing else.
# ---------------------------------------------------------------------------

# The schema is authored as SQL (sql/schema.sql) and embedded at configure
# time, so there is exactly ONE copy of it and the binary carries it.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/sql/schema.sql KLEND_ARIO_SCHEMA_SQL)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/sql/schema_sql.hh.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/klend_ario/schema_sql.hh
    @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_SOURCE_DIR}/sql/schema.sql)

add_library(klend_ario_core STATIC
    src/db.cc
    src/schema.cc
    src/event.cc)

target_include_directories(klend_ario_core
    PUBLIC  include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
# arc_fs is PUBLIC: db.hh uses arc::Result, and callers of core naturally see
# the same storage vocabulary. sqlite3 is PRIVATE — it is an implementation
# detail behind the Db boundary.
target_link_libraries(klend_ario_core
    PUBLIC  arc_fs
    PRIVATE sqlite3)

if(MSVC)
    target_compile_options(klend_ario_core PRIVATE /W3 /utf-8)
else()
    target_compile_options(klend_ario_core PRIVATE -Wall)
endif()

# ---------------------------------------------------------------------------
# Tests — plain assert(), Debug-only, registered with CTest (TESTING.md).
#
# Narrow on purpose: event_test links the model alone; schema_test links the
# storage sources plus sqlite3 over an in-memory database, and deliberately NOT
# klend_ario_core — if it ever needs the whole library, something leaked.
# ---------------------------------------------------------------------------
include(TestHelpers)

klend_ario_add_test(event_test
    SOURCES  tests/event_test.cc src/event.cc
    INCLUDES include)

klend_ario_add_test(schema_test
    SOURCES  tests/schema_test.cc src/db.cc src/schema.cc src/event.cc
    INCLUDES include ${CMAKE_CURRENT_BINARY_DIR}/generated
    LIBS     sqlite3 arc_util)
