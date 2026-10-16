cmake_minimum_required(VERSION 3.22.1)
project(klend_ario LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# C++17 with no compiler extensions, matching the rest of this workspace.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KLEND_BUILD_TESTS "Build the Debug-only assert-based test executables" ON)
option(KLEND_BUILD_GUI "Build the GUI frontend (Phase 4 — not implemented)" OFF)

# CTest drives the plain assert() suites; tests themselves are Debug-only and
# are registered through cmake/TestHelpers.cmake.
include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# archive_engine (framework submodule) — only arc_fs is wanted.
#
# The storage vocabulary (paths/layout/walk) is all this app consumes. The
# other three modules belong to the downloader stack: arc_net and arc_tag are
# OFF by default, and arc_archive is ON by default and DOWNLOADS libarchive at
# configure time, which this app has no use for. Force all three OFF before
# add_subdirectory() so a configure never reaches the network.
# ---------------------------------------------------------------------------
set(ARC_BUILD_ARCHIVE OFF CACHE BOOL "" FORCE)
set(ARC_BUILD_NET     OFF CACHE BOOL "" FORCE)
set(ARC_BUILD_TAG     OFF CACHE BOOL "" FORCE)
set(ARC_BUILD_FS      ON  CACHE BOOL "" FORCE)
set(ARC_BUILD_FS_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/firstparty/archive_engine/core archive_engine_build)

# ---------------------------------------------------------------------------
# sqlite3 — vendored amalgamation (libs/thirdparty/sqlite3).
#
# Vendored rather than found on the system so the same version builds on
# Linux and Android with no package to install. sqlite3.c is C and is the one
# place a warning is expected from someone else's code, hence -w.
# ---------------------------------------------------------------------------
add_library(sqlite3 STATIC libs/thirdparty/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/thirdparty/sqlite3)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# The app: core (all logic) then cli (the MVP frontend). core/ never links a
# frontend; frontends link core.
# ---------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(cli)
