cmake_minimum_required(VERSION 3.20)

project(pokefirered_native_bootstrap C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

find_package(Threads REQUIRED)

set(POKEFIRERED_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pokefirered" CACHE PATH "Path to the upstream pret/pokefirered checkout")

if(NOT EXISTS "${POKEFIRERED_SOURCE_DIR}/README.md")
    message(FATAL_ERROR "POKEFIRERED_SOURCE_DIR must point at a pret/pokefirered checkout")
endif()

set(PFR_COMMON_DEFINES
    MODERN=1
    REVISION=0
    FIRERED
    ENGLISH
)

# Asset pipeline output directories (used by preprocessed source targets)
set(PFR_ASSETS_DIR "${CMAKE_BINARY_DIR}/assets")
set(PFR_INC_DIR    "${CMAKE_BINARY_DIR}/inc")
set(PFR_GEN_DIR    "${CMAKE_BINARY_DIR}/gen")

add_library(pfr_upstream_core STATIC
    src/host_crt0.c
    src/host_memory.c
    src/host_dma.c
    src/host_bios.c
    src/host_debug.c
    src/host_renderer.c
    src/host_display.c
    src/host_intro_stubs.c
        src/host_oak_speech_stubs.c
    src/host_title_screen_stubs.c
    src/upstream_stubs.c
    "${POKEFIRERED_SOURCE_DIR}/src/random.c"
    "${POKEFIRERED_SOURCE_DIR}/src/malloc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/decompress.c"
    "${POKEFIRERED_SOURCE_DIR}/src/gpu_regs.c"
    "${POKEFIRERED_SOURCE_DIR}/src/dma3_manager.c"
    "${POKEFIRERED_SOURCE_DIR}/src/task.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trig.c"
    "${POKEFIRERED_SOURCE_DIR}/src/scanline_effect.c"
    "${POKEFIRERED_SOURCE_DIR}/src/palette.c"
    "${POKEFIRERED_SOURCE_DIR}/src/blend_palette.c"
    "${POKEFIRERED_SOURCE_DIR}/src/bg.c"
    "${POKEFIRERED_SOURCE_DIR}/src/sprite.c"
    "${POKEFIRERED_SOURCE_DIR}/src/window.c"
    "${POKEFIRERED_SOURCE_DIR}/src/blit.c"
)

target_compile_definitions(pfr_upstream_core PRIVATE ${PFR_COMMON_DEFINES})
# Upstream headers must be quote-only to avoid colliding with the system's <strings.h>.
target_compile_options(pfr_upstream_core PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# SDL2 display backend (optional — falls back to PPM frame dump if unavailable)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
endif()

if(SDL2_FOUND)
    message(STATUS "SDL2 found — building with live display backend")
    target_compile_definitions(pfr_upstream_core PRIVATE HOST_DISPLAY_SDL2)
    target_compile_options(pfr_upstream_core PRIVATE ${SDL2_CFLAGS})
    target_link_libraries(pfr_upstream_core PRIVATE ${SDL2_LIBRARIES})
else()
    message(STATUS "SDL2 not found — using headless PPM frame-dump backend")
endif()

target_link_libraries(pfr_upstream_core PRIVATE m)

add_library(pfr_upstream_runtime OBJECT
    "${POKEFIRERED_SOURCE_DIR}/src/main.c"
)
target_compile_definitions(pfr_upstream_runtime PRIVATE
    MODERN=0
    REVISION=0
    FIRERED
    ENGLISH
)
target_compile_options(pfr_upstream_runtime PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_intro OBJECT
    "${PFR_GEN_DIR}/intro.c"
)
target_compile_definitions(pfr_upstream_intro PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_intro PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_intro PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_title OBJECT
    "${PFR_GEN_DIR}/title_screen.c"
)
target_compile_definitions(pfr_upstream_title PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_title PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_title PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_clear_save OBJECT
    "${POKEFIRERED_SOURCE_DIR}/src/clear_save_data_screen.c"
)
target_compile_definitions(pfr_upstream_clear_save PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_clear_save PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_clear_save PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_berry_fix OBJECT
    "${POKEFIRERED_SOURCE_DIR}/src/berry_fix_program.c"
)
target_compile_definitions(pfr_upstream_berry_fix PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_berry_fix PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_berry_fix PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_main_menu OBJECT
    "${POKEFIRERED_SOURCE_DIR}/src/main_menu.c"
)
target_compile_definitions(pfr_upstream_main_menu PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_main_menu PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_main_menu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_oak_speech OBJECT
    "${POKEFIRERED_SOURCE_DIR}/src/oak_speech.c"
)
target_compile_definitions(pfr_upstream_oak_speech PRIVATE
    ${PFR_COMMON_DEFINES}
    StartNewGameScene=UpstreamStartNewGameScene
)
target_compile_options(pfr_upstream_oak_speech PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_oak_speech PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(pfr_smoke tests/smoke.c)
target_sources(pfr_smoke PRIVATE
    src/host_agbmain.c
    $<TARGET_OBJECTS:pfr_upstream_intro>
    $<TARGET_OBJECTS:pfr_upstream_title>
    $<TARGET_OBJECTS:pfr_upstream_clear_save>
    $<TARGET_OBJECTS:pfr_upstream_berry_fix>
    $<TARGET_OBJECTS:pfr_upstream_main_menu>
    $<TARGET_OBJECTS:pfr_upstream_oak_speech>
    $<TARGET_OBJECTS:pfr_upstream_runtime>
)
target_compile_definitions(pfr_smoke PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_smoke PRIVATE
    -fno-pie
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_compile_options(pfr_smoke PRIVATE
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
)
target_link_options(pfr_smoke PRIVATE
    -no-pie
    -Wl,--gc-sections
)
target_link_libraries(pfr_smoke PRIVATE pfr_upstream_core Threads::Threads m)

add_executable(pfr_lz77 tools/pfr_lz77.c)
target_compile_definitions(pfr_lz77 PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_lz77 PRIVATE
    -fno-pie
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_compile_options(pfr_lz77 PRIVATE
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
)
target_link_options(pfr_lz77 PRIVATE
    -no-pie
    -Wl,--gc-sections
)
target_link_libraries(pfr_lz77 PRIVATE pfr_upstream_core m)

# Render test: runs the boot chain and dumps frames for visual verification
add_executable(pfr_render_test tests/render_test.c)
target_sources(pfr_render_test PRIVATE
    src/host_agbmain.c
    $<TARGET_OBJECTS:pfr_upstream_intro>
    $<TARGET_OBJECTS:pfr_upstream_title>
    $<TARGET_OBJECTS:pfr_upstream_clear_save>
    $<TARGET_OBJECTS:pfr_upstream_berry_fix>
    $<TARGET_OBJECTS:pfr_upstream_main_menu>
    $<TARGET_OBJECTS:pfr_upstream_runtime>
)
# NOTE: oak_speech excluded from render_test until its stubs are wired
target_compile_definitions(pfr_render_test PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_render_test PRIVATE
    -fno-pie
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
)
target_link_options(pfr_render_test PRIVATE
    -no-pie
    -Wl,--gc-sections
)
target_link_libraries(pfr_render_test PRIVATE pfr_upstream_core Threads::Threads m)

# Interactive player: SDL2 window with live keyboard input
add_executable(pfr_play src/pfr_play.c)
target_sources(pfr_play PRIVATE
    src/host_agbmain.c
    $<TARGET_OBJECTS:pfr_upstream_intro>
    $<TARGET_OBJECTS:pfr_upstream_title>
    $<TARGET_OBJECTS:pfr_upstream_clear_save>
    $<TARGET_OBJECTS:pfr_upstream_berry_fix>
    $<TARGET_OBJECTS:pfr_upstream_main_menu>
    $<TARGET_OBJECTS:pfr_upstream_oak_speech>
    $<TARGET_OBJECTS:pfr_upstream_runtime>
)
target_compile_definitions(pfr_play PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_play PRIVATE
    -fno-pie
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
)
target_link_options(pfr_play PRIVATE
    -no-pie
    -Wl,--gc-sections
)
target_link_libraries(pfr_play PRIVATE pfr_upstream_core Threads::Threads m)

# Asset compiler: converts upstream PNG/PAL/BIN to GBA intermediate formats
add_executable(pfr_assets tools/pfr_assets.c)
target_compile_options(pfr_assets PRIVATE -O2)
target_link_libraries(pfr_assets PRIVATE m)

# Asset pipeline: compile assets, generate .inc files, preprocess upstream sources
add_custom_command(
    OUTPUT "${PFR_GEN_DIR}/intro.c" "${PFR_GEN_DIR}/title_screen.c"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_ASSETS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_INC_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_GEN_DIR}"
    COMMAND $<TARGET_FILE:pfr_assets> batch "${POKEFIRERED_SOURCE_DIR}" "${PFR_ASSETS_DIR}"
    COMMAND $<TARGET_FILE:pfr_assets> geninc "${PFR_ASSETS_DIR}" "${PFR_INC_DIR}"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/intro.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/intro.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/title_screen.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/title_screen.c"
    DEPENDS pfr_assets
        "${POKEFIRERED_SOURCE_DIR}/src/intro.c"
        "${POKEFIRERED_SOURCE_DIR}/src/title_screen.c"
    COMMENT "Asset pipeline: PNG/PAL → GBA → .inc → preprocessed sources"
)
