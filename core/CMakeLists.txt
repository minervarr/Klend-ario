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
    src/event.cc
    src/alarm.cc
    src/sync.cc
    src/holidays.cc)

target_include_directories(klend_ario_core
    PUBLIC  include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
# arc_fs is PUBLIC: db.hh uses arc::Result, and callers of core naturally see
# the same storage vocabulary. pe_holiday_calc is PUBLIC because
# klend_ario/holidays.hh exposes its Holiday/Date types. sqlite3mc is PRIVATE —
# it is an implementation detail behind the Db boundary.
target_link_libraries(klend_ario_core
    PUBLIC  arc_fs
            pe_holiday_calc
    PRIVATE sqlite3mc)

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
    LIBS     sqlite3mc arc_util)

# sync_test — the pure half of the Syncthing conflict policy. Links sync.cc and
# arc_fs (for the directory listing) and opens nothing.
klend_ario_add_test(sync_test
    SOURCES  tests/sync_test.cc src/sync.cc
    INCLUDES include
    LIBS     arc_fs)

# alarm_test — when an alarm fires, as pure calendar arithmetic. No notifier,
# no clock, no platform: it is a function of the events and a time window.
klend_ario_add_test(alarm_test
    SOURCES  tests/alarm_test.cc src/alarm.cc src/event.cc
    INCLUDES include)

# holidays_test — the std::optional -> arc::Result boundary over the
# pe-holiday-calc library. Links the adapter source plus the library, not the
# whole core (TESTING.md: narrow is the point).
klend_ario_add_test(holidays_test
    SOURCES  tests/holidays_test.cc src/holidays.cc
    INCLUDES include
    LIBS     pe_holiday_calc arc_util)
