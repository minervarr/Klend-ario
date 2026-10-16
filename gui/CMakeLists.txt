# ---------------------------------------------------------------------------
# gui/ — the weekly 16:9 view.
#
# This file is deliberately thin right now. The weekly view's GEOMETRY is pure
# and lives in klend_ario_view, so it can be asserted with no GPU
# (tests/week_layout_test.cc) — the part of a GUI that is worth testing before
# anyone can see it. The vk_canvas renderer (which needs the Vulkan SDK and the
# `slangc` shader compiler) is a later step in this same phase and is NOT built
# here yet; it will link klend_ario_view plus vk_canvas + app_shell.
# ---------------------------------------------------------------------------
add_library(klend_ario_view STATIC
    src/week_layout.cc)

target_include_directories(klend_ario_view PUBLIC src)

if(MSVC)
    target_compile_options(klend_ario_view PRIVATE /W3 /utf-8)
else()
    target_compile_options(klend_ario_view PRIVATE -Wall)
endif()

include(TestHelpers)

klend_ario_add_test(week_layout_test
    SOURCES  tests/week_layout_test.cc src/week_layout.cc
    INCLUDES src)
