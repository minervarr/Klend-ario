# ---------------------------------------------------------------------------
# alarm/ — the alarm engine (merged.md §6). A frontend: it links core and
# installs a platform Notifier. It can ship as its own binary later, which is
# why it is separate from the CLI.
# ---------------------------------------------------------------------------
option(KLEND_BUILD_ALARM "Build the alarm engine frontend" ON)
if(NOT KLEND_BUILD_ALARM)
    return()
endif()

# libnotify is the Linux desktop backend. It is OPTIONAL: without it the engine
# still runs, logging to stderr (the same fallback shape as arc_fs's media
# index). PipeWire sound is not implemented yet — see MEMORY.md §9.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBNOTIFY QUIET libnotify)
endif()

add_library(klend_ario_alarm STATIC
    src/engine.cc
    src/notifier.cc)

target_include_directories(klend_ario_alarm PUBLIC src)
target_link_libraries(klend_ario_alarm PUBLIC klend_ario_core)

if(LIBNOTIFY_FOUND)
    target_sources(klend_ario_alarm PRIVATE src/notifier_libnotify.cc)
    target_compile_definitions(klend_ario_alarm PRIVATE KLEND_HAVE_LIBNOTIFY=1)
    target_include_directories(klend_ario_alarm PRIVATE ${LIBNOTIFY_INCLUDE_DIRS})
    target_link_directories(klend_ario_alarm PRIVATE ${LIBNOTIFY_LIBRARY_DIRS})
    target_link_libraries(klend_ario_alarm PRIVATE ${LIBNOTIFY_LIBRARIES})
    message(STATUS "alarm: libnotify found (${LIBNOTIFY_VERSION}) — desktop notifications enabled")
else()
    message(STATUS "alarm: libnotify not found — the engine will log alarms to stderr")
endif()

add_executable(klend_ario_alarmd src/main.cc)
target_link_libraries(klend_ario_alarmd PRIVATE klend_ario_alarm)

if(MSVC)
    target_compile_options(klend_ario_alarm PRIVATE /W3 /utf-8)
    target_compile_options(klend_ario_alarmd PRIVATE /W3 /utf-8)
else()
    target_compile_options(klend_ario_alarm PRIVATE -Wall)
    target_compile_options(klend_ario_alarmd PRIVATE -Wall)
endif()

include(TestHelpers)

# engine_test — the poll window and de-duplication, driven with chosen instants
# and a fake notifier. No sleep, no daemon, no platform.
klend_ario_add_test(engine_test
    SOURCES  tests/engine_test.cc src/engine.cc
    INCLUDES src
    LIBS     klend_ario_core)
