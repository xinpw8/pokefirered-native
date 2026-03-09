cmake_minimum_required(VERSION 3.20)

project(pokefirered_native_bootstrap C)

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
)

set(PFR_BOOT_ASSET_TUS
    "${POKEFIRERED_SOURCE_DIR}/src/intro.c"
    "${POKEFIRERED_SOURCE_DIR}/src/title_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/main_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/oak_speech.c"
    "${POKEFIRERED_SOURCE_DIR}/src/text_window_graphics.c"
    "${POKEFIRERED_SOURCE_DIR}/src/text.c"
    "${POKEFIRERED_SOURCE_DIR}/src/new_menu_helpers.c"
)

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
    src/host_flash.c
    src/host_timer.c
    src/host_intro_stubs.c
    src/host_new_game_stubs.c
    src/host_oak_speech_stubs.c
    src/host_title_screen_stubs.c
    src/upstream_stubs.c
    # ---- ALL upstream C source files (282 files) ----
    "${POKEFIRERED_SOURCE_DIR}/src/AgbRfu_LinkManager.c"
    # agb_flash*.c replaced by src/host_flash.c
    "${POKEFIRERED_SOURCE_DIR}/src/bag.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_ai_script_commands.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_ai_switch_items.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_bug.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_dark.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_dragon.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_effects_1.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_effects_2.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_effects_3.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_electric.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_fight.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_fire.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_flying.c"
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
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_status_effects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_utility_funcs.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_anim_water.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_bg.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_link_opponent.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_link_partner.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_oak_old_man.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_opponent.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_player.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_pokedude.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controller_safari.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_controllers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_gfx_sfx_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_interface.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_intro.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_main.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_message.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_records.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_script_commands.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_setup.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_tower.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_transition.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/battle_util2.c"
    "${POKEFIRERED_SOURCE_DIR}/src/berry.c"
    "${POKEFIRERED_SOURCE_DIR}/src/berry_crush.c"
    "${POKEFIRERED_SOURCE_DIR}/src/berry_fix_program.c"
    "${POKEFIRERED_SOURCE_DIR}/src/berry_pouch.c"
    "${POKEFIRERED_SOURCE_DIR}/src/berry_powder.c"
    "${POKEFIRERED_SOURCE_DIR}/src/bg.c"
    "${POKEFIRERED_SOURCE_DIR}/src/bg_regs.c"
    "${POKEFIRERED_SOURCE_DIR}/src/bike.c"
    "${POKEFIRERED_SOURCE_DIR}/src/blend_palette.c"
    "${POKEFIRERED_SOURCE_DIR}/src/blit.c"
    "${POKEFIRERED_SOURCE_DIR}/src/braille_text.c"
    "${POKEFIRERED_SOURCE_DIR}/src/buy_menu_helpers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/cable_car_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/cable_club.c"
    "${POKEFIRERED_SOURCE_DIR}/src/cereader_tool.c"
    "${POKEFIRERED_SOURCE_DIR}/src/clear_save_data_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/coins.c"
    "${POKEFIRERED_SOURCE_DIR}/src/coord_event_weather.c"
    "${POKEFIRERED_SOURCE_DIR}/src/credits.c"
    "${POKEFIRERED_SOURCE_DIR}/src/data.c"
    "${POKEFIRERED_SOURCE_DIR}/src/daycare.c"
    "${POKEFIRERED_SOURCE_DIR}/src/decompress.c"
    "${POKEFIRERED_SOURCE_DIR}/src/decoration.c"
    "${POKEFIRERED_SOURCE_DIR}/src/digit_obj_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/diploma.c"
    "${POKEFIRERED_SOURCE_DIR}/src/dma3_manager.c"
    "${POKEFIRERED_SOURCE_DIR}/src/dodrio_berry_picking.c"
    "${POKEFIRERED_SOURCE_DIR}/src/dodrio_berry_picking_comm.c"
    "${POKEFIRERED_SOURCE_DIR}/src/dynamic_placeholder_text_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/easy_chat.c"
    "${POKEFIRERED_SOURCE_DIR}/src/easy_chat_2.c"
    "${POKEFIRERED_SOURCE_DIR}/src/easy_chat_3.c"
    "${POKEFIRERED_SOURCE_DIR}/src/ereader_helpers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/ereader_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/event_data.c"
    "${POKEFIRERED_SOURCE_DIR}/src/event_object_lock.c"
    "${POKEFIRERED_SOURCE_DIR}/src/event_object_movement.c"
    "${POKEFIRERED_SOURCE_DIR}/src/evolution_graphics.c"
    "${POKEFIRERED_SOURCE_DIR}/src/evolution_scene.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fame_checker.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_camera.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_control_avatar.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_door.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_effect.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_effect_helpers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_fadetransition.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_message_box.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_player_avatar.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_poison.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_screen_effect.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_special_scene.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_specials.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_tasks.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_weather.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_weather_effects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/field_weather_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fieldmap.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_berrytree.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_cut.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_dig.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_flash.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_poison.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_rocksmash.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_softboiled.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_strength.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_sweetscent.c"
    "${POKEFIRERED_SOURCE_DIR}/src/fldeff_teleport.c"
    "${POKEFIRERED_SOURCE_DIR}/src/gpu_regs.c"
    "${POKEFIRERED_SOURCE_DIR}/src/graphics.c"
    "${POKEFIRERED_SOURCE_DIR}/src/hall_of_fame.c"
    "${POKEFIRERED_SOURCE_DIR}/src/heal_location.c"
    "${POKEFIRERED_SOURCE_DIR}/src/help_message.c"
    "${POKEFIRERED_SOURCE_DIR}/src/help_system.c"
    "${POKEFIRERED_SOURCE_DIR}/src/help_system_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/hof_pc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/image_processing_effects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/intro.c"
    # isagbprn.c excluded — host_debug.c provides native debug print implementations
    "${POKEFIRERED_SOURCE_DIR}/src/item.c"
    "${POKEFIRERED_SOURCE_DIR}/src/item_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/item_menu_icons.c"
    "${POKEFIRERED_SOURCE_DIR}/src/item_pc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/item_use.c"
    "${POKEFIRERED_SOURCE_DIR}/src/itemfinder.c"
    "${POKEFIRERED_SOURCE_DIR}/src/keyboard_text.c"
    "${POKEFIRERED_SOURCE_DIR}/src/learn_move.c"
    "${POKEFIRERED_SOURCE_DIR}/src/librfu_intr.c"
    "${POKEFIRERED_SOURCE_DIR}/src/librfu_rfu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/librfu_sio32id.c"
    "${POKEFIRERED_SOURCE_DIR}/src/librfu_stwi.c"
    "${POKEFIRERED_SOURCE_DIR}/src/link.c"
    "${POKEFIRERED_SOURCE_DIR}/src/link_rfu_2.c"
    "${POKEFIRERED_SOURCE_DIR}/src/link_rfu_3.c"
    "${POKEFIRERED_SOURCE_DIR}/src/list_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/load_save.c"
    "${POKEFIRERED_SOURCE_DIR}/src/m4a.c"
    "${POKEFIRERED_SOURCE_DIR}/src/m4a_tables.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mail.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mail_data.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mailbox_pc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/main.c"
    "${POKEFIRERED_SOURCE_DIR}/src/main_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/malloc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/map_name_popup.c"
    "${POKEFIRERED_SOURCE_DIR}/src/map_preview_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/math_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/menu2.c"
    "${POKEFIRERED_SOURCE_DIR}/src/menu_helpers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/menu_indicators.c"
    "${POKEFIRERED_SOURCE_DIR}/src/metatile_behavior.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mini_printf.c"
    "${POKEFIRERED_SOURCE_DIR}/src/minigame_countdown.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mon_markings.c"
    "${POKEFIRERED_SOURCE_DIR}/src/money.c"
    "${POKEFIRERED_SOURCE_DIR}/src/move_descriptions.c"
    "${POKEFIRERED_SOURCE_DIR}/src/multiboot.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_event_msg.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_event_script.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_client.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_link.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_scripts.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_server.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_show_card.c"
    "${POKEFIRERED_SOURCE_DIR}/src/mystery_gift_show_news.c"
    "${POKEFIRERED_SOURCE_DIR}/src/naming_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/new_game.c"
    "${POKEFIRERED_SOURCE_DIR}/src/new_menu_helpers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/oak_speech.c"
    "${POKEFIRERED_SOURCE_DIR}/src/option_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/overworld.c"
    "${POKEFIRERED_SOURCE_DIR}/src/palette.c"
    "${POKEFIRERED_SOURCE_DIR}/src/palette_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/party_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/party_menu_specials.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pc_screen_effect.c"
    "${POKEFIRERED_SOURCE_DIR}/src/play_time.c"
    "${POKEFIRERED_SOURCE_DIR}/src/player_pc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokeball.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokedex.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokedex_area_markers.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokedex_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_icon.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_jump.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_size_record.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_special_anim.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_special_anim_scene.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_data.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_graphics.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_misc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_storage_system_tasks.c"
    "${POKEFIRERED_SOURCE_DIR}/src/pokemon_summary_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/post_battle_event_funcs.c"
    "${POKEFIRERED_SOURCE_DIR}/src/prof_pc.c"
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log.c"
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log_battle.c"
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log_events.c"
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log_objects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/quest_log_player.c"
    "${POKEFIRERED_SOURCE_DIR}/src/random.c"
    "${POKEFIRERED_SOURCE_DIR}/src/region_map.c"
    "${POKEFIRERED_SOURCE_DIR}/src/renewable_hidden_items.c"
    "${POKEFIRERED_SOURCE_DIR}/src/reset_save_heap.c"
    "${POKEFIRERED_SOURCE_DIR}/src/reshow_battle_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/roamer.c"
    "${POKEFIRERED_SOURCE_DIR}/src/rom_header_gf.c"
    "${POKEFIRERED_SOURCE_DIR}/src/safari_zone.c"
    "${POKEFIRERED_SOURCE_DIR}/src/save.c"
    "${POKEFIRERED_SOURCE_DIR}/src/save_failed_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/save_location.c"
    "${POKEFIRERED_SOURCE_DIR}/src/save_menu_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/scanline_effect.c"
    "${POKEFIRERED_SOURCE_DIR}/src/scrcmd.c"
    "${POKEFIRERED_SOURCE_DIR}/src/script.c"
    "${POKEFIRERED_SOURCE_DIR}/src/script_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/script_movement.c"
    "${POKEFIRERED_SOURCE_DIR}/src/script_pokemon_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/seagallop.c"
    "${POKEFIRERED_SOURCE_DIR}/src/shop.c"
    "${POKEFIRERED_SOURCE_DIR}/src/slot_machine.c"
    "${POKEFIRERED_SOURCE_DIR}/src/sound.c"
    "${POKEFIRERED_SOURCE_DIR}/src/special_field_anim.c"
    "${POKEFIRERED_SOURCE_DIR}/src/sprite.c"
    "${POKEFIRERED_SOURCE_DIR}/src/ss_anne.c"
    "${POKEFIRERED_SOURCE_DIR}/src/start_menu.c"
    "${POKEFIRERED_SOURCE_DIR}/src/string_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/strings.c"
    "${POKEFIRERED_SOURCE_DIR}/src/task.c"
    "${POKEFIRERED_SOURCE_DIR}/src/teachy_tv.c"
    "${POKEFIRERED_SOURCE_DIR}/src/text.c"
    "${POKEFIRERED_SOURCE_DIR}/src/text_printer.c"
    "${POKEFIRERED_SOURCE_DIR}/src/text_window.c"
    "${POKEFIRERED_SOURCE_DIR}/src/text_window_graphics.c"
    "${POKEFIRERED_SOURCE_DIR}/src/tilemap_util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/tileset_anims.c"
    "${POKEFIRERED_SOURCE_DIR}/src/tilesets.c"
    "${POKEFIRERED_SOURCE_DIR}/src/title_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/tm_case.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trade.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trade_scene.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_card.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_fan_club.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_pokemon_sprites.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_see.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_tower.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trainer_tower_sets.c"
    "${POKEFIRERED_SOURCE_DIR}/src/trig.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_battle.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat_display.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_chat_objects.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_message.c"
    "${POKEFIRERED_SOURCE_DIR}/src/union_room_player_avatar.c"
    "${POKEFIRERED_SOURCE_DIR}/src/util.c"
    "${POKEFIRERED_SOURCE_DIR}/src/vs_seeker.c"
    "${POKEFIRERED_SOURCE_DIR}/src/wild_encounter.c"
    "${POKEFIRERED_SOURCE_DIR}/src/wild_pokemon_area.c"
    "${POKEFIRERED_SOURCE_DIR}/src/window.c"
    "${POKEFIRERED_SOURCE_DIR}/src/window_8bpp.c"
    "${POKEFIRERED_SOURCE_DIR}/src/wireless_communication_status_screen.c"
    "${POKEFIRERED_SOURCE_DIR}/src/wonder_news.c"
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
    "-iquote${PFR_INC_DIR}"
    "-iquote${PFR_GEN_HEADERS_DIR}"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
)
target_include_directories(pfr_upstream_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_dependencies(pfr_upstream_core pfr_generated_headers)

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

add_dependencies(pfr_upstream_intro pfr_asset_pipeline)
add_dependencies(pfr_upstream_title pfr_asset_pipeline)
add_dependencies(pfr_upstream_main_menu pfr_asset_pipeline)
add_dependencies(pfr_upstream_oak_speech pfr_asset_pipeline)
add_dependencies(pfr_upstream_text_window_graphics pfr_asset_pipeline)
add_dependencies(pfr_upstream_text pfr_asset_pipeline)
add_dependencies(pfr_upstream_new_menu_helpers pfr_asset_pipeline)

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
add_executable(pfr_play src/pfr_play.c)
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
)
target_compile_definitions(pfr_play PRIVATE ${PFR_COMMON_DEFINES})
target_compile_options(pfr_play PRIVATE
    -fno-pie
    "-iquote${CMAKE_CURRENT_SOURCE_DIR}/src/host_include"
    "-iquote${PFR_GEN_HEADERS_DIR}"
    "-iquote${POKEFIRERED_SOURCE_DIR}/include"
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
)
target_link_options(pfr_play PRIVATE
    -no-pie
    -Wl,--gc-sections
)
target_link_libraries(pfr_play PRIVATE pfr_upstream_core Threads::Threads m)

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
        make -C "${POKEFIRERED_SOURCE_DIR}/tools/gbagfx"
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
    BYPRODUCTS ${PFR_PREPROCESSED_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_ASSETS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_INC_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_GEN_DIR}"
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
    COMMENT "Asset pipeline: PNG/PAL → GBA → .inc → preprocessed sources"
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
