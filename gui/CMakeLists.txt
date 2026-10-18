# ---------------------------------------------------------------------------
# gui/ — the weekly 16:9 view.
#
# Two halves, built separately on purpose:
#
#   klend_ario_view    the PURE half — layout, local-day resolution, theme and
#                      the draw-op scene. No Vulkan, no window, no OS graphics.
#                      Always built, always tested, on any machine.
#
#   klend_ario_gui     the WINDOW half — vk_canvas_wayland + app_shell_wayland,
#                      the scene-to-Canvas adapter and the capture tool. Gated on
#                      KLEND_BUILD_GUI because it needs the Vulkan SDK, the
#                      Slang compiler and the Wayland/xkb development packages.
#
# Until Phase 4's renderer landed, only the geometry existed; the scene is the
# tested visual contract that makes the renderer a thin mapping rather than the
# place where colour, labels and truncation were decided untested.
# ---------------------------------------------------------------------------
add_library(klend_ario_view STATIC
    src/week_layout.cc
    src/local_time.cc
    src/theme.cc
    src/scene.cc
    src/week_store.cc
    src/text_inputs.cc)

target_include_directories(klend_ario_view PUBLIC src)
target_link_libraries(klend_ario_view PUBLIC klend_ario_core)

if(MSVC)
    target_compile_options(klend_ario_view PRIVATE /W3 /utf-8)
else()
    target_compile_options(klend_ario_view PRIVATE -Wall)
endif()

include(TestHelpers)

# Geometry: frame/margins, Sunday-first columns, proportional boxes, the
# hatched tail, DST-length days and overlap clusters. No font, no GPU.
klend_ario_add_test(week_layout_test
    SOURCES  tests/week_layout_test.cc src/week_layout.cc
    INCLUDES src)

# Local civil days: the one place a timezone is consulted. Pins Lima (no DST)
# and Berlin (23h and 25h days) so "a day is 86,400 s" cannot come back.
klend_ario_add_test(local_time_test
    SOURCES  tests/local_time_test.cc src/local_time.cc
    INCLUDES src)

# Chrome metrics and palette: derived from the content rect, never from DPI.
klend_ario_add_test(theme_test
    SOURCES  tests/theme_test.cc src/theme.cc src/week_layout.cc
    INCLUDES src)

# The visual contract itself: colours, borders, labels, tiers, tail, now-line,
# header and hits — asserted as a draw-op list with no GPU and a fake font.
klend_ario_add_test(scene_test
    SOURCES  tests/scene_test.cc src/scene.cc src/theme.cc src/week_layout.cc
             src/local_time.cc ${CMAKE_SOURCE_DIR}/core/src/event.cc
    INCLUDES src ${CMAKE_SOURCE_DIR}/core/include)

# The codepoint/size set the glyph cache must bake. No font, no vk_canvas: it is
# the pure half of "measure only after the cells exist". Also pins that a title
# can add non-ASCII codepoints and that the reserved dayDateSize is not baked.
klend_ario_add_test(text_inputs_test
    SOURCES  tests/text_inputs_test.cc src/text_inputs.cc
    INCLUDES src ${CMAKE_SOURCE_DIR}/core/include)

# The database-to-view bridge: one query, seven local days, the week label and
# the holidays. Uses an in-memory database and a pinned zone; no GPU, no window.
klend_ario_add_test(week_store_test
    SOURCES  tests/week_store_test.cc src/week_store.cc src/scene.cc src/theme.cc
             src/week_layout.cc src/local_time.cc
             ${CMAKE_SOURCE_DIR}/core/src/db.cc ${CMAKE_SOURCE_DIR}/core/src/schema.cc
             ${CMAKE_SOURCE_DIR}/core/src/query.cc ${CMAKE_SOURCE_DIR}/core/src/event.cc
             ${CMAKE_SOURCE_DIR}/core/src/holidays.cc
    INCLUDES src ${CMAKE_SOURCE_DIR}/core/include ${CMAKE_BINARY_DIR}/core/generated
    LIBS     sqlite3mc pe_holiday_calc arc_util)

