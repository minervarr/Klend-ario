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
# sqlite3mc — SQLite3MultipleCiphers amalgamation (libs/thirdparty/sqlite3mc).
#
# The storage engine, and the Phase 3 page-level encryption. Vendored rather
# than found on the system so Linux and Android build the same version with no
# package to install. It embeds SQLite itself; sqlite3mc_amalgamation.h is the
# only header a consumer includes. It is C, and the one place a warning is
# expected from someone else's code, hence -w.
# ---------------------------------------------------------------------------
add_library(sqlite3mc STATIC libs/thirdparty/sqlite3mc/sqlite3mc_amalgamation.c)
target_include_directories(sqlite3mc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/thirdparty/sqlite3mc)
if(MSVC)
    target_compile_options(sqlite3mc PRIVATE /W0)
else()
    target_compile_options(sqlite3mc PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# The app: core (all logic), then the frontends. core/ never links a frontend;
# frontends link core. cli/ is the MVP frontend; gui/ is the weekly view (its
# pure geometry builds and tests today; the vk_canvas renderer is gated on the
# Vulkan SDK + slangc).
# ---------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(gui)
