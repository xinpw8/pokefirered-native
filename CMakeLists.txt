cmake_minimum_required(VERSION 3.20)

project(pokefirered_native_bootstrap C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(ExternalProject)

set(POKEFIRERED_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pokefirered" CACHE PATH "Path to the upstream pret/pokefirered checkout")
set(AGBCC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/agbcc" CACHE PATH "Path to the upstream pret/agbcc checkout")

if(NOT EXISTS "${POKEFIRERED_SOURCE_DIR}/README.md")
    message(FATAL_ERROR "POKEFIRERED_SOURCE_DIR must point at a pret/pokefirered checkout")
endif()
if(NOT EXISTS "${AGBCC_SOURCE_DIR}/build.sh")
    message(FATAL_ERROR "AGBCC_SOURCE_DIR must point at a pret/agbcc checkout")
endif()

set(PFR_COMMON_DEFINES
    MODERN=1
    REVISION=0
    FIRERED
    ENGLISH
    HOST_NATIVE=1
)

# Asset pipeline output directories (used by preprocessed source targets)
set(PFR_ASSETS_DIR "${CMAKE_BINARY_DIR}/assets")
set(PFR_INC_DIR    "${CMAKE_BINARY_DIR}/inc")
set(PFR_GEN_DIR    "${CMAKE_BINARY_DIR}/gen")
set(PFR_GEN_HEADERS_DIR "${CMAKE_BINARY_DIR}/generated_include")
set(PFR_MAPJSON_OUT_DIR "${CMAKE_BINARY_DIR}/mapjson")
set(PFR_ASSET_MANIFEST_DIR "${CMAKE_BINARY_DIR}/asset_manifests")
set(PFR_BOOT_ASSET_MANIFEST "${PFR_ASSET_MANIFEST_DIR}/boot_path.manifest")
set(PFR_CHECKED_IN_BOOT_ASSET_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/.project/assets/boot_path.manifest")
set(PFR_ASSET_PIPELINE_STAMP "${PFR_GEN_DIR}/.asset_pipeline.stamp")
set(PFR_PREPROCESSED_SOURCES
    "${PFR_GEN_DIR}/intro.c"
    "${PFR_GEN_DIR}/title_screen.c"
    "${PFR_GEN_DIR}/main_menu.c"
    "${PFR_GEN_DIR}/oak_speech.c"
    "${PFR_GEN_DIR}/text_window_graphics.c"
    "${PFR_GEN_DIR}/text.c"
    "${PFR_GEN_DIR}/new_menu_helpers.c"
    "${PFR_GEN_DIR}/graphics.c"
    "${PFR_GEN_DIR}/tilesets.c"
    "${PFR_GEN_DIR}/event_object_movement.c"
    "${PFR_GEN_DIR}/party_menu.c"
    "${PFR_GEN_DIR}/bag.c"
    "${PFR_GEN_DIR}/battle_anim_effects_3.c"
    "${PFR_GEN_DIR}/battle_anim_flying.c"
    "${PFR_GEN_DIR}/battle_anim_water.c"
    "${PFR_GEN_DIR}/battle_bg.c"
    "${PFR_GEN_DIR}/battle_interface.c"
    "${PFR_GEN_DIR}/battle_records.c"
    "${PFR_GEN_DIR}/battle_script_commands.c"
    "${PFR_GEN_DIR}/battle_transition.c"
    "${PFR_GEN_DIR}/berry_crush.c"
    "${PFR_GEN_DIR}/braille_text.c"
    "${PFR_GEN_DIR}/credits.c"
    "${PFR_GEN_DIR}/daycare.c"
    "${PFR_GEN_DIR}/digit_obj_util.c"
    "${PFR_GEN_DIR}/diploma.c"
    "${PFR_GEN_DIR}/dodrio_berry_picking.c"
    "${PFR_GEN_DIR}/easy_chat_3.c"
    "${PFR_GEN_DIR}/evolution_graphics.c"
    "${PFR_GEN_DIR}/evolution_scene.c"
    "${PFR_GEN_DIR}/fame_checker.c"
    "${PFR_GEN_DIR}/field_door.c"
    "${PFR_GEN_DIR}/field_effect.c"
    "${PFR_GEN_DIR}/field_specials.c"
    "${PFR_GEN_DIR}/field_weather.c"
    "${PFR_GEN_DIR}/fldeff_flash.c"
    "${PFR_GEN_DIR}/hall_of_fame.c"
    "${PFR_GEN_DIR}/help_message.c"
    "${PFR_GEN_DIR}/help_system.c"
    "${PFR_GEN_DIR}/help_system_util.c"
    "${PFR_GEN_DIR}/item_menu.c"
    "${PFR_GEN_DIR}/itemfinder.c"
    "${PFR_GEN_DIR}/learn_move.c"
    "${PFR_GEN_DIR}/link.c"
    "${PFR_GEN_DIR}/link_rfu_3.c"
    "${PFR_GEN_DIR}/map_preview_screen.c"
    "${PFR_GEN_DIR}/menu_indicators.c"
    "${PFR_GEN_DIR}/minigame_countdown.c"
    "${PFR_GEN_DIR}/mon_markings.c"
    "${PFR_GEN_DIR}/mystery_gift_menu.c"
    "${PFR_GEN_DIR}/mystery_gift_show_card.c"
    "${PFR_GEN_DIR}/mystery_gift_show_news.c"
    "${PFR_GEN_DIR}/naming_screen.c"
    "${PFR_GEN_DIR}/option_menu.c"
    "${PFR_GEN_DIR}/pokedex_area_markers.c"
    "${PFR_GEN_DIR}/pokedex_screen.c"
    "${PFR_GEN_DIR}/pokemon.c"
    "${PFR_GEN_DIR}/pokemon_icon.c"
    "${PFR_GEN_DIR}/pokemon_jump.c"
    "${PFR_GEN_DIR}/pokemon_special_anim_scene.c"
    "${PFR_GEN_DIR}/pokemon_storage_system_data.c"
    "${PFR_GEN_DIR}/pokemon_storage_system_graphics.c"
    "${PFR_GEN_DIR}/pokemon_storage_system_menu.c"
    "${PFR_GEN_DIR}/pokemon_storage_system_misc.c"
    "${PFR_GEN_DIR}/pokemon_storage_system_tasks.c"
    "${PFR_GEN_DIR}/pokemon_summary_screen.c"
    "${PFR_GEN_DIR}/quest_log.c"
    "${PFR_GEN_DIR}/region_map.c"
    "${PFR_GEN_DIR}/save_failed_screen.c"
    "${PFR_GEN_DIR}/script_menu.c"
    "${PFR_GEN_DIR}/seagallop.c"
    "${PFR_GEN_DIR}/slot_machine.c"
    "${PFR_GEN_DIR}/ss_anne.c"
    "${PFR_GEN_DIR}/tileset_anims.c"
    "${PFR_GEN_DIR}/trade.c"
    "${PFR_GEN_DIR}/trade_scene.c"
    "${PFR_GEN_DIR}/trainer_card.c"
    "${PFR_GEN_DIR}/trainer_see.c"
    "${PFR_GEN_DIR}/union_room_chat_display.c"
    "${PFR_GEN_DIR}/union_room_chat_objects.c"
    "${PFR_GEN_DIR}/util.c"
    "${PFR_GEN_DIR}/wireless_communication_status_screen.c"
    # Charmap-only files (no INCBINs, but contain _() strings needing Pokemon encoding)
    "${PFR_GEN_DIR}/strings.c"
    "${PFR_GEN_DIR}/battle_message.c"
    "${PFR_GEN_DIR}/trainer_tower_sets.c"
    "${PFR_GEN_DIR}/move_descriptions.c"
    "${PFR_GEN_DIR}/union_room_message.c"
    "${PFR_GEN_DIR}/berry.c"
    "${PFR_GEN_DIR}/keyboard_text.c"
    "${PFR_GEN_DIR}/battle_main.c"
    "${PFR_GEN_DIR}/mystery_event_msg.c"
    # Additional charmap-only files (contain _() but no INCBINs)
    "${PFR_GEN_DIR}/battle_anim_status_effects.c"
    "${PFR_GEN_DIR}/battle_tower.c"
    "${PFR_GEN_DIR}/berry_pouch.c"
    "${PFR_GEN_DIR}/field_player_avatar.c"
    "${PFR_GEN_DIR}/link_rfu_2.c"
    "${PFR_GEN_DIR}/mailbox_pc.c"
    "${PFR_GEN_DIR}/mystery_gift_scripts.c"
    "${PFR_GEN_DIR}/scrcmd.c"
    "${PFR_GEN_DIR}/string_util.c"
    "${PFR_GEN_DIR}/tm_case.c"
)

set(PFR_CHARMAP_HEADER_INPUTS)
set(PFR_CHARMAP_HEADER_OUTPUTS)
set(PFR_CHARMAP_HEADER_COMMANDS)
file(GLOB_RECURSE PFR_DATA_HEADER_CANDIDATES
    "${POKEFIRERED_SOURCE_DIR}/src/data/*.h"
)
foreach(header_path IN LISTS PFR_DATA_HEADER_CANDIDATES)
    file(READ "${header_path}" header_contents)
    if(header_contents MATCHES "_\\(\"" OR header_contents MATCHES "__\\(\"")
        file(RELATIVE_PATH header_rel "${POKEFIRERED_SOURCE_DIR}/src" "${header_path}")
        set(header_out "${PFR_GEN_HEADERS_DIR}/${header_rel}")
        get_filename_component(header_out_dir "${header_out}" DIRECTORY)
        list(APPEND PFR_CHARMAP_HEADER_INPUTS "${header_path}")
        list(APPEND PFR_CHARMAP_HEADER_OUTPUTS "${header_out}")
        list(APPEND PFR_CHARMAP_HEADER_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E make_directory "${header_out_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${header_path}" "${header_out}"
        )
    endif()
endforeach()

set(PFR_BOOT_ASSET_TUS
    "${POKEFIRERED_SOURCE_DIR}/src/intro.c"
    "${POKEFIRERED_SOURCE_DIR}/src/title_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/main_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/oak_speech.c"
    "${POKEFIRERED_SOURCE_DIR}/src/text_window_graphics.c"
    "${POKEFIRERED_SOURCE_DIR}/src/text.c"
    "${POKEFIRERED_SOURCE_DIR}/src/new_menu_helpers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/graphics.c"
    "${POKEFIRERED_SOURCE_DIR}/src/bag.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_effects_3.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_flying.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_water.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_bg.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_interface.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_records.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_script_commands.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_transition.c"
    "${POKEFIRERED_SOURCE_DIR}/src/berry_crush.c"
    "${POKEFIRERED_SOURCE_DIR}/src/braille_text.c"
    "${POKEFIRERED_SOURCE_DIR}/src/credits.c"
    "${POKEFIRERED_SOURCE_DIR}/src/daycare.c"
    "${POKEFIRERED_SOURCE_DIR}/src/digit_obj_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/diploma.c"
    "${POKEFIRERED_SOURCE_DIR}/src/dodrio_berry_picking.c"
    "${POKEFIRERED_SOURCE_DIR}/src/easy_chat_3.c"
    "${POKEFIRERED_SOURCE_DIR}/src/evolution_graphics.c"
    "${POKEFIRERED_SOURCE_DIR}/src/evolution_scene.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fame_checker.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_door.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_effect.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_specials.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_weather.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_flash.c"
    "${POKEFIRERED_SOURCE_DIR}/src/hall_of_fame.c"
    "${POKEFIRERED_SOURCE_DIR}/src/help_message.c"
    "${POKEFIRERED_SOURCE_DIR}/src/help_system.c"
    "${POKEFIRERED_SOURCE_DIR}/src/help_system_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/item_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/itemfinder.c"
    "${POKEFIRERED_SOURCE_DIR}/src/learn_move.c"
    "${POKEFIRERED_SOURCE_DIR}/src/link.c"
    "${POKEFIRERED_SOURCE_DIR}/src/link_rfu_3.c"
    "${POKEFIRERED_SOURCE_DIR}/src/map_preview_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/menu_indicators.c"
    "${POKEFIRERED_SOURCE_DIR}/src/minigame_countdown.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mon_markings.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_show_card.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_show_news.c"
    "${POKEFIRERED_SOURCE_DIR}/src/naming_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/option_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokedex_area_markers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokedex_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_icon.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_jump.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_special_anim_scene.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_data.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_graphics.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_misc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_tasks.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_summary_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log.c"
    "${POKEFIRERED_SOURCE_DIR}/src/region_map.c"
    "${POKEFIRERED_SOURCE_DIR}/src/save_failed_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/script_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/seagallop.c"
    "${POKEFIRERED_SOURCE_DIR}/src/slot_machine.c"
    "${POKEFIRERED_SOURCE_DIR}/src/ss_anne.c"
    "${POKEFIRERED_SOURCE_DIR}/src/tileset_anims.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trade.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trade_scene.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_card.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_see.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat_display.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat_objects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/wireless_communication_status_screen.c"
    # Charmap-only files
    "${POKEFIRERED_SOURCE_DIR}/src/strings.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_message.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_tower_sets.c"
    "${POKEFIRERED_SOURCE_DIR}/src/move_descriptions.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_message.c"
    "${POKEFIRERED_SOURCE_DIR}/src/berry.c"
    "${POKEFIRERED_SOURCE_DIR}/src/keyboard_text.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_main.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_event_msg.c"
    # Additional charmap-only files
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_status_effects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_tower.c"
    "${POKEFIRERED_SOURCE_DIR}/src/berry_pouch.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_player_avatar.c"
    "${POKEFIRERED_SOURCE_DIR}/src/link_rfu_2.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mailbox_pc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_scripts.c"
    "${POKEFIRERED_SOURCE_DIR}/src/scrcmd.c"
    "${POKEFIRERED_SOURCE_DIR}/src/string_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/tm_case.c"
    # Source files whose #included data headers contain INCBINs
    "${POKEFIRERED_SOURCE_DIR}/src/tilesets.c"
    "${POKEFIRERED_SOURCE_DIR}/src/event_object_movement.c"
    "${POKEFIRERED_SOURCE_DIR}/src/party_menu.c"
)

# Data headers that contain INCBIN macros (included by source files above).
# These must be preprocessed so INCBIN_XX("...") is replaced with real data
# instead of expanding to {0} via the HOST_NATIVE fallback macro.
set(PFR_DATA_HEADERS_WITH_INCBIN
    "data/graphics/pokemon.h"
    "data/graphics/trainers.h"
    "data/graphics/items.h"
    "data/graphics/mail.h"
    "data/graphics/interface_pokeballs.h"
    "data/graphics/battle_terrain_unused.h"
    "data/tilesets/graphics.h"
    "data/tilesets/metatiles.h"
    "data/object_events/object_event_graphics.h"
    "data/party_menu.h"
)

# Build preproc commands for data headers → PFR_GEN_HEADERS_DIR
set(PFR_DATA_HEADER_PREPROC_COMMANDS)
set(PFR_DATA_HEADER_OUTPUTS)
foreach(header_rel IN LISTS PFR_DATA_HEADERS_WITH_INCBIN)
    set(header_src "${POKEFIRERED_SOURCE_DIR}/src/${header_rel}")
    set(header_out "${PFR_GEN_HEADERS_DIR}/${header_rel}")
    get_filename_component(header_out_dir "${header_out}" DIRECTORY)
    list(APPEND PFR_DATA_HEADER_PREPROC_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory "${header_out_dir}"
        COMMAND $<TARGET_FILE:pfr_assets> preproc
            "${header_src}" "${PFR_INC_DIR}" "${header_out}"
    )
    list(APPEND PFR_DATA_HEADER_OUTPUTS "${header_out}")
endforeach()

file(GLOB_RECURSE PFR_MAP_JSONS "${POKEFIRERED_SOURCE_DIR}/data/maps/*/map.json")
list(SORT PFR_MAP_JSONS)

add_custom_command(
    OUTPUT "${POKEFIRERED_SOURCE_DIR}/tools/mapjson/mapjson"
    COMMAND make -C "${POKEFIRERED_SOURCE_DIR}/tools/mapjson"
    DEPENDS
        "${POKEFIRERED_SOURCE_DIR}/tools/mapjson/Makefile"
        "${POKEFIRERED_SOURCE_DIR}/tools/mapjson/mapjson.cpp"
        "${POKEFIRERED_SOURCE_DIR}/tools/mapjson/mapjson.h"
        "${POKEFIRERED_SOURCE_DIR}/tools/mapjson/json11.cpp"
        "${POKEFIRERED_SOURCE_DIR}/tools/mapjson/json11.h"
    COMMENT "Building upstream mapjson tool"
)

add_custom_command(
    OUTPUT "${POKEFIRERED_SOURCE_DIR}/tools/jsonproc/jsonproc"
    COMMAND make -C "${POKEFIRERED_SOURCE_DIR}/tools/jsonproc"
    DEPENDS
        "${POKEFIRERED_SOURCE_DIR}/tools/jsonproc/Makefile"
        "${POKEFIRERED_SOURCE_DIR}/tools/jsonproc/jsonproc.cpp"
        "${POKEFIRERED_SOURCE_DIR}/tools/jsonproc/jsonproc.h"
        "${POKEFIRERED_SOURCE_DIR}/tools/jsonproc/inja.hpp"
        "${POKEFIRERED_SOURCE_DIR}/tools/jsonproc/nlohmann/json.hpp"
    COMMENT "Building upstream jsonproc tool"
)

add_custom_command(
    OUTPUT
        "${PFR_GEN_HEADERS_DIR}/constants/map_groups.h"
        "${PFR_GEN_HEADERS_DIR}/constants/map_event_ids.h"
        "${PFR_GEN_HEADERS_DIR}/constants/region_map_sections.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_GEN_HEADERS_DIR}/constants"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_MAPJSON_OUT_DIR}"
    COMMAND "${POKEFIRERED_SOURCE_DIR}/tools/mapjson/mapjson" groups firered
        "${POKEFIRERED_SOURCE_DIR}/data/maps/map_groups.json"
        "${PFR_MAPJSON_OUT_DIR}"
        "${PFR_GEN_HEADERS_DIR}/constants"
    COMMAND "${POKEFIRERED_SOURCE_DIR}/tools/mapjson/mapjson" event_constants firered
        ${PFR_MAP_JSONS}
        "${PFR_GEN_HEADERS_DIR}/constants/map_event_ids.h"
    COMMAND "${POKEFIRERED_SOURCE_DIR}/tools/jsonproc/jsonproc"
        "${POKEFIRERED_SOURCE_DIR}/src/data/region_map/region_map_sections.json"
        "${POKEFIRERED_SOURCE_DIR}/src/data/region_map/region_map_sections.constants.json.txt"
        "${PFR_GEN_HEADERS_DIR}/constants/region_map_sections.h"
    DEPENDS
        "${POKEFIRERED_SOURCE_DIR}/tools/mapjson/mapjson"
        "${POKEFIRERED_SOURCE_DIR}/tools/jsonproc/jsonproc"
        "${POKEFIRERED_SOURCE_DIR}/data/maps/map_groups.json"
        "${POKEFIRERED_SOURCE_DIR}/src/data/region_map/region_map_sections.json"
        "${POKEFIRERED_SOURCE_DIR}/src/data/region_map/region_map_sections.constants.json.txt"
        ${PFR_MAP_JSONS}
    COMMENT "Generating upstream constant headers needed by new_game/roamer"
)

add_custom_target(pfr_generated_headers
    DEPENDS
        "${PFR_GEN_HEADERS_DIR}/constants/map_groups.h"
        "${PFR_GEN_HEADERS_DIR}/constants/map_event_ids.h"
        "${PFR_GEN_HEADERS_DIR}/constants/region_map_sections.h"
)

add_custom_command(
    OUTPUT "${PFR_GEN_DIR}/upstream_map_data.c"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/map_data_to_c.py"
        --pokedir "${POKEFIRERED_SOURCE_DIR}"
        --output "${PFR_GEN_DIR}/upstream_map_data.c"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/map_data_to_c.py"
        "${POKEFIRERED_SOURCE_DIR}/data/layouts/layouts.json"
        "${POKEFIRERED_SOURCE_DIR}/data/maps/map_groups.json"
        ${PFR_MAP_JSONS}
    COMMENT "Generating host-native upstream map data"
)

add_custom_command(
    OUTPUT
        "${PFR_GEN_DIR}/upstream_field_effect_scripts.c"
        "${PFR_GEN_DIR}/upstream_field_effect_scripts.h"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/script_assembler.py"
        --pokedir "${POKEFIRERED_SOURCE_DIR}"
        --prefix pfr_fes
        --patch-fn HostPatchFieldEffectScriptPointers
        --exclude-label gFieldEffectScriptPointers
        --output "${PFR_GEN_DIR}/upstream_field_effect_scripts.c"
        "${POKEFIRERED_SOURCE_DIR}/data/field_effect_scripts.s"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/script_assembler.py"
        "${POKEFIRERED_SOURCE_DIR}/data/field_effect_scripts.s"
        "${POKEFIRERED_SOURCE_DIR}/asm/macros/field_effect_script.inc"
    COMMENT "Generating host-native field effect scripts"
)

add_custom_command(
    OUTPUT
        "${PFR_GEN_DIR}/upstream_battle_ai_scripts.c"
        "${PFR_GEN_DIR}/upstream_battle_ai_scripts.h"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/script_assembler.py"
        --pokedir "${POKEFIRERED_SOURCE_DIR}"
        --prefix pfr_bai
        --patch-fn HostPatchBattleAIScriptPointers
        --exclude-label gBattleAI_ScriptsTable
        --output "${PFR_GEN_DIR}/upstream_battle_ai_scripts.c"
        "${POKEFIRERED_SOURCE_DIR}/data/battle_ai_scripts.s"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/script_assembler.py"
        "${POKEFIRERED_SOURCE_DIR}/data/battle_ai_scripts.s"
        "${POKEFIRERED_SOURCE_DIR}/asm/macros/battle_ai_script.inc"
    COMMENT "Generating host-native battle AI scripts"
)

add_custom_command(
    OUTPUT
        "${PFR_GEN_DIR}/upstream_battle_anim_scripts.c"
        "${PFR_GEN_DIR}/upstream_battle_anim_scripts.h"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/script_assembler.py"
        --pokedir "${POKEFIRERED_SOURCE_DIR}"
        --prefix pfr_bas
        --patch-fn HostPatchBattleAnimScriptPointers
        --exclude-label gMovesWithQuietBGM
        --exclude-label gBattleAnims_Moves
        --exclude-label gBattleAnims_StatusConditions
        --exclude-label gBattleAnims_General
        --exclude-label gBattleAnims_Special
        --output "${PFR_GEN_DIR}/upstream_battle_anim_scripts.c"
        "${POKEFIRERED_SOURCE_DIR}/data/battle_anim_scripts.s"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/script_assembler.py"
        "${POKEFIRERED_SOURCE_DIR}/data/battle_anim_scripts.s"
        "${POKEFIRERED_SOURCE_DIR}/asm/macros.inc"
        "${POKEFIRERED_SOURCE_DIR}/asm/macros/battle_anim_script.inc"
        "${POKEFIRERED_SOURCE_DIR}/asm/macros/movement.inc"
        "${POKEFIRERED_SOURCE_DIR}/constants/constants.inc"
    COMMENT "Generating host-native battle animation scripts"
)

add_custom_command(
    OUTPUT "${PFR_GEN_DIR}/upstream_battle_anim_tables.c"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/battle_anim_tables_to_c.py"
        --input "${POKEFIRERED_SOURCE_DIR}/data/battle_anim_scripts.s"
        --output "${PFR_GEN_DIR}/upstream_battle_anim_tables.c"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/battle_anim_tables_to_c.py"
        "${POKEFIRERED_SOURCE_DIR}/data/battle_anim_scripts.s"
        "${PFR_GEN_DIR}/upstream_battle_anim_scripts.h"
    COMMENT "Generating host-native battle animation tables"
)

add_custom_command(
    OUTPUT "${PFR_GEN_DIR}/upstream_specials_table.c"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/specials_to_c.py"
        --input "${POKEFIRERED_SOURCE_DIR}/data/specials.inc"
        --output "${PFR_GEN_DIR}/upstream_specials_table.c"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/specials_to_c.py"
        "${POKEFIRERED_SOURCE_DIR}/data/specials.inc"
    COMMENT "Generating host-native specials table"
)

add_custom_target(pfr_generated_upstream_native_data
    DEPENDS
        "${PFR_GEN_DIR}/upstream_map_data.c"
        "${PFR_GEN_DIR}/upstream_field_effect_scripts.c"
        "${PFR_GEN_DIR}/upstream_field_effect_scripts.h"
        "${PFR_GEN_DIR}/upstream_battle_ai_scripts.c"
        "${PFR_GEN_DIR}/upstream_battle_ai_scripts.h"
        "${PFR_GEN_DIR}/upstream_battle_anim_scripts.c"
        "${PFR_GEN_DIR}/upstream_battle_anim_scripts.h"
        "${PFR_GEN_DIR}/upstream_battle_anim_tables.c"
        "${PFR_GEN_DIR}/upstream_specials_table.c"
)

add_library(pfr_upstream_core STATIC
    # ---- Host shim layer ----
    src/host_crt0.c
    src/host_capture_manifest.c
    src/host_frame_step.c
    src/host_memory.c
    src/host_dma.c
    src/host_bios.c
    src/host_debug.c
    src/host_renderer.c
    src/host_display.c
    src/host_sound_mixer.c
    src/host_sound_init.c
    src/host_flash.c
    src/host_timer.c
    src/host_intro_stubs.c
    src/host_new_game_stubs.c
    src/host_oak_speech_stubs.c
    src/host_title_screen_stubs.c
    src/upstream_stubs.c
    # upstream_text_data.c excluded — all symbols provided by upstream_event_scripts.c
    src/upstream_asm_data.c
    src/upstream_sound_data.c       # auto-generated by tools/sound_data_to_c.py
    src/upstream_sound_samples.S    # auto-generated by tools/sound_data_to_c.py
    src/upstream_battle_scripts.c   # auto-generated by tools/script_assembler.py
    src/upstream_event_scripts.c    # auto-generated by tools/script_assembler.py
    "${PFR_GEN_DIR}/upstream_map_data.c"
    "${PFR_GEN_DIR}/upstream_field_effect_scripts.c"
    "${PFR_GEN_DIR}/upstream_battle_ai_scripts.c"
    "${PFR_GEN_DIR}/upstream_battle_anim_scripts.c"
    "${PFR_GEN_DIR}/upstream_battle_anim_tables.c"
    "${PFR_GEN_DIR}/upstream_specials_table.c"
    # ---- ALL upstream C source files (282 files) ----
    "${POKEFIRERED_SOURCE_DIR}/src/AgbRfu_LinkManager.c"
    # agb_flash*.c replaced by src/host_flash.c
    # bag.c excluded — compiled separately as pfr_upstream_bag (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/battle_ai_script_commands.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_ai_switch_items.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_bug.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_dark.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_dragon.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_effects_1.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_effects_2.c"
    # battle_anim_effects_3.c excluded — compiled separately as pfr_upstream_battle_anim_effects_3 (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_electric.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_fight.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_fire.c"
    # battle_anim_flying.c excluded — compiled separately as pfr_upstream_battle_anim_flying (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_ghost.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_ground.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_ice.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_mon_movement.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_mons.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_normal.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_poison.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_psychic.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_rock.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_smokescreen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_sound_tasks.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_special.c"
    # battle_anim_status_effects.c — compiled from gen (charmap _() strings)
    "${PFR_GEN_DIR}/battle_anim_status_effects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_utility_funcs.c"
    # battle_anim_water.c excluded — compiled separately as pfr_upstream_battle_anim_water (preprocessed)
    # battle_bg.c excluded — compiled separately as pfr_upstream_battle_bg (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_link_opponent.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_link_partner.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_oak_old_man.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_opponent.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_player.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_pokedude.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_safari.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controllers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_gfx_sfx_util.c"
    # battle_interface.c excluded — compiled separately as pfr_upstream_battle_interface (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/battle_intro.c"
    "${PFR_GEN_DIR}/battle_main.c"
    "${PFR_GEN_DIR}/battle_message.c"
    # battle_records.c excluded — compiled separately as pfr_upstream_battle_records (preprocessed)
    # battle_script_commands.c excluded — compiled separately as pfr_upstream_battle_script_commands (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/battle_setup.c"
    # battle_tower.c — compiled from gen (charmap _() strings)
    "${PFR_GEN_DIR}/battle_tower.c"
    # battle_transition.c excluded — compiled separately as pfr_upstream_battle_transition (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/battle_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_util2.c"
    "${PFR_GEN_DIR}/berry.c"
    # berry_crush.c excluded — compiled separately as pfr_upstream_berry_crush (preprocessed)
    # berry_fix_program.c excluded — compiled separately as pfr_upstream_berry_fix
    # berry_pouch.c — compiled from gen (charmap _() strings)
    "${PFR_GEN_DIR}/berry_pouch.c"
    "${POKEFIRERED_SOURCE_DIR}/src/berry_powder.c"
    "${POKEFIRERED_SOURCE_DIR}/src/bg.c"
    "${POKEFIRERED_SOURCE_DIR}/src/bg_regs.c"
    "${POKEFIRERED_SOURCE_DIR}/src/bike.c"
    "${POKEFIRERED_SOURCE_DIR}/src/blend_palette.c"
    "${POKEFIRERED_SOURCE_DIR}/src/blit.c"
    # braille_text.c excluded — compiled separately as pfr_upstream_braille_text (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/buy_menu_helpers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/cable_car_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/cable_club.c"
    "${POKEFIRERED_SOURCE_DIR}/src/cereader_tool.c"
    # clear_save_data_screen.c excluded — compiled separately as pfr_upstream_clear_save
    "${POKEFIRERED_SOURCE_DIR}/src/coins.c"
    "${POKEFIRERED_SOURCE_DIR}/src/coord_event_weather.c"
    # credits.c excluded — compiled separately as pfr_upstream_credits (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/data.c"
    # daycare.c excluded — compiled separately as pfr_upstream_daycare (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/decompress.c"
    "${POKEFIRERED_SOURCE_DIR}/src/decoration.c"
    # digit_obj_util.c excluded — compiled separately as pfr_upstream_digit_obj_util (preprocessed)
    # diploma.c excluded — compiled separately as pfr_upstream_diploma (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/dma3_manager.c"
    # dodrio_berry_picking.c excluded — compiled separately as pfr_upstream_dodrio_berry_picking (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/dodrio_berry_picking_comm.c"
    "${POKEFIRERED_SOURCE_DIR}/src/dynamic_placeholder_text_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/easy_chat.c"
    "${POKEFIRERED_SOURCE_DIR}/src/easy_chat_2.c"
    # easy_chat_3.c excluded — compiled separately as pfr_upstream_easy_chat_3 (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/ereader_helpers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/ereader_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/event_data.c"
    "${POKEFIRERED_SOURCE_DIR}/src/event_object_lock.c"
    # event_object_movement.c excluded — compiled separately as pfr_upstream_event_object_movement (preprocessed, data headers with INCBINs)
    # evolution_graphics.c excluded — compiled separately as pfr_upstream_evolution_graphics (preprocessed)
    # evolution_scene.c excluded — compiled separately as pfr_upstream_evolution_scene (preprocessed)
    # fame_checker.c excluded — compiled separately as pfr_upstream_fame_checker (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/field_camera.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_control_avatar.c"
    # field_door.c excluded — compiled separately as pfr_upstream_field_door (preprocessed)
    # field_effect.c excluded — compiled separately as pfr_upstream_field_effect (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/field_effect_helpers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_fadetransition.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_message_box.c"
    # field_player_avatar.c — compiled from gen (charmap _() strings)
    "${PFR_GEN_DIR}/field_player_avatar.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_poison.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_screen_effect.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_special_scene.c"
    # field_specials.c excluded — compiled separately as pfr_upstream_field_specials (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/field_tasks.c"
    # field_weather.c excluded — compiled separately as pfr_upstream_field_weather (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/field_weather_effects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_weather_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fieldmap.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_berrytree.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_cut.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_dig.c"
    # fldeff_flash.c excluded — compiled separately as pfr_upstream_fldeff_flash (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_poison.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_rocksmash.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_softboiled.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_strength.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_sweetscent.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_teleport.c"
    "${POKEFIRERED_SOURCE_DIR}/src/gpu_regs.c"
    # graphics.c excluded — compiled separately as pfr_upstream_graphics (preprocessed)
    # hall_of_fame.c excluded — compiled separately as pfr_upstream_hall_of_fame (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/heal_location.c"
    # help_message.c excluded — compiled separately as pfr_upstream_help_message (preprocessed)
    # help_system.c excluded — compiled separately as pfr_upstream_help_system (preprocessed)
    # help_system_util.c excluded — compiled separately as pfr_upstream_help_system_util (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/hof_pc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/image_processing_effects.c"
    # intro.c excluded — compiled separately as pfr_upstream_intro (preprocessed)
    # isagbprn.c excluded — host_debug.c provides native debug print implementations
    "${POKEFIRERED_SOURCE_DIR}/src/item.c"
    # item_menu.c excluded — compiled separately as pfr_upstream_item_menu (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/item_menu_icons.c"
    "${POKEFIRERED_SOURCE_DIR}/src/item_pc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/item_use.c"
    # itemfinder.c excluded — compiled separately as pfr_upstream_itemfinder (preprocessed)
    "${PFR_GEN_DIR}/keyboard_text.c"
    # learn_move.c excluded — compiled separately as pfr_upstream_learn_move (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/librfu_intr.c"
    "${POKEFIRERED_SOURCE_DIR}/src/librfu_rfu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/librfu_sio32id.c"
    "${POKEFIRERED_SOURCE_DIR}/src/librfu_stwi.c"
    # link.c excluded — compiled separately as pfr_upstream_link (preprocessed)
    # link_rfu_2.c — compiled from gen (charmap _() strings)
    "${PFR_GEN_DIR}/link_rfu_2.c"
    # link_rfu_3.c excluded — compiled separately as pfr_upstream_link_rfu_3 (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/list_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/load_save.c"
    "${POKEFIRERED_SOURCE_DIR}/src/m4a.c"
    "${POKEFIRERED_SOURCE_DIR}/src/m4a_tables.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mail.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mail_data.c"
    # mailbox_pc.c — compiled from gen (charmap _() strings)
    "${PFR_GEN_DIR}/mailbox_pc.c"
    # main.c excluded — compiled separately as pfr_upstream_runtime
    # main_menu.c excluded — compiled separately as pfr_upstream_main_menu (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/malloc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/map_name_popup.c"
    # map_preview_screen.c excluded — compiled separately as pfr_upstream_map_preview_screen (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/math_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/menu2.c"
    "${POKEFIRERED_SOURCE_DIR}/src/menu_helpers.c"
    # menu_indicators.c excluded — compiled separately as pfr_upstream_menu_indicators (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/metatile_behavior.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mini_printf.c"
    # minigame_countdown.c excluded — compiled separately as pfr_upstream_minigame_countdown (preprocessed)
    # mon_markings.c excluded — compiled separately as pfr_upstream_mon_markings (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/money.c"
    "${PFR_GEN_DIR}/move_descriptions.c"
    # multiboot.c excluded — stubs in host_intro_stubs.c / host_title_screen_stubs.c
    "${PFR_GEN_DIR}/mystery_event_msg.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_event_script.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_client.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_link.c"
    # mystery_gift_menu.c excluded — compiled separately as pfr_upstream_mystery_gift_menu (preprocessed)
    # mystery_gift_scripts.c — compiled from gen (charmap _() strings)
    "${PFR_GEN_DIR}/mystery_gift_scripts.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_server.c"
    # mystery_gift_show_card.c excluded — compiled separately as pfr_upstream_mystery_gift_show_card (preprocessed)
    # mystery_gift_show_news.c excluded — compiled separately as pfr_upstream_mystery_gift_show_news (preprocessed)
    # naming_screen.c excluded — compiled separately as pfr_upstream_naming_screen (preprocessed)
    # new_game.c excluded — compiled separately as pfr_upstream_new_game (with renames)
    # new_menu_helpers.c excluded — compiled separately as pfr_upstream_new_menu_helpers (preprocessed)
    # oak_speech.c excluded — compiled separately as pfr_upstream_oak_speech (preprocessed, with rename)
    # option_menu.c excluded — compiled separately as pfr_upstream_option_menu (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/overworld.c"
    "${POKEFIRERED_SOURCE_DIR}/src/palette.c"
    "${POKEFIRERED_SOURCE_DIR}/src/palette_util.c"
    # party_menu.c excluded — compiled separately as pfr_upstream_party_menu (preprocessed, data headers with INCBINs)
    "${POKEFIRERED_SOURCE_DIR}/src/party_menu_specials.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pc_screen_effect.c"
    # play_time.c excluded — compiled separately as pfr_upstream_play_time (with rename)
    "${POKEFIRERED_SOURCE_DIR}/src/player_pc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokeball.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokedex.c"
    # pokedex_area_markers.c excluded — compiled separately as pfr_upstream_pokedex_area_markers (preprocessed)
    # pokedex_screen.c excluded — compiled separately as pfr_upstream_pokedex_screen (preprocessed)
    # pokemon.c excluded — compiled separately as pfr_upstream_pokemon (preprocessed)
    # pokemon_icon.c excluded — compiled separately as pfr_upstream_pokemon_icon (preprocessed)
    # pokemon_jump.c excluded — compiled separately as pfr_upstream_pokemon_jump (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_size_record.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_special_anim.c"
    # pokemon_special_anim_scene.c excluded — compiled separately as pfr_upstream_pokemon_special_anim_scene (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system.c"
    # pokemon_storage_system_data.c excluded — compiled separately as pfr_upstream_pokemon_storage_system_data (preprocessed)
    # pokemon_storage_system_graphics.c excluded — compiled separately as pfr_upstream_pokemon_storage_system_graphics (preprocessed)
    # pokemon_storage_system_menu.c excluded — compiled separately as pfr_upstream_pokemon_storage_system_menu (preprocessed)
    # pokemon_storage_system_misc.c excluded — compiled separately as pfr_upstream_pokemon_storage_system_misc (preprocessed)
    # pokemon_storage_system_tasks.c excluded — compiled separately as pfr_upstream_pokemon_storage_system_tasks (preprocessed)
    # pokemon_summary_screen.c excluded — compiled separately as pfr_upstream_pokemon_summary_screen (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/post_battle_event_funcs.c"
    "${POKEFIRERED_SOURCE_DIR}/src/prof_pc.c"
    # quest_log.c excluded — compiled separately as pfr_upstream_quest_log (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log_battle.c"
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log_events.c"
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log_objects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log_player.c"
    "${POKEFIRERED_SOURCE_DIR}/src/random.c"
    # region_map.c excluded — compiled separately as pfr_upstream_region_map (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/renewable_hidden_items.c"
    "${POKEFIRERED_SOURCE_DIR}/src/reset_save_heap.c"
    "${POKEFIRERED_SOURCE_DIR}/src/reshow_battle_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/roamer.c"
    "${POKEFIRERED_SOURCE_DIR}/src/rom_header_gf.c"
    "${POKEFIRERED_SOURCE_DIR}/src/safari_zone.c"
    "${POKEFIRERED_SOURCE_DIR}/src/save.c"
    # save_failed_screen.c excluded — compiled separately as pfr_upstream_save_failed_screen (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/save_location.c"
    "${POKEFIRERED_SOURCE_DIR}/src/save_menu_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/scanline_effect.c"
    # scrcmd.c — compiled from gen (charmap _() strings)
    "${PFR_GEN_DIR}/scrcmd.c"
    "${POKEFIRERED_SOURCE_DIR}/src/script.c"
    # script_menu.c excluded — compiled separately as pfr_upstream_script_menu (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/script_movement.c"
    "${POKEFIRERED_SOURCE_DIR}/src/script_pokemon_util.c"
    # seagallop.c excluded — compiled separately as pfr_upstream_seagallop (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/shop.c"
    # slot_machine.c excluded — compiled separately as pfr_upstream_slot_machine (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/sound.c"
    "${POKEFIRERED_SOURCE_DIR}/src/special_field_anim.c"
    "${POKEFIRERED_SOURCE_DIR}/src/sprite.c"
    # ss_anne.c excluded — compiled separately as pfr_upstream_ss_anne (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/start_menu.c"
    # string_util.c — compiled from gen (charmap __() strings)
    "${PFR_GEN_DIR}/string_util.c"
    # strings.c — compiled from gen (charmap-processed _() strings)
    "${PFR_GEN_DIR}/strings.c"
    "${POKEFIRERED_SOURCE_DIR}/src/task.c"
    "${POKEFIRERED_SOURCE_DIR}/src/teachy_tv.c"
    # text.c excluded — compiled separately as pfr_upstream_text (preprocessed)
    # text_printer.c excluded — compiled separately as pfr_upstream_text_printer
    # text_window.c excluded — compiled separately as pfr_upstream_text_window
    # text_window_graphics.c excluded — compiled separately as pfr_upstream_text_window_graphics (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/tilemap_util.c"
    # tileset_anims.c excluded — compiled separately as pfr_upstream_tileset_anims (preprocessed)
    # tilesets.c excluded — compiled separately as pfr_upstream_tilesets (preprocessed, data headers with INCBINs)
    # title_screen.c excluded — compiled separately as pfr_upstream_title (preprocessed)
    # tm_case.c — compiled from gen (charmap _() strings)
    "${PFR_GEN_DIR}/tm_case.c"
    # trade.c excluded — compiled separately as pfr_upstream_trade (preprocessed)
    # trade_scene.c excluded — compiled separately as pfr_upstream_trade_scene (preprocessed)
    # trainer_card.c excluded — compiled separately as pfr_upstream_trainer_card (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_fan_club.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_pokemon_sprites.c"
    # trainer_see.c excluded — compiled separately as pfr_upstream_trainer_see (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_tower.c"
    "${PFR_GEN_DIR}/trainer_tower_sets.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trig.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_battle.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat.c"
    # union_room_chat_display.c excluded — compiled separately as pfr_upstream_union_room_chat_display (preprocessed)
    # union_room_chat_objects.c excluded — compiled separately as pfr_upstream_union_room_chat_objects (preprocessed)
    "${PFR_GEN_DIR}/union_room_message.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_player_avatar.c"
    # util.c excluded — compiled separately as pfr_upstream_util (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/vs_seeker.c"
    "${POKEFIRERED_SOURCE_DIR}/src/wild_encounter.c"
    "${POKEFIRERED_SOURCE_DIR}/src/wild_pokemon_area.c"
    "${POKEFIRERED_SOURCE_DIR}/src/window.c"
    "${POKEFIRERED_SOURCE_DIR}/src/window_8bpp.c"
    # wireless_communication_status_screen.c excluded — compiled separately as pfr_upstream_wireless_communication_status_screen (preprocessed)
    "${POKEFIRERED_SOURCE_DIR}/src/wonder_news.c"
)

target_compile_definitions(pfr_upstream_core PRIVATE ${PFR_COMMON_DEFINES})
# Upstream headers must be quote-only to avoid colliding with the system's <strings.h>.
target_compile_options(pfr_upstream_core PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${PFR_INC_DIR}"
    "-iquote${PFR_GEN_DIR}"
    "-iquote${PFR_GEN_HEADERS_DIR}"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
    "-iquote${POKEFIRERED_SOURCE_DIR}/src"
)
target_include_directories(pfr_upstream_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
# .incbin in upstream_sound_samples.S resolves relative to the source root
set_source_files_properties(src/upstream_sound_samples.S PROPERTIES
    COMPILE_FLAGS "-Wa,-I${CMAKE_CURRENT_SOURCE_DIR}"
)
add_dependencies(pfr_upstream_core
    pfr_generated_headers
    pfr_asset_pipeline
    pfr_generated_upstream_native_data
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
    "-iquote${PFR_GEN_HEADERS_DIR}"
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
    "-iquote${PFR_GEN_HEADERS_DIR}"
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
    "-iquote${PFR_GEN_HEADERS_DIR}"
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
    "-iquote${PFR_GEN_HEADERS_DIR}"
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
    "-iquote${PFR_GEN_HEADERS_DIR}"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_berry_fix PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_oak_speech OBJECT
    "${PFR_GEN_DIR}/oak_speech.c"
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
    "-iquote${PFR_GEN_HEADERS_DIR}"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_oak_speech PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_new_game OBJECT
    "${POKEFIRERED_SOURCE_DIR}/src/new_game.c"
)
target_compile_definitions(pfr_upstream_new_game PRIVATE
    ${PFR_COMMON_DEFINES}
    ResetMenuAndMonGlobals=UpstreamResetMenuAndMonGlobals
    Sav2_ClearSetDefault=UpstreamSav2_ClearSetDefault
)
target_compile_options(pfr_upstream_new_game PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${PFR_GEN_HEADERS_DIR}"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_new_game PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_new_game pfr_generated_headers)

add_library(pfr_upstream_play_time OBJECT
    "${POKEFIRERED_SOURCE_DIR}/src/play_time.c"
)
target_compile_definitions(pfr_upstream_play_time PRIVATE
    ${PFR_COMMON_DEFINES}
    PlayTimeCounter_Update=UpstreamPlayTimeCounter_Update
)
target_compile_options(pfr_upstream_play_time PRIVATE
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${PFR_GEN_HEADERS_DIR}"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_play_time PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# --- Text/Window/Font subsystem (preprocessed TUs) ---

set(PFR_UPSTREAM_PREPROC_OPTIONS
    -fno-pie
    -ffunction-sections
    -fdata-sections
    -Wno-int-conversion
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${PFR_GEN_HEADERS_DIR}"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)

add_library(pfr_upstream_main_menu OBJECT
    "${PFR_GEN_DIR}/main_menu.c"
)
target_compile_definitions(pfr_upstream_main_menu PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_main_menu PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_main_menu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_text_window_graphics OBJECT
    "${PFR_GEN_DIR}/text_window_graphics.c"
)
target_compile_definitions(pfr_upstream_text_window_graphics PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_text_window_graphics PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_text_window_graphics PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_text OBJECT
    "${PFR_GEN_DIR}/text.c"
)
target_compile_definitions(pfr_upstream_text PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_text PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_text PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_text_window OBJECT
    "${POKEFIRERED_SOURCE_DIR}/src/text_window.c"
)
target_compile_definitions(pfr_upstream_text_window PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_text_window PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_text_window PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_new_menu_helpers OBJECT
    "${PFR_GEN_DIR}/new_menu_helpers.c"
)
target_compile_definitions(pfr_upstream_new_menu_helpers PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_new_menu_helpers PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_new_menu_helpers PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# text_printer.c provides SetFontsPointer, AddTextPrinter*, RunTextPrinters, etc.
add_library(pfr_upstream_text_printer OBJECT
    "${POKEFIRERED_SOURCE_DIR}/src/text_printer.c"
)
target_compile_definitions(pfr_upstream_text_printer PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_text_printer PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_text_printer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(pfr_upstream_graphics OBJECT
    "${PFR_GEN_DIR}/graphics.c"
)
target_compile_definitions(pfr_upstream_graphics PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_graphics PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_graphics PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)

add_dependencies(pfr_upstream_intro pfr_asset_pipeline)
add_dependencies(pfr_upstream_title pfr_asset_pipeline)
add_dependencies(pfr_upstream_main_menu pfr_asset_pipeline)
add_dependencies(pfr_upstream_oak_speech pfr_asset_pipeline)
add_dependencies(pfr_upstream_text_window_graphics pfr_asset_pipeline)
add_dependencies(pfr_upstream_text pfr_asset_pipeline)
add_dependencies(pfr_upstream_new_menu_helpers pfr_asset_pipeline)
add_dependencies(pfr_upstream_graphics pfr_asset_pipeline)

add_library(pfr_upstream_bag OBJECT
    "${PFR_GEN_DIR}/bag.c"
)
target_compile_definitions(pfr_upstream_bag PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_bag PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_bag PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_bag pfr_asset_pipeline)

add_library(pfr_upstream_battle_anim_effects_3 OBJECT
    "${PFR_GEN_DIR}/battle_anim_effects_3.c"
)
target_compile_definitions(pfr_upstream_battle_anim_effects_3 PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_battle_anim_effects_3 PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_battle_anim_effects_3 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_battle_anim_effects_3 pfr_asset_pipeline)

add_library(pfr_upstream_battle_anim_flying OBJECT
    "${PFR_GEN_DIR}/battle_anim_flying.c"
)
target_compile_definitions(pfr_upstream_battle_anim_flying PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_battle_anim_flying PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_battle_anim_flying PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_battle_anim_flying pfr_asset_pipeline)

add_library(pfr_upstream_battle_anim_water OBJECT
    "${PFR_GEN_DIR}/battle_anim_water.c"
)
target_compile_definitions(pfr_upstream_battle_anim_water PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_battle_anim_water PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_battle_anim_water PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_battle_anim_water pfr_asset_pipeline)

add_library(pfr_upstream_battle_bg OBJECT
    "${PFR_GEN_DIR}/battle_bg.c"
)
target_compile_definitions(pfr_upstream_battle_bg PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_battle_bg PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_battle_bg PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_battle_bg pfr_asset_pipeline)

add_library(pfr_upstream_battle_interface OBJECT
    "${PFR_GEN_DIR}/battle_interface.c"
)
target_compile_definitions(pfr_upstream_battle_interface PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_battle_interface PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_battle_interface PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_battle_interface pfr_asset_pipeline)

add_library(pfr_upstream_battle_records OBJECT
    "${PFR_GEN_DIR}/battle_records.c"
)
target_compile_definitions(pfr_upstream_battle_records PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_battle_records PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_battle_records PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_battle_records pfr_asset_pipeline)

add_library(pfr_upstream_battle_script_commands OBJECT
    "${PFR_GEN_DIR}/battle_script_commands.c"
)
target_compile_definitions(pfr_upstream_battle_script_commands PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_battle_script_commands PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_battle_script_commands PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_battle_script_commands pfr_asset_pipeline)

add_library(pfr_upstream_battle_transition OBJECT
    "${PFR_GEN_DIR}/battle_transition.c"
)
target_compile_definitions(pfr_upstream_battle_transition PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_battle_transition PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_battle_transition PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_battle_transition pfr_asset_pipeline)

add_library(pfr_upstream_berry_crush OBJECT
    "${PFR_GEN_DIR}/berry_crush.c"
)
target_compile_definitions(pfr_upstream_berry_crush PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_berry_crush PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_berry_crush PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_berry_crush pfr_asset_pipeline)

add_library(pfr_upstream_braille_text OBJECT
    "${PFR_GEN_DIR}/braille_text.c"
)
target_compile_definitions(pfr_upstream_braille_text PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_braille_text PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_braille_text PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_braille_text pfr_asset_pipeline)

add_library(pfr_upstream_credits OBJECT
    "${PFR_GEN_DIR}/credits.c"
)
target_compile_definitions(pfr_upstream_credits PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_credits PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_credits PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_credits pfr_asset_pipeline)

add_library(pfr_upstream_daycare OBJECT
    "${PFR_GEN_DIR}/daycare.c"
)
target_compile_definitions(pfr_upstream_daycare PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_daycare PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_daycare PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_daycare pfr_asset_pipeline)

add_library(pfr_upstream_digit_obj_util OBJECT
    "${PFR_GEN_DIR}/digit_obj_util.c"
)
target_compile_definitions(pfr_upstream_digit_obj_util PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_digit_obj_util PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_digit_obj_util PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_digit_obj_util pfr_asset_pipeline)

add_library(pfr_upstream_diploma OBJECT
    "${PFR_GEN_DIR}/diploma.c"
)
target_compile_definitions(pfr_upstream_diploma PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_diploma PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_diploma PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_diploma pfr_asset_pipeline)

add_library(pfr_upstream_dodrio_berry_picking OBJECT
    "${PFR_GEN_DIR}/dodrio_berry_picking.c"
)
target_compile_definitions(pfr_upstream_dodrio_berry_picking PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_dodrio_berry_picking PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_dodrio_berry_picking PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_dodrio_berry_picking pfr_asset_pipeline)

add_library(pfr_upstream_easy_chat_3 OBJECT
    "${PFR_GEN_DIR}/easy_chat_3.c"
)
target_compile_definitions(pfr_upstream_easy_chat_3 PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_easy_chat_3 PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_easy_chat_3 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_easy_chat_3 pfr_asset_pipeline)

add_library(pfr_upstream_evolution_graphics OBJECT
    "${PFR_GEN_DIR}/evolution_graphics.c"
)
target_compile_definitions(pfr_upstream_evolution_graphics PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_evolution_graphics PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_evolution_graphics PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_evolution_graphics pfr_asset_pipeline)

add_library(pfr_upstream_evolution_scene OBJECT
    "${PFR_GEN_DIR}/evolution_scene.c"
)
target_compile_definitions(pfr_upstream_evolution_scene PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_evolution_scene PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_evolution_scene PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_evolution_scene pfr_asset_pipeline)

add_library(pfr_upstream_fame_checker OBJECT
    "${PFR_GEN_DIR}/fame_checker.c"
)
target_compile_definitions(pfr_upstream_fame_checker PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_fame_checker PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_fame_checker PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_fame_checker pfr_asset_pipeline)

add_library(pfr_upstream_field_door OBJECT
    "${PFR_GEN_DIR}/field_door.c"
)
target_compile_definitions(pfr_upstream_field_door PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_field_door PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_field_door PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_field_door pfr_asset_pipeline)

add_library(pfr_upstream_field_effect OBJECT
    "${PFR_GEN_DIR}/field_effect.c"
)
target_compile_definitions(pfr_upstream_field_effect PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_field_effect PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_field_effect PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_field_effect pfr_asset_pipeline)

add_library(pfr_upstream_field_specials OBJECT
    "${PFR_GEN_DIR}/field_specials.c"
)
target_compile_definitions(pfr_upstream_field_specials PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_field_specials PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_field_specials PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_field_specials pfr_asset_pipeline)

add_library(pfr_upstream_field_weather OBJECT
    "${PFR_GEN_DIR}/field_weather.c"
)
target_compile_definitions(pfr_upstream_field_weather PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_field_weather PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_field_weather PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_field_weather pfr_asset_pipeline)

add_library(pfr_upstream_fldeff_flash OBJECT
    "${PFR_GEN_DIR}/fldeff_flash.c"
)
target_compile_definitions(pfr_upstream_fldeff_flash PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_fldeff_flash PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_fldeff_flash PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_fldeff_flash pfr_asset_pipeline)

add_library(pfr_upstream_hall_of_fame OBJECT
    "${PFR_GEN_DIR}/hall_of_fame.c"
)
target_compile_definitions(pfr_upstream_hall_of_fame PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_hall_of_fame PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_hall_of_fame PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_hall_of_fame pfr_asset_pipeline)

add_library(pfr_upstream_help_message OBJECT
    "${PFR_GEN_DIR}/help_message.c"
)
target_compile_definitions(pfr_upstream_help_message PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_help_message PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_help_message PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_help_message pfr_asset_pipeline)

add_library(pfr_upstream_help_system OBJECT
    "${PFR_GEN_DIR}/help_system.c"
)
target_compile_definitions(pfr_upstream_help_system PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_help_system PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_help_system PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_help_system pfr_asset_pipeline)

add_library(pfr_upstream_help_system_util OBJECT
    "${PFR_GEN_DIR}/help_system_util.c"
)
target_compile_definitions(pfr_upstream_help_system_util PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_help_system_util PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_help_system_util PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_help_system_util pfr_asset_pipeline)

add_library(pfr_upstream_item_menu OBJECT
    "${PFR_GEN_DIR}/item_menu.c"
)
target_compile_definitions(pfr_upstream_item_menu PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_item_menu PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_item_menu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_item_menu pfr_asset_pipeline)

add_library(pfr_upstream_itemfinder OBJECT
    "${PFR_GEN_DIR}/itemfinder.c"
)
target_compile_definitions(pfr_upstream_itemfinder PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_itemfinder PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_itemfinder PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_itemfinder pfr_asset_pipeline)

add_library(pfr_upstream_learn_move OBJECT
    "${PFR_GEN_DIR}/learn_move.c"
)
target_compile_definitions(pfr_upstream_learn_move PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_learn_move PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_learn_move PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_learn_move pfr_asset_pipeline)

add_library(pfr_upstream_link OBJECT
    "${PFR_GEN_DIR}/link.c"
)
target_compile_definitions(pfr_upstream_link PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_link PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_link PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_link pfr_asset_pipeline)

add_library(pfr_upstream_link_rfu_3 OBJECT
    "${PFR_GEN_DIR}/link_rfu_3.c"
)
target_compile_definitions(pfr_upstream_link_rfu_3 PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_link_rfu_3 PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_link_rfu_3 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_link_rfu_3 pfr_asset_pipeline)

add_library(pfr_upstream_map_preview_screen OBJECT
    "${PFR_GEN_DIR}/map_preview_screen.c"
)
target_compile_definitions(pfr_upstream_map_preview_screen PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_map_preview_screen PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_map_preview_screen PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_map_preview_screen pfr_asset_pipeline)

add_library(pfr_upstream_menu_indicators OBJECT
    "${PFR_GEN_DIR}/menu_indicators.c"
)
target_compile_definitions(pfr_upstream_menu_indicators PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_menu_indicators PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_menu_indicators PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_menu_indicators pfr_asset_pipeline)

add_library(pfr_upstream_minigame_countdown OBJECT
    "${PFR_GEN_DIR}/minigame_countdown.c"
)
target_compile_definitions(pfr_upstream_minigame_countdown PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_minigame_countdown PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_minigame_countdown PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_minigame_countdown pfr_asset_pipeline)

add_library(pfr_upstream_mon_markings OBJECT
    "${PFR_GEN_DIR}/mon_markings.c"
)
target_compile_definitions(pfr_upstream_mon_markings PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_mon_markings PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_mon_markings PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_mon_markings pfr_asset_pipeline)

add_library(pfr_upstream_mystery_gift_menu OBJECT
    "${PFR_GEN_DIR}/mystery_gift_menu.c"
)
target_compile_definitions(pfr_upstream_mystery_gift_menu PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_mystery_gift_menu PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_mystery_gift_menu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_mystery_gift_menu pfr_asset_pipeline)

add_library(pfr_upstream_mystery_gift_show_card OBJECT
    "${PFR_GEN_DIR}/mystery_gift_show_card.c"
)
target_compile_definitions(pfr_upstream_mystery_gift_show_card PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_mystery_gift_show_card PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_mystery_gift_show_card PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_mystery_gift_show_card pfr_asset_pipeline)

add_library(pfr_upstream_mystery_gift_show_news OBJECT
    "${PFR_GEN_DIR}/mystery_gift_show_news.c"
)
target_compile_definitions(pfr_upstream_mystery_gift_show_news PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_mystery_gift_show_news PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_mystery_gift_show_news PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_mystery_gift_show_news pfr_asset_pipeline)

add_library(pfr_upstream_naming_screen OBJECT
    "${PFR_GEN_DIR}/naming_screen.c"
)
target_compile_definitions(pfr_upstream_naming_screen PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_naming_screen PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_naming_screen PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_naming_screen pfr_asset_pipeline)

add_library(pfr_upstream_option_menu OBJECT
    "${PFR_GEN_DIR}/option_menu.c"
)
target_compile_definitions(pfr_upstream_option_menu PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_option_menu PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_option_menu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_option_menu pfr_asset_pipeline)

add_library(pfr_upstream_pokedex_area_markers OBJECT
    "${PFR_GEN_DIR}/pokedex_area_markers.c"
)
target_compile_definitions(pfr_upstream_pokedex_area_markers PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokedex_area_markers PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokedex_area_markers PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokedex_area_markers pfr_asset_pipeline)

add_library(pfr_upstream_pokedex_screen OBJECT
    "${PFR_GEN_DIR}/pokedex_screen.c"
)
target_compile_definitions(pfr_upstream_pokedex_screen PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokedex_screen PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokedex_screen PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokedex_screen pfr_asset_pipeline)

add_library(pfr_upstream_pokemon OBJECT
    "${PFR_GEN_DIR}/pokemon.c"
)
target_compile_definitions(pfr_upstream_pokemon PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon pfr_asset_pipeline)

add_library(pfr_upstream_pokemon_icon OBJECT
    "${PFR_GEN_DIR}/pokemon_icon.c"
)
target_compile_definitions(pfr_upstream_pokemon_icon PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon_icon PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon_icon PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon_icon pfr_asset_pipeline)

add_library(pfr_upstream_pokemon_jump OBJECT
    "${PFR_GEN_DIR}/pokemon_jump.c"
)
target_compile_definitions(pfr_upstream_pokemon_jump PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon_jump PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon_jump PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon_jump pfr_asset_pipeline)

add_library(pfr_upstream_pokemon_special_anim_scene OBJECT
    "${PFR_GEN_DIR}/pokemon_special_anim_scene.c"
)
target_compile_definitions(pfr_upstream_pokemon_special_anim_scene PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon_special_anim_scene PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon_special_anim_scene PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon_special_anim_scene pfr_asset_pipeline)

add_library(pfr_upstream_pokemon_storage_system_data OBJECT
    "${PFR_GEN_DIR}/pokemon_storage_system_data.c"
)
target_compile_definitions(pfr_upstream_pokemon_storage_system_data PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon_storage_system_data PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon_storage_system_data PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon_storage_system_data pfr_asset_pipeline)

add_library(pfr_upstream_pokemon_storage_system_graphics OBJECT
    "${PFR_GEN_DIR}/pokemon_storage_system_graphics.c"
)
target_compile_definitions(pfr_upstream_pokemon_storage_system_graphics PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon_storage_system_graphics PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon_storage_system_graphics PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon_storage_system_graphics pfr_asset_pipeline)

add_library(pfr_upstream_pokemon_storage_system_menu OBJECT
    "${PFR_GEN_DIR}/pokemon_storage_system_menu.c"
)
target_compile_definitions(pfr_upstream_pokemon_storage_system_menu PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon_storage_system_menu PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon_storage_system_menu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon_storage_system_menu pfr_asset_pipeline)

add_library(pfr_upstream_pokemon_storage_system_misc OBJECT
    "${PFR_GEN_DIR}/pokemon_storage_system_misc.c"
)
target_compile_definitions(pfr_upstream_pokemon_storage_system_misc PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon_storage_system_misc PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon_storage_system_misc PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon_storage_system_misc pfr_asset_pipeline)

add_library(pfr_upstream_pokemon_storage_system_tasks OBJECT
    "${PFR_GEN_DIR}/pokemon_storage_system_tasks.c"
)
target_compile_definitions(pfr_upstream_pokemon_storage_system_tasks PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon_storage_system_tasks PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon_storage_system_tasks PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon_storage_system_tasks pfr_asset_pipeline)

add_library(pfr_upstream_pokemon_summary_screen OBJECT
    "${PFR_GEN_DIR}/pokemon_summary_screen.c"
)
target_compile_definitions(pfr_upstream_pokemon_summary_screen PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_pokemon_summary_screen PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_pokemon_summary_screen PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_pokemon_summary_screen pfr_asset_pipeline)

add_library(pfr_upstream_quest_log OBJECT
    "${PFR_GEN_DIR}/quest_log.c"
)
target_compile_definitions(pfr_upstream_quest_log PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_quest_log PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_quest_log PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_quest_log pfr_asset_pipeline)

add_library(pfr_upstream_region_map OBJECT
    "${PFR_GEN_DIR}/region_map.c"
)
target_compile_definitions(pfr_upstream_region_map PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_region_map PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_region_map PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_region_map pfr_asset_pipeline)

add_library(pfr_upstream_save_failed_screen OBJECT
    "${PFR_GEN_DIR}/save_failed_screen.c"
)
target_compile_definitions(pfr_upstream_save_failed_screen PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_save_failed_screen PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_save_failed_screen PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_save_failed_screen pfr_asset_pipeline)

add_library(pfr_upstream_script_menu OBJECT
    "${PFR_GEN_DIR}/script_menu.c"
)
target_compile_definitions(pfr_upstream_script_menu PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_script_menu PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_script_menu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_script_menu pfr_asset_pipeline)

add_library(pfr_upstream_seagallop OBJECT
    "${PFR_GEN_DIR}/seagallop.c"
)
target_compile_definitions(pfr_upstream_seagallop PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_seagallop PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_seagallop PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_seagallop pfr_asset_pipeline)

add_library(pfr_upstream_slot_machine OBJECT
    "${PFR_GEN_DIR}/slot_machine.c"
)
target_compile_definitions(pfr_upstream_slot_machine PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_slot_machine PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_slot_machine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_slot_machine pfr_asset_pipeline)

add_library(pfr_upstream_ss_anne OBJECT
    "${PFR_GEN_DIR}/ss_anne.c"
)
target_compile_definitions(pfr_upstream_ss_anne PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_ss_anne PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_ss_anne PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_ss_anne pfr_asset_pipeline)

add_library(pfr_upstream_tileset_anims OBJECT
    "${PFR_GEN_DIR}/tileset_anims.c"
)
target_compile_definitions(pfr_upstream_tileset_anims PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_tileset_anims PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_tileset_anims PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_tileset_anims pfr_asset_pipeline)

add_library(pfr_upstream_trade OBJECT
    "${PFR_GEN_DIR}/trade.c"
)
target_compile_definitions(pfr_upstream_trade PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_trade PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_trade PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_trade pfr_asset_pipeline)

add_library(pfr_upstream_trade_scene OBJECT
    "${PFR_GEN_DIR}/trade_scene.c"
)
target_compile_definitions(pfr_upstream_trade_scene PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_trade_scene PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_trade_scene PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_trade_scene pfr_asset_pipeline)

add_library(pfr_upstream_trainer_card OBJECT
    "${PFR_GEN_DIR}/trainer_card.c"
)
target_compile_definitions(pfr_upstream_trainer_card PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_trainer_card PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_trainer_card PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_trainer_card pfr_asset_pipeline)

add_library(pfr_upstream_trainer_see OBJECT
    "${PFR_GEN_DIR}/trainer_see.c"
)
target_compile_definitions(pfr_upstream_trainer_see PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_trainer_see PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_trainer_see PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_trainer_see pfr_asset_pipeline)

add_library(pfr_upstream_union_room_chat_display OBJECT
    "${PFR_GEN_DIR}/union_room_chat_display.c"
)
target_compile_definitions(pfr_upstream_union_room_chat_display PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_union_room_chat_display PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_union_room_chat_display PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_union_room_chat_display pfr_asset_pipeline)

add_library(pfr_upstream_union_room_chat_objects OBJECT
    "${PFR_GEN_DIR}/union_room_chat_objects.c"
)
target_compile_definitions(pfr_upstream_union_room_chat_objects PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_union_room_chat_objects PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_union_room_chat_objects PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_union_room_chat_objects pfr_asset_pipeline)

add_library(pfr_upstream_util OBJECT
    "${PFR_GEN_DIR}/util.c"
)
target_compile_definitions(pfr_upstream_util PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_util PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_util PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_util pfr_asset_pipeline)

add_library(pfr_upstream_wireless_communication_status_screen OBJECT
    "${PFR_GEN_DIR}/wireless_communication_status_screen.c"
)
target_compile_definitions(pfr_upstream_wireless_communication_status_screen PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_wireless_communication_status_screen PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_wireless_communication_status_screen PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_wireless_communication_status_screen pfr_asset_pipeline)

# Source files with data headers containing INCBINs — must be compiled from gen/
# so the -iquote path finds preprocessed headers in PFR_GEN_HEADERS_DIR before upstream src/
add_library(pfr_upstream_tilesets OBJECT
    "${PFR_GEN_DIR}/tilesets.c"
)
target_compile_definitions(pfr_upstream_tilesets PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_tilesets PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_tilesets PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_tilesets pfr_asset_pipeline)

add_library(pfr_upstream_event_object_movement OBJECT
    "${PFR_GEN_DIR}/event_object_movement.c"
)
target_compile_definitions(pfr_upstream_event_object_movement PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_event_object_movement PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_event_object_movement PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_event_object_movement pfr_asset_pipeline)

add_library(pfr_upstream_party_menu OBJECT
    "${PFR_GEN_DIR}/party_menu.c"
)
target_compile_definitions(pfr_upstream_party_menu PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_upstream_party_menu PRIVATE ${PFR_UPSTREAM_PREPROC_OPTIONS})
target_include_directories(pfr_upstream_party_menu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${POKEFIRERED_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_party_menu pfr_asset_pipeline)

add_executable(pfr_smoke tests/smoke.c)
target_sources(pfr_smoke PRIVATE
    src/host_agbmain.c
    $<TARGET_OBJECTS:pfr_upstream_intro>
    $<TARGET_OBJECTS:pfr_upstream_title>
    $<TARGET_OBJECTS:pfr_upstream_clear_save>
    $<TARGET_OBJECTS:pfr_upstream_berry_fix>
    $<TARGET_OBJECTS:pfr_upstream_main_menu>
    $<TARGET_OBJECTS:pfr_upstream_oak_speech>
    $<TARGET_OBJECTS:pfr_upstream_new_game>
    $<TARGET_OBJECTS:pfr_upstream_play_time>
    $<TARGET_OBJECTS:pfr_upstream_text_window_graphics>
    $<TARGET_OBJECTS:pfr_upstream_text>
    $<TARGET_OBJECTS:pfr_upstream_text_window>
    $<TARGET_OBJECTS:pfr_upstream_new_menu_helpers>
    $<TARGET_OBJECTS:pfr_upstream_text_printer>
    $<TARGET_OBJECTS:pfr_upstream_runtime>
    $<TARGET_OBJECTS:pfr_upstream_graphics>
    $<TARGET_OBJECTS:pfr_upstream_bag>
    $<TARGET_OBJECTS:pfr_upstream_battle_anim_effects_3>
    $<TARGET_OBJECTS:pfr_upstream_battle_anim_flying>
    $<TARGET_OBJECTS:pfr_upstream_battle_anim_water>
    $<TARGET_OBJECTS:pfr_upstream_battle_bg>
    $<TARGET_OBJECTS:pfr_upstream_battle_interface>
    $<TARGET_OBJECTS:pfr_upstream_battle_records>
    $<TARGET_OBJECTS:pfr_upstream_battle_script_commands>
    $<TARGET_OBJECTS:pfr_upstream_battle_transition>
    $<TARGET_OBJECTS:pfr_upstream_berry_crush>
    $<TARGET_OBJECTS:pfr_upstream_braille_text>
    $<TARGET_OBJECTS:pfr_upstream_credits>
    $<TARGET_OBJECTS:pfr_upstream_daycare>
    $<TARGET_OBJECTS:pfr_upstream_digit_obj_util>
    $<TARGET_OBJECTS:pfr_upstream_diploma>
    $<TARGET_OBJECTS:pfr_upstream_dodrio_berry_picking>
    $<TARGET_OBJECTS:pfr_upstream_easy_chat_3>
    $<TARGET_OBJECTS:pfr_upstream_evolution_graphics>
    $<TARGET_OBJECTS:pfr_upstream_evolution_scene>
    $<TARGET_OBJECTS:pfr_upstream_fame_checker>
    $<TARGET_OBJECTS:pfr_upstream_field_door>
    $<TARGET_OBJECTS:pfr_upstream_field_effect>
    $<TARGET_OBJECTS:pfr_upstream_field_specials>
    $<TARGET_OBJECTS:pfr_upstream_field_weather>
    $<TARGET_OBJECTS:pfr_upstream_fldeff_flash>
    $<TARGET_OBJECTS:pfr_upstream_hall_of_fame>
    $<TARGET_OBJECTS:pfr_upstream_help_message>
    $<TARGET_OBJECTS:pfr_upstream_help_system>
    $<TARGET_OBJECTS:pfr_upstream_help_system_util>
    $<TARGET_OBJECTS:pfr_upstream_item_menu>
    $<TARGET_OBJECTS:pfr_upstream_itemfinder>
    $<TARGET_OBJECTS:pfr_upstream_learn_move>
    $<TARGET_OBJECTS:pfr_upstream_link>
    $<TARGET_OBJECTS:pfr_upstream_link_rfu_3>
    $<TARGET_OBJECTS:pfr_upstream_map_preview_screen>
    $<TARGET_OBJECTS:pfr_upstream_menu_indicators>
    $<TARGET_OBJECTS:pfr_upstream_minigame_countdown>
    $<TARGET_OBJECTS:pfr_upstream_mon_markings>
    $<TARGET_OBJECTS:pfr_upstream_mystery_gift_menu>
    $<TARGET_OBJECTS:pfr_upstream_mystery_gift_show_card>
    $<TARGET_OBJECTS:pfr_upstream_mystery_gift_show_news>
    $<TARGET_OBJECTS:pfr_upstream_naming_screen>
    $<TARGET_OBJECTS:pfr_upstream_option_menu>
    $<TARGET_OBJECTS:pfr_upstream_pokedex_area_markers>
    $<TARGET_OBJECTS:pfr_upstream_pokedex_screen>
    $<TARGET_OBJECTS:pfr_upstream_pokemon>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_icon>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_jump>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_special_anim_scene>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_data>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_graphics>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_menu>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_misc>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_tasks>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_summary_screen>
    $<TARGET_OBJECTS:pfr_upstream_quest_log>
    $<TARGET_OBJECTS:pfr_upstream_region_map>
    $<TARGET_OBJECTS:pfr_upstream_save_failed_screen>
    $<TARGET_OBJECTS:pfr_upstream_script_menu>
    $<TARGET_OBJECTS:pfr_upstream_seagallop>
    $<TARGET_OBJECTS:pfr_upstream_slot_machine>
    $<TARGET_OBJECTS:pfr_upstream_ss_anne>
    $<TARGET_OBJECTS:pfr_upstream_tileset_anims>
    $<TARGET_OBJECTS:pfr_upstream_trade>
    $<TARGET_OBJECTS:pfr_upstream_trade_scene>
    $<TARGET_OBJECTS:pfr_upstream_trainer_card>
    $<TARGET_OBJECTS:pfr_upstream_trainer_see>
    $<TARGET_OBJECTS:pfr_upstream_union_room_chat_display>
    $<TARGET_OBJECTS:pfr_upstream_union_room_chat_objects>
    $<TARGET_OBJECTS:pfr_upstream_util>
    $<TARGET_OBJECTS:pfr_upstream_wireless_communication_status_screen>
    $<TARGET_OBJECTS:pfr_upstream_tilesets>
    $<TARGET_OBJECTS:pfr_upstream_event_object_movement>
    $<TARGET_OBJECTS:pfr_upstream_party_menu>
)
target_compile_definitions(pfr_smoke PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_smoke PRIVATE
    -fno-pie
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${PFR_GEN_HEADERS_DIR}"
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
    $<TARGET_OBJECTS:pfr_upstream_oak_speech>
    $<TARGET_OBJECTS:pfr_upstream_new_game>
    $<TARGET_OBJECTS:pfr_upstream_play_time>
    $<TARGET_OBJECTS:pfr_upstream_text_window_graphics>
    $<TARGET_OBJECTS:pfr_upstream_text>
    $<TARGET_OBJECTS:pfr_upstream_text_window>
    $<TARGET_OBJECTS:pfr_upstream_new_menu_helpers>
    $<TARGET_OBJECTS:pfr_upstream_text_printer>
    $<TARGET_OBJECTS:pfr_upstream_runtime>
    $<TARGET_OBJECTS:pfr_upstream_graphics>
    $<TARGET_OBJECTS:pfr_upstream_bag>
    $<TARGET_OBJECTS:pfr_upstream_battle_anim_effects_3>
    $<TARGET_OBJECTS:pfr_upstream_battle_anim_flying>
    $<TARGET_OBJECTS:pfr_upstream_battle_anim_water>
    $<TARGET_OBJECTS:pfr_upstream_battle_bg>
    $<TARGET_OBJECTS:pfr_upstream_battle_interface>
    $<TARGET_OBJECTS:pfr_upstream_battle_records>
    $<TARGET_OBJECTS:pfr_upstream_battle_script_commands>
    $<TARGET_OBJECTS:pfr_upstream_battle_transition>
    $<TARGET_OBJECTS:pfr_upstream_berry_crush>
    $<TARGET_OBJECTS:pfr_upstream_braille_text>
    $<TARGET_OBJECTS:pfr_upstream_credits>
    $<TARGET_OBJECTS:pfr_upstream_daycare>
    $<TARGET_OBJECTS:pfr_upstream_digit_obj_util>
    $<TARGET_OBJECTS:pfr_upstream_diploma>
    $<TARGET_OBJECTS:pfr_upstream_dodrio_berry_picking>
    $<TARGET_OBJECTS:pfr_upstream_easy_chat_3>
    $<TARGET_OBJECTS:pfr_upstream_evolution_graphics>
    $<TARGET_OBJECTS:pfr_upstream_evolution_scene>
    $<TARGET_OBJECTS:pfr_upstream_fame_checker>
    $<TARGET_OBJECTS:pfr_upstream_field_door>
    $<TARGET_OBJECTS:pfr_upstream_field_effect>
    $<TARGET_OBJECTS:pfr_upstream_field_specials>
    $<TARGET_OBJECTS:pfr_upstream_field_weather>
    $<TARGET_OBJECTS:pfr_upstream_fldeff_flash>
    $<TARGET_OBJECTS:pfr_upstream_hall_of_fame>
    $<TARGET_OBJECTS:pfr_upstream_help_message>
    $<TARGET_OBJECTS:pfr_upstream_help_system>
    $<TARGET_OBJECTS:pfr_upstream_help_system_util>
    $<TARGET_OBJECTS:pfr_upstream_item_menu>
    $<TARGET_OBJECTS:pfr_upstream_itemfinder>
    $<TARGET_OBJECTS:pfr_upstream_learn_move>
    $<TARGET_OBJECTS:pfr_upstream_link>
    $<TARGET_OBJECTS:pfr_upstream_link_rfu_3>
    $<TARGET_OBJECTS:pfr_upstream_map_preview_screen>
    $<TARGET_OBJECTS:pfr_upstream_menu_indicators>
    $<TARGET_OBJECTS:pfr_upstream_minigame_countdown>
    $<TARGET_OBJECTS:pfr_upstream_mon_markings>
    $<TARGET_OBJECTS:pfr_upstream_mystery_gift_menu>
    $<TARGET_OBJECTS:pfr_upstream_mystery_gift_show_card>
    $<TARGET_OBJECTS:pfr_upstream_mystery_gift_show_news>
    $<TARGET_OBJECTS:pfr_upstream_naming_screen>
    $<TARGET_OBJECTS:pfr_upstream_option_menu>
    $<TARGET_OBJECTS:pfr_upstream_pokedex_area_markers>
    $<TARGET_OBJECTS:pfr_upstream_pokedex_screen>
    $<TARGET_OBJECTS:pfr_upstream_pokemon>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_icon>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_jump>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_special_anim_scene>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_data>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_graphics>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_menu>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_misc>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_tasks>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_summary_screen>
    $<TARGET_OBJECTS:pfr_upstream_quest_log>
    $<TARGET_OBJECTS:pfr_upstream_region_map>
    $<TARGET_OBJECTS:pfr_upstream_save_failed_screen>
    $<TARGET_OBJECTS:pfr_upstream_script_menu>
    $<TARGET_OBJECTS:pfr_upstream_seagallop>
    $<TARGET_OBJECTS:pfr_upstream_slot_machine>
    $<TARGET_OBJECTS:pfr_upstream_ss_anne>
    $<TARGET_OBJECTS:pfr_upstream_tileset_anims>
    $<TARGET_OBJECTS:pfr_upstream_trade>
    $<TARGET_OBJECTS:pfr_upstream_trade_scene>
    $<TARGET_OBJECTS:pfr_upstream_trainer_card>
    $<TARGET_OBJECTS:pfr_upstream_trainer_see>
    $<TARGET_OBJECTS:pfr_upstream_union_room_chat_display>
    $<TARGET_OBJECTS:pfr_upstream_union_room_chat_objects>
    $<TARGET_OBJECTS:pfr_upstream_util>
    $<TARGET_OBJECTS:pfr_upstream_wireless_communication_status_screen>
    $<TARGET_OBJECTS:pfr_upstream_tilesets>
    $<TARGET_OBJECTS:pfr_upstream_event_object_movement>
    $<TARGET_OBJECTS:pfr_upstream_party_menu>
)
target_compile_definitions(pfr_render_test PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_render_test PRIVATE
    -fno-pie
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${PFR_GEN_HEADERS_DIR}"
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
add_executable(pfr_play src/pfr_play.c src/host_audio.c)
set_target_properties(pfr_play PROPERTIES ENABLE_EXPORTS TRUE)
target_sources(pfr_play PRIVATE
    src/host_agbmain.c
    $<TARGET_OBJECTS:pfr_upstream_intro>
    $<TARGET_OBJECTS:pfr_upstream_title>
    $<TARGET_OBJECTS:pfr_upstream_clear_save>
    $<TARGET_OBJECTS:pfr_upstream_berry_fix>
    $<TARGET_OBJECTS:pfr_upstream_main_menu>
    $<TARGET_OBJECTS:pfr_upstream_oak_speech>
    $<TARGET_OBJECTS:pfr_upstream_new_game>
    $<TARGET_OBJECTS:pfr_upstream_play_time>
    $<TARGET_OBJECTS:pfr_upstream_text_window_graphics>
    $<TARGET_OBJECTS:pfr_upstream_text>
    $<TARGET_OBJECTS:pfr_upstream_text_window>
    $<TARGET_OBJECTS:pfr_upstream_new_menu_helpers>
    $<TARGET_OBJECTS:pfr_upstream_text_printer>
    $<TARGET_OBJECTS:pfr_upstream_runtime>
    $<TARGET_OBJECTS:pfr_upstream_graphics>
    $<TARGET_OBJECTS:pfr_upstream_bag>
    $<TARGET_OBJECTS:pfr_upstream_battle_anim_effects_3>
    $<TARGET_OBJECTS:pfr_upstream_battle_anim_flying>
    $<TARGET_OBJECTS:pfr_upstream_battle_anim_water>
    $<TARGET_OBJECTS:pfr_upstream_battle_bg>
    $<TARGET_OBJECTS:pfr_upstream_battle_interface>
    $<TARGET_OBJECTS:pfr_upstream_battle_records>
    $<TARGET_OBJECTS:pfr_upstream_battle_script_commands>
    $<TARGET_OBJECTS:pfr_upstream_battle_transition>
    $<TARGET_OBJECTS:pfr_upstream_berry_crush>
    $<TARGET_OBJECTS:pfr_upstream_braille_text>
    $<TARGET_OBJECTS:pfr_upstream_credits>
    $<TARGET_OBJECTS:pfr_upstream_daycare>
    $<TARGET_OBJECTS:pfr_upstream_digit_obj_util>
    $<TARGET_OBJECTS:pfr_upstream_diploma>
    $<TARGET_OBJECTS:pfr_upstream_dodrio_berry_picking>
    $<TARGET_OBJECTS:pfr_upstream_easy_chat_3>
    $<TARGET_OBJECTS:pfr_upstream_evolution_graphics>
    $<TARGET_OBJECTS:pfr_upstream_evolution_scene>
    $<TARGET_OBJECTS:pfr_upstream_fame_checker>
    $<TARGET_OBJECTS:pfr_upstream_field_door>
    $<TARGET_OBJECTS:pfr_upstream_field_effect>
    $<TARGET_OBJECTS:pfr_upstream_field_specials>
    $<TARGET_OBJECTS:pfr_upstream_field_weather>
    $<TARGET_OBJECTS:pfr_upstream_fldeff_flash>
    $<TARGET_OBJECTS:pfr_upstream_hall_of_fame>
    $<TARGET_OBJECTS:pfr_upstream_help_message>
    $<TARGET_OBJECTS:pfr_upstream_help_system>
    $<TARGET_OBJECTS:pfr_upstream_help_system_util>
    $<TARGET_OBJECTS:pfr_upstream_item_menu>
    $<TARGET_OBJECTS:pfr_upstream_itemfinder>
    $<TARGET_OBJECTS:pfr_upstream_learn_move>
    $<TARGET_OBJECTS:pfr_upstream_link>
    $<TARGET_OBJECTS:pfr_upstream_link_rfu_3>
    $<TARGET_OBJECTS:pfr_upstream_map_preview_screen>
    $<TARGET_OBJECTS:pfr_upstream_menu_indicators>
    $<TARGET_OBJECTS:pfr_upstream_minigame_countdown>
    $<TARGET_OBJECTS:pfr_upstream_mon_markings>
    $<TARGET_OBJECTS:pfr_upstream_mystery_gift_menu>
    $<TARGET_OBJECTS:pfr_upstream_mystery_gift_show_card>
    $<TARGET_OBJECTS:pfr_upstream_mystery_gift_show_news>
    $<TARGET_OBJECTS:pfr_upstream_naming_screen>
    $<TARGET_OBJECTS:pfr_upstream_option_menu>
    $<TARGET_OBJECTS:pfr_upstream_pokedex_area_markers>
    $<TARGET_OBJECTS:pfr_upstream_pokedex_screen>
    $<TARGET_OBJECTS:pfr_upstream_pokemon>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_icon>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_jump>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_special_anim_scene>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_data>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_graphics>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_menu>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_misc>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_storage_system_tasks>
    $<TARGET_OBJECTS:pfr_upstream_pokemon_summary_screen>
    $<TARGET_OBJECTS:pfr_upstream_quest_log>
    $<TARGET_OBJECTS:pfr_upstream_region_map>
    $<TARGET_OBJECTS:pfr_upstream_save_failed_screen>
    $<TARGET_OBJECTS:pfr_upstream_script_menu>
    $<TARGET_OBJECTS:pfr_upstream_seagallop>
    $<TARGET_OBJECTS:pfr_upstream_slot_machine>
    $<TARGET_OBJECTS:pfr_upstream_ss_anne>
    $<TARGET_OBJECTS:pfr_upstream_tileset_anims>
    $<TARGET_OBJECTS:pfr_upstream_trade>
    $<TARGET_OBJECTS:pfr_upstream_trade_scene>
    $<TARGET_OBJECTS:pfr_upstream_trainer_card>
    $<TARGET_OBJECTS:pfr_upstream_trainer_see>
    $<TARGET_OBJECTS:pfr_upstream_union_room_chat_display>
    $<TARGET_OBJECTS:pfr_upstream_union_room_chat_objects>
    $<TARGET_OBJECTS:pfr_upstream_util>
    $<TARGET_OBJECTS:pfr_upstream_wireless_communication_status_screen>
    $<TARGET_OBJECTS:pfr_upstream_tilesets>
    $<TARGET_OBJECTS:pfr_upstream_event_object_movement>
    $<TARGET_OBJECTS:pfr_upstream_party_menu>
)
target_compile_definitions(pfr_play PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_play PRIVATE
    -fno-pie
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${PFR_GEN_HEADERS_DIR}"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    ${SDL2_CFLAGS}
)
target_link_options(pfr_play PRIVATE
    -no-pie
    -Wl,--gc-sections
)
target_link_libraries(pfr_play PRIVATE pfr_upstream_core Threads::Threads m dl ${SDL2_LIBRARIES})

# Frame diff tool: pixel-level PPM comparison for golden frame verification
add_executable(pfr_frame_diff tools/pfr_frame_diff.c)
target_compile_options(pfr_frame_diff PRIVATE -O2)
target_link_libraries(pfr_frame_diff PRIVATE m)

# Reference ROM + mGBA oracle tooling
set(PFR_REFERENCE_ROM "${POKEFIRERED_SOURCE_DIR}/pokefirered.gba")
add_custom_target(pfr_reference_rom
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_reference_rom.sh"
        "${AGBCC_SOURCE_DIR}"
        "${POKEFIRERED_SOURCE_DIR}"
    DEPENDS
        pfr_libpng_external
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_reference_rom.sh"
        "${AGBCC_SOURCE_DIR}/build.sh"
        "${AGBCC_SOURCE_DIR}/install.sh"
        "${POKEFIRERED_SOURCE_DIR}/INSTALL.md"
        "${POKEFIRERED_SOURCE_DIR}/README.md"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building exact pokefirered.gba from pinned upstream sources"
    VERBATIM
)

set(MGBA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mgba" CACHE PATH "Path to mGBA source tree")
set(MGBA_BUILD_DIR "${CMAKE_BINARY_DIR}/third_party_build/mgba")

ExternalProject_Add(pfr_mgba_external
    SOURCE_DIR "${MGBA_SOURCE_DIR}"
    BINARY_DIR "${MGBA_BUILD_DIR}"
    UPDATE_COMMAND ""
    INSTALL_COMMAND ""
    CMAKE_ARGS
        -DLIBMGBA_ONLY=ON
        -DM_CORE_GBA=ON
        -DM_CORE_GB=OFF
        -DBUILD_SHARED=OFF
        -DBUILD_TEST=OFF
    BUILD_BYPRODUCTS
        "${MGBA_BUILD_DIR}/libmgba.a"
        "${MGBA_BUILD_DIR}/include/mgba/internal/config.h"
)

add_library(mgba_static STATIC IMPORTED GLOBAL)
set_target_properties(mgba_static PROPERTIES
    IMPORTED_LOCATION "${MGBA_BUILD_DIR}/libmgba.a"
)
add_dependencies(mgba_static pfr_mgba_external)

add_executable(mgba_capture
    scripts/mgba_capture.c
    src/host_capture_manifest.c
)
target_compile_definitions(mgba_capture PRIVATE
    BUILD_STATIC
    ENABLE_DIRECTORIES
    ENABLE_VFS
    ENABLE_VFS_FD
    HAVE_FREELOCALE
    HAVE_FUTIMENS
    HAVE_FUTIMES
    HAVE_LOCALE
    HAVE_LOCALTIME_R
    HAVE_NEWLOCALE
    HAVE_PTHREAD_CREATE
    HAVE_PTHREAD_SETNAME_NP
    HAVE_REALPATH
    HAVE_SETLOCALE
    HAVE_STRDUP
    HAVE_STRLCPY
    HAVE_STRNDUP
    HAVE_USELOCALE
    HAVE_VASPRINTF
    M_CORE_GBA
    USE_PTHREADS
    _GNU_SOURCE
)
target_include_directories(mgba_capture PRIVATE
    "${MGBA_BUILD_DIR}/include"
    "${MGBA_SOURCE_DIR}/src"
    "${MGBA_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(mgba_capture PRIVATE mgba_static Threads::Threads m)

add_custom_target(pfr_refresh_goldens
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_golden_frames.sh"
        "${PFR_REFERENCE_ROM}"
    DEPENDS
        pfr_reference_rom
        mgba_capture
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture_golden_frames.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden_frames/manifest.txt"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Refreshing golden frames from pinned mGBA and reference ROM"
    VERBATIM
)

# Asset compiler: converts upstream PNG/PAL/BIN to GBA intermediate formats
add_executable(pfr_assets tools/pfr_assets.c)
target_compile_options(pfr_assets PRIVATE -O2)
target_link_libraries(pfr_assets PRIVATE m)

set(PFR_LIBPNG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mgba/src/third-party/libpng")
set(PFR_LIBPNG_BUILD_DIR "${CMAKE_BINARY_DIR}/third_party_build/libpng")
set(PFR_LIBPNG_INSTALL_DIR "${CMAKE_BINARY_DIR}/third_party_install/libpng")

ExternalProject_Add(pfr_libpng_external
    SOURCE_DIR "${PFR_LIBPNG_SOURCE_DIR}"
    BINARY_DIR "${PFR_LIBPNG_BUILD_DIR}"
    UPDATE_COMMAND ""
    INSTALL_DIR "${PFR_LIBPNG_INSTALL_DIR}"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DPNG_SHARED=OFF
        -DPNG_STATIC=ON
        -DPNG_TESTS=OFF
        -DPNG_TOOLS=OFF
        -DPNG_EXECUTABLES=OFF
    BUILD_BYPRODUCTS
        "${PFR_LIBPNG_INSTALL_DIR}/lib/libpng.a"
        "${PFR_LIBPNG_INSTALL_DIR}/lib/pkgconfig/libpng.pc"
)

add_custom_command(
    OUTPUT "${POKEFIRERED_SOURCE_DIR}/tools/gbagfx/gbagfx"
    COMMAND ${CMAKE_COMMAND} -E env
        "PKG_CONFIG_PATH=${PFR_LIBPNG_INSTALL_DIR}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}"
        make -C "${POKEFIRERED_SOURCE_DIR}/tools/gbagfx" "LIBS=-lpng -lz -lm"
    DEPENDS
        pfr_libpng_external
        "${POKEFIRERED_SOURCE_DIR}/tools/gbagfx/Makefile"
        "${POKEFIRERED_SOURCE_DIR}/tools/gbagfx/main.c"
        "${POKEFIRERED_SOURCE_DIR}/tools/gbagfx/convert_png.c"
        "${POKEFIRERED_SOURCE_DIR}/tools/gbagfx/font.c"
    COMMENT "Building upstream gbagfx with local libpng"
    VERBATIM
)

add_custom_target(pfr_gbagfx_tool
    DEPENDS "${POKEFIRERED_SOURCE_DIR}/tools/gbagfx/gbagfx"
)

add_custom_command(
    OUTPUT "${PFR_BOOT_ASSET_MANIFEST}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_ASSET_MANIFEST_DIR}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_incbin.py"
        emit-manifest
        ${PFR_BOOT_ASSET_TUS}
        --upstream-root "${POKEFIRERED_SOURCE_DIR}"
        --output "${PFR_BOOT_ASSET_MANIFEST}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_incbin.py"
        ${PFR_BOOT_ASSET_TUS}
    COMMENT "Generating deterministic boot-path asset manifest"
    VERBATIM
)

add_custom_target(pfr_verify_asset_manifests
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_incbin.py"
        verify-manifest
        "${PFR_CHECKED_IN_BOOT_ASSET_MANIFEST}"
        --upstream-root "${POKEFIRERED_SOURCE_DIR}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_incbin.py"
        "${PFR_CHECKED_IN_BOOT_ASSET_MANIFEST}"
        ${PFR_BOOT_ASSET_TUS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Verifying checked-in asset manifests against current INCBIN references"
    VERBATIM
)

# Asset pipeline: compile assets, generate .inc files, preprocess upstream sources
add_custom_command(
    OUTPUT "${PFR_ASSET_PIPELINE_STAMP}"
    BYPRODUCTS ${PFR_PREPROCESSED_SOURCES} ${PFR_CHARMAP_HEADER_OUTPUTS} ${PFR_DATA_HEADER_OUTPUTS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_ASSETS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_INC_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_GEN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_GEN_HEADERS_DIR}"
    COMMAND $<TARGET_FILE:pfr_assets> batch
        "${POKEFIRERED_SOURCE_DIR}" "${PFR_ASSETS_DIR}" "${PFR_BOOT_ASSET_MANIFEST}"
    COMMAND $<TARGET_FILE:pfr_assets> geninc "${PFR_ASSETS_DIR}" "${PFR_INC_DIR}"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/intro.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/intro.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/title_screen.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/title_screen.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/main_menu.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/main_menu.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/oak_speech.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/oak_speech.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/text_window_graphics.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/text_window_graphics.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/text.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/text.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/new_menu_helpers.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/new_menu_helpers.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/graphics.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/graphics.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/bag.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/bag.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_effects_3.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_anim_effects_3.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_flying.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_anim_flying.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_water.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_anim_water.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_bg.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_bg.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_interface.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_interface.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_records.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_records.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_script_commands.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_script_commands.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_transition.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_transition.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/berry_crush.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/berry_crush.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/braille_text.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/braille_text.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/credits.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/credits.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/daycare.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/daycare.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/digit_obj_util.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/digit_obj_util.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/diploma.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/diploma.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/dodrio_berry_picking.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/dodrio_berry_picking.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/easy_chat_3.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/easy_chat_3.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/evolution_graphics.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/evolution_graphics.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/evolution_scene.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/evolution_scene.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/fame_checker.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/fame_checker.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/field_door.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/field_door.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/field_effect.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/field_effect.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/field_specials.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/field_specials.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/field_weather.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/field_weather.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/fldeff_flash.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/fldeff_flash.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/hall_of_fame.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/hall_of_fame.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/help_message.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/help_message.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/help_system.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/help_system.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/help_system_util.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/help_system_util.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/item_menu.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/item_menu.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/itemfinder.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/itemfinder.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/learn_move.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/learn_move.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/link.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/link.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/link_rfu_3.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/link_rfu_3.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/map_preview_screen.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/map_preview_screen.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/menu_indicators.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/menu_indicators.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/minigame_countdown.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/minigame_countdown.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/mon_markings.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/mon_markings.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_menu.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/mystery_gift_menu.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_show_card.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/mystery_gift_show_card.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_show_news.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/mystery_gift_show_news.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/naming_screen.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/naming_screen.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/option_menu.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/option_menu.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokedex_area_markers.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokedex_area_markers.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokedex_screen.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokedex_screen.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_icon.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon_icon.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_jump.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon_jump.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_special_anim_scene.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon_special_anim_scene.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_data.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon_storage_system_data.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_graphics.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon_storage_system_graphics.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_menu.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon_storage_system_menu.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_misc.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon_storage_system_misc.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_tasks.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon_storage_system_tasks.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_summary_screen.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/pokemon_summary_screen.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/quest_log.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/quest_log.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/region_map.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/region_map.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/save_failed_screen.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/save_failed_screen.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/script_menu.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/script_menu.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/seagallop.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/seagallop.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/slot_machine.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/slot_machine.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/ss_anne.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/ss_anne.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/tileset_anims.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/tileset_anims.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/trade.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/trade.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/trade_scene.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/trade_scene.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/trainer_card.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/trainer_card.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/trainer_see.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/trainer_see.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat_display.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/union_room_chat_display.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat_objects.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/union_room_chat_objects.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/util.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/util.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/wireless_communication_status_screen.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/wireless_communication_status_screen.c"
    # Charmap-only files: copy to gen dir (pfr_assets preproc just copies files without INCBINs)
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/strings.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/strings.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_message.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_message.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/trainer_tower_sets.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/trainer_tower_sets.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/move_descriptions.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/move_descriptions.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/union_room_message.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/union_room_message.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/berry.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/berry.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/keyboard_text.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/keyboard_text.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_main.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_main.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_event_msg.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/mystery_event_msg.c"
    # Additional charmap-only files (contain _() but no INCBINs)
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_status_effects.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_anim_status_effects.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/battle_tower.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/battle_tower.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/berry_pouch.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/berry_pouch.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/field_player_avatar.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/field_player_avatar.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/link_rfu_2.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/link_rfu_2.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/mailbox_pc.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/mailbox_pc.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_scripts.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/mystery_gift_scripts.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/scrcmd.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/scrcmd.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/string_util.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/string_util.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/tm_case.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/tm_case.c"
    # Preprocess source files whose data headers contain INCBIN macros
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/tilesets.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/tilesets.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/event_object_movement.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/event_object_movement.c"
    COMMAND $<TARGET_FILE:pfr_assets> preproc
        "${POKEFIRERED_SOURCE_DIR}/src/party_menu.c" "${PFR_INC_DIR}" "${PFR_GEN_DIR}/party_menu.c"
    # Preprocess data headers that contain INCBIN macros (pokemon sprites, tilesets, etc.)
    ${PFR_DATA_HEADER_PREPROC_COMMANDS}
    ${PFR_CHARMAP_HEADER_COMMANDS}
    # Charmap: convert _("text") to Pokemon character encoding in ALL generated files
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/charmap_preproc.sh"
        "${POKEFIRERED_SOURCE_DIR}/tools/preproc/preproc"
        "${POKEFIRERED_SOURCE_DIR}/charmap.txt"
        ${PFR_PREPROCESSED_SOURCES}
        ${PFR_CHARMAP_HEADER_OUTPUTS}
    COMMAND ${CMAKE_COMMAND} -E touch "${PFR_ASSET_PIPELINE_STAMP}"
    DEPENDS
        pfr_assets
        pfr_gbagfx_tool
        pfr_verify_asset_manifests
        "${PFR_BOOT_ASSET_MANIFEST}"
        "${POKEFIRERED_SOURCE_DIR}/src/intro.c"
        "${POKEFIRERED_SOURCE_DIR}/src/title_screen.c"
        "${POKEFIRERED_SOURCE_DIR}/src/main_menu.c"
        "${POKEFIRERED_SOURCE_DIR}/src/oak_speech.c"
        "${POKEFIRERED_SOURCE_DIR}/src/text_window_graphics.c"
        "${POKEFIRERED_SOURCE_DIR}/src/text.c"
        "${POKEFIRERED_SOURCE_DIR}/src/new_menu_helpers.c"
        "${POKEFIRERED_SOURCE_DIR}/src/graphics.c"
        "${POKEFIRERED_SOURCE_DIR}/src/bag.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_effects_3.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_flying.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_water.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_bg.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_interface.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_records.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_script_commands.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_transition.c"
        "${POKEFIRERED_SOURCE_DIR}/src/berry_crush.c"
        "${POKEFIRERED_SOURCE_DIR}/src/braille_text.c"
        "${POKEFIRERED_SOURCE_DIR}/src/credits.c"
        "${POKEFIRERED_SOURCE_DIR}/src/daycare.c"
        "${POKEFIRERED_SOURCE_DIR}/src/digit_obj_util.c"
        "${POKEFIRERED_SOURCE_DIR}/src/diploma.c"
        "${POKEFIRERED_SOURCE_DIR}/src/dodrio_berry_picking.c"
        "${POKEFIRERED_SOURCE_DIR}/src/easy_chat_3.c"
        "${POKEFIRERED_SOURCE_DIR}/src/evolution_graphics.c"
        "${POKEFIRERED_SOURCE_DIR}/src/evolution_scene.c"
        "${POKEFIRERED_SOURCE_DIR}/src/fame_checker.c"
        "${POKEFIRERED_SOURCE_DIR}/src/field_door.c"
        "${POKEFIRERED_SOURCE_DIR}/src/field_effect.c"
        "${POKEFIRERED_SOURCE_DIR}/src/field_specials.c"
        "${POKEFIRERED_SOURCE_DIR}/src/field_weather.c"
        "${POKEFIRERED_SOURCE_DIR}/src/fldeff_flash.c"
        "${POKEFIRERED_SOURCE_DIR}/src/hall_of_fame.c"
        "${POKEFIRERED_SOURCE_DIR}/src/help_message.c"
        "${POKEFIRERED_SOURCE_DIR}/src/help_system.c"
        "${POKEFIRERED_SOURCE_DIR}/src/help_system_util.c"
        "${POKEFIRERED_SOURCE_DIR}/src/item_menu.c"
        "${POKEFIRERED_SOURCE_DIR}/src/itemfinder.c"
        "${POKEFIRERED_SOURCE_DIR}/src/learn_move.c"
        "${POKEFIRERED_SOURCE_DIR}/src/link.c"
        "${POKEFIRERED_SOURCE_DIR}/src/link_rfu_3.c"
        "${POKEFIRERED_SOURCE_DIR}/src/map_preview_screen.c"
        "${POKEFIRERED_SOURCE_DIR}/src/menu_indicators.c"
        "${POKEFIRERED_SOURCE_DIR}/src/minigame_countdown.c"
        "${POKEFIRERED_SOURCE_DIR}/src/mon_markings.c"
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_menu.c"
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_show_card.c"
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_show_news.c"
        "${POKEFIRERED_SOURCE_DIR}/src/naming_screen.c"
        "${POKEFIRERED_SOURCE_DIR}/src/option_menu.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokedex_area_markers.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokedex_screen.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_icon.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_jump.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_special_anim_scene.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_data.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_graphics.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_menu.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_misc.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_tasks.c"
        "${POKEFIRERED_SOURCE_DIR}/src/pokemon_summary_screen.c"
        "${POKEFIRERED_SOURCE_DIR}/src/quest_log.c"
        "${POKEFIRERED_SOURCE_DIR}/src/region_map.c"
        "${POKEFIRERED_SOURCE_DIR}/src/save_failed_screen.c"
        "${POKEFIRERED_SOURCE_DIR}/src/script_menu.c"
        "${POKEFIRERED_SOURCE_DIR}/src/seagallop.c"
        "${POKEFIRERED_SOURCE_DIR}/src/slot_machine.c"
        "${POKEFIRERED_SOURCE_DIR}/src/ss_anne.c"
        "${POKEFIRERED_SOURCE_DIR}/src/tileset_anims.c"
        "${POKEFIRERED_SOURCE_DIR}/src/trade.c"
        "${POKEFIRERED_SOURCE_DIR}/src/trade_scene.c"
        "${POKEFIRERED_SOURCE_DIR}/src/trainer_card.c"
        "${POKEFIRERED_SOURCE_DIR}/src/trainer_see.c"
        "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat_display.c"
        "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat_objects.c"
        "${POKEFIRERED_SOURCE_DIR}/src/util.c"
        "${POKEFIRERED_SOURCE_DIR}/src/wireless_communication_status_screen.c"
        # Charmap-only files
        "${POKEFIRERED_SOURCE_DIR}/src/strings.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_message.c"
        "${POKEFIRERED_SOURCE_DIR}/src/trainer_tower_sets.c"
        "${POKEFIRERED_SOURCE_DIR}/src/move_descriptions.c"
        "${POKEFIRERED_SOURCE_DIR}/src/union_room_message.c"
        "${POKEFIRERED_SOURCE_DIR}/src/berry.c"
        "${POKEFIRERED_SOURCE_DIR}/src/keyboard_text.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_main.c"
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_event_msg.c"
        # Additional charmap-only files
        "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_status_effects.c"
        "${POKEFIRERED_SOURCE_DIR}/src/battle_tower.c"
        "${POKEFIRERED_SOURCE_DIR}/src/berry_pouch.c"
        "${POKEFIRERED_SOURCE_DIR}/src/field_player_avatar.c"
        "${POKEFIRERED_SOURCE_DIR}/src/link_rfu_2.c"
        "${POKEFIRERED_SOURCE_DIR}/src/mailbox_pc.c"
        "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_scripts.c"
        "${POKEFIRERED_SOURCE_DIR}/src/scrcmd.c"
        "${POKEFIRERED_SOURCE_DIR}/src/string_util.c"
        "${POKEFIRERED_SOURCE_DIR}/src/tm_case.c"
        ${PFR_CHARMAP_HEADER_INPUTS}
        "${POKEFIRERED_SOURCE_DIR}/charmap.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/charmap_preproc.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/preproc_charmap.py"
    COMMENT "Asset pipeline: PNG/PAL → GBA → .inc → preprocessed sources → charmap"
)

add_custom_target(pfr_asset_pipeline
    DEPENDS "${PFR_ASSET_PIPELINE_STAMP}"
)

# Golden frame check: runs pfr_render_test then diffs each milestone against golden references
add_custom_target(pfr_golden_check
    COMMAND ${CMAKE_COMMAND} -E echo "Running golden frame check..."
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/golden_check.sh"
        "$<TARGET_FILE:pfr_render_test>"
        "$<TARGET_FILE:pfr_frame_diff>"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden_frames"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden_frames/manifest.txt"
    DEPENDS pfr_render_test pfr_frame_diff
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Golden check: render_test + frame_diff against golden references"
    VERBATIM
)