# ---------------------------------------------------------------------------
# The window half. Gated on KLEND_BUILD_GUI because it needs the Vulkan SDK,
# the Slang compiler and the Wayland/xkb development packages — none of which
# the pure tests above require.
#
# Everything here is a CONSUMER of the two framework submodules:
#   vk_canvas_core / vk_canvas_wayland  drawing + the Wayland window
#   vk_canvas_capture                   headless PNG screenshots (dev tooling)
#   app_shell / app_shell_wayland       the Host/AppView seam and paths
# ---------------------------------------------------------------------------
if(KLEND_BUILD_GUI)
    set(VK_CANVAS_DIR ${CMAKE_SOURCE_DIR}/libs/firstparty/framework/Vk_Canvas_Lb_LAW
        CACHE PATH "vk_canvas source root (consumed by app_shell too)")

    # What this application tells its shell. The state dir stays "beside the
    # executable" (app_shell's default) because the database deliberately does
    # NOT live here — it is in arc::fs's state dir, and this is only the log.
    set(APP_SHELL_APP_NAME  "klend_ario"     CACHE STRING "" FORCE)
    set(APP_SHELL_WIN_CLASS "KlendArioMain"  CACHE STRING "" FORCE)
    set(APP_SHELL_WINDOW_TITLE "Klend-ario"  CACHE STRING "" FORCE)
    set(APP_SHELL_APP_ID       "klend-ario"  CACHE STRING "" FORCE)

    add_subdirectory(${VK_CANVAS_DIR}/core ${CMAKE_BINARY_DIR}/vk_canvas_build)
    add_subdirectory(${CMAKE_SOURCE_DIR}/libs/firstparty/framework/App_shell
                     ${CMAKE_BINARY_DIR}/app_shell_build)

    if(WIN32 OR ANDROID)
        message(FATAL_ERROR
            "klend_ario_gui is Linux/Wayland first (desktop-first, AGENTS.md §2.9). "
            "WIN32/ANDROID need their own host wiring before this can build.")
    endif()
    add_subdirectory(${VK_CANVAS_DIR}/platform/linux ${CMAKE_BINARY_DIR}/vk_canvas_linux_build)
    add_subdirectory(${VK_CANVAS_DIR}/core/capture ${CMAKE_BINARY_DIR}/vk_canvas_capture_build)

    # The UI text-size floor, computed from the faces this app actually ships
    # (one). ui_metrics.cc includes the generated header, so the dependency is
    # stated from here — the consumer owns the font list.
    include(AppShellMinTextSize)
    app_shell_generate_min_text_size(${CMAKE_SOURCE_DIR}/assets/fonts/ui/ui.otf)
    add_dependencies(app_shell generate_ui_min_text_size)

    # Shaders: the font engine's five passes plus vk_canvas's overlay/image/
    # shape. Compiled next to the build tree, then copied beside the exe, which
    # is where FileAssetReader looks.
    include(${VK_CANVAS_DIR}/cmake/VceShaders.cmake)
    if(NOT WIN32 AND NOT EXISTS "${VCE_SLANGC}")
        find_program(KLEND_SLANGC slangc)
        if(KLEND_SLANGC)
            set(VCE_SLANGC "${KLEND_SLANGC}" CACHE FILEPATH "Slang compiler (slangc)" FORCE)
        else()
            message(FATAL_ERROR "slangc not found; set -DVCE_SLANGC=/path/to/slangc")
        endif()
    endif()
    vce_compile_slang(compile_font_shaders
        ${CMAKE_BINARY_DIR}/assets/shaders
        ${VK_CANVAS_DIR}/first_party/vulkan_font_engine/shaders_src
        composite_vert composite_frag tiling coverage msdf_vert msdf_frag
        glyph_area glyph_resolve)
    vce_compile_slang(compile_canvas_shaders
        ${CMAKE_BINARY_DIR}/assets/shaders
        ${VK_CANVAS_DIR}/shaders_src
        overlay_vert overlay_frag image_vert image_frag shape_vert shape_frag)

    # -----------------------------------------------------------------------
    # klend_ario_gui — the weekly view window.
    # -----------------------------------------------------------------------
    add_executable(klend_ario_gui
        src/gui_main.cc
        src/week_view.cc
        src/scene_render.cc)

    target_link_libraries(klend_ario_gui PRIVATE
        klend_ario_view
        klend_ario_core
        app_shell
        app_shell_wayland
        vk_canvas_core
        vk_canvas_wayland)

    target_compile_definitions(klend_ario_gui PRIVATE KLEND_ARIO_VERSION="${PROJECT_VERSION}")
    target_compile_options(klend_ario_gui PRIVATE -Wall)
    add_dependencies(klend_ario_gui compile_font_shaders compile_canvas_shaders)

    # Fonts and shaders beside the executable. A custom target rather than
    # install rules: the build tree has to be runnable, which is how this app
    # has been exercised from the start. Both executables in this directory
    # depend on it, so either one can be built on its own.
    add_custom_target(klend_ario_gui_assets ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:klend_ario_gui>/assets
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:klend_ario_gui>/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_BINARY_DIR}/assets $<TARGET_FILE_DIR:klend_ario_gui>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/assets/fonts/ui $<TARGET_FILE_DIR:klend_ario_gui>/fonts/ui
        COMMENT "Copying fonts and shaders next to klend_ario_gui")
    add_dependencies(klend_ario_gui_assets compile_font_shaders compile_canvas_shaders)
    add_dependencies(klend_ario_gui klend_ario_gui_assets)

    # -----------------------------------------------------------------------
    # klend_ario_capture — headless PNG screenshots (Debug-only dev tooling).
    # -----------------------------------------------------------------------
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_executable(klend_ario_capture tools/capture_main.cc src/scene_render.cc)
        target_link_libraries(klend_ario_capture PRIVATE
            klend_ario_view
            vk_canvas_capture
            app_shell)
        target_compile_options(klend_ario_capture PRIVATE -Wall)
        add_dependencies(klend_ario_capture compile_font_shaders compile_canvas_shaders)
        add_dependencies(klend_ario_capture klend_ario_gui_assets)
    endif()
endif()
