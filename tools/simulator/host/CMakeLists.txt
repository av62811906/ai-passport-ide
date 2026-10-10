cmake_minimum_required(VERSION 3.16)
project(passport_sim C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

# Prefer the ESP-IDF managed LVGL (already vendored by a device build); fall
# back to the build-local clone produced by tools/simulator/fetch_lvgl.sh.
set(LVGL_DIR "${REPO_ROOT}/managed_components/lvgl__lvgl")
if(NOT EXISTS "${LVGL_DIR}/CMakeLists.txt")
    set(LVGL_DIR "${REPO_ROOT}/build/simulator/lvgl")
endif()
if(NOT EXISTS "${LVGL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "LVGL 9.5.0 not found. Run tools/simulator/fetch_lvgl.sh first.")
endif()

# Use lv_conf.h from this directory; drop the optional LVGL extras we do not
# render (demos/examples/ThorVG vectors) to keep the host build lean.
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl")

# PokeWalk's embedded sprite/font/move assets. On the device main/CMakeLists.txt
# uses EMBED_FILES; here the same `_binary_<name>_bin_start/_end` symbols are
# produced with the assembler's .incbin so the game's asset parser is untouched.
set(POKEWALK_ASSET_DIR "${REPO_ROOT}/assets/pokewalk")
set(POKEWALK_ASSETS gen1 gen1_front gen1_back palettes font16 moves ui)
set(POKEWALK_ASSET_SRC "${CMAKE_CURRENT_BINARY_DIR}/pokewalk_assets.S")
file(WRITE "${POKEWALK_ASSET_SRC}" "// Generated: embeds PokeWalk assets as _binary_<name>_bin_start/_end.\n")
foreach(asset IN LISTS POKEWALK_ASSETS)
    file(APPEND "${POKEWALK_ASSET_SRC}"
        ".section __TEXT,__const\n"
        ".globl _binary_${asset}_bin_start\n"
        "_binary_${asset}_bin_start:\n"
        ".incbin \"${POKEWALK_ASSET_DIR}/${asset}.bin\"\n"
        ".globl _binary_${asset}_bin_end\n"
        "_binary_${asset}_bin_end:\n")
endforeach()

# The simulator library: LVGL + the real application UI code, with the
# ESP-IDF/BSP/FreeRTOS surface provided by host stubs in this directory.
#
# PokeWalk (main/app_pokemon.c + main/pokewalk/) is compiled with HOST_BUILD so
# its device-only paths (USB serial screenshots, NVS-backed dungeon storage, the
# Wi-Fi time portal) switch to host hooks. Modules that only serve the USB
# console or the radio are left out entirely and replaced by sim_pokewalk.c:
#   dbg.c              serial debug-key injection (its task is never started)
#   wifi_time.c        Wi-Fi captive portal + SNTP clock sync
#   usb_backup_device.c  partition/NVS backup over the USB console
add_library(passport_sim SHARED
    sim.c
    sim_display.c
    sim_audio.c
    sim_platform.c
    sim_esp_idf.c
    sim_pokewalk.c
    "${POKEWALK_ASSET_SRC}"
    "${REPO_ROOT}/main/app_home.c"
    "${REPO_ROOT}/main/app_tuner.c"
    "${REPO_ROOT}/main/app_pokemon.c"
    "${REPO_ROOT}/main/tuner_pitch.c"
    "${REPO_ROOT}/main/ui_pixel.c"
    "${REPO_ROOT}/main/pokewalk/achievements.c"
    "${REPO_ROOT}/main/pokewalk/assets.c"
    "${REPO_ROOT}/main/pokewalk/audio.c"
    "${REPO_ROOT}/main/pokewalk/audio_settings.c"
    "${REPO_ROOT}/main/pokewalk/battery_ui.c"
    "${REPO_ROOT}/main/pokewalk/battle.c"
    "${REPO_ROOT}/main/pokewalk/battle_escape.c"
    "${REPO_ROOT}/main/pokewalk/battle_fx.c"
    "${REPO_ROOT}/main/pokewalk/battle_hud.c"
    "${REPO_ROOT}/main/pokewalk/battle_presentation.c"
    "${REPO_ROOT}/main/pokewalk/box_view.c"
    "${REPO_ROOT}/main/pokewalk/capture.c"
    "${REPO_ROOT}/main/pokewalk/combat.c"
    "${REPO_ROOT}/main/pokewalk/cry.c"
    "${REPO_ROOT}/main/pokewalk/display_settings.c"
    "${REPO_ROOT}/main/pokewalk/dungeon.c"
    "${REPO_ROOT}/main/pokewalk/dungeon_rewards.c"
    "${REPO_ROOT}/main/pokewalk/encounter.c"
    "${REPO_ROOT}/main/pokewalk/evolution.c"
    "${REPO_ROOT}/main/pokewalk/evolution_ui.c"
    "${REPO_ROOT}/main/pokewalk/exp.c"
    "${REPO_ROOT}/main/pokewalk/exploration.c"
    "${REPO_ROOT}/main/pokewalk/game_ui.c"
    "${REPO_ROOT}/main/pokewalk/gold_fx.c"
    "${REPO_ROOT}/main/pokewalk/growth_ui.c"
    "${REPO_ROOT}/main/pokewalk/items.c"
    "${REPO_ROOT}/main/pokewalk/music.c"
    "${REPO_ROOT}/main/pokewalk/music_director.c"
    "${REPO_ROOT}/main/pokewalk/nav.c"
    "${REPO_ROOT}/main/pokewalk/nurture.c"
    "${REPO_ROOT}/main/pokewalk/opening.c"
    "${REPO_ROOT}/main/pokewalk/party.c"
    "${REPO_ROOT}/main/pokewalk/play_achievements.c"
    "${REPO_ROOT}/main/pokewalk/play_bag.c"
    "${REPO_ROOT}/main/pokewalk/play_battle.c"
    "${REPO_ROOT}/main/pokewalk/play_capture.c"
    "${REPO_ROOT}/main/pokewalk/play_care.c"
    "${REPO_ROOT}/main/pokewalk/play_collect.c"
    "${REPO_ROOT}/main/pokewalk/play_dex.c"
    "${REPO_ROOT}/main/pokewalk/play_dungeon.c"
    "${REPO_ROOT}/main/pokewalk/play_enc.c"
    "${REPO_ROOT}/main/pokewalk/play_exploration.c"
    "${REPO_ROOT}/main/pokewalk/play_idle.c"
    "${REPO_ROOT}/main/pokewalk/play_menu.c"
    "${REPO_ROOT}/main/pokewalk/play_opening.c"
    "${REPO_ROOT}/main/pokewalk/play_party.c"
    "${REPO_ROOT}/main/pokewalk/play_starter.c"
    "${REPO_ROOT}/main/pokewalk/play_trainer.c"
    "${REPO_ROOT}/main/pokewalk/pokemon_animation.c"
    "${REPO_ROOT}/main/pokewalk/pokemon_names.c"
    "${REPO_ROOT}/main/pokewalk/render.c"
    "${REPO_ROOT}/main/pokewalk/restore_journal.c"
    "${REPO_ROOT}/main/pokewalk/save.c"
    "${REPO_ROOT}/main/pokewalk/screen.c"
    "${REPO_ROOT}/main/pokewalk/screen_idle.c"
    "${REPO_ROOT}/main/pokewalk/sensing.c"
    "${REPO_ROOT}/main/pokewalk/sfx.c"
    "${REPO_ROOT}/main/pokewalk/shiny_entry.c"
    "${REPO_ROOT}/main/pokewalk/sound_mixer.c"
    "${REPO_ROOT}/main/pokewalk/trainer.c"
    "${REPO_ROOT}/main/pokewalk/transition.c"
    "${REPO_ROOT}/main/pokewalk/usb_backup.c"
    "${REPO_ROOT}/main/pokewalk/world.c"
    "${REPO_ROOT}/components/bsp/src/bsp_display_rounding.c"
    "${REPO_ROOT}/assets/fonts/tuner_font_20.c"
    "${REPO_ROOT}/assets/fonts/app_font_12.c"
)

# Stub include dir first so host esp_*/FreeRTOS headers win over anything else.
target_include_directories(passport_sim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${REPO_ROOT}/main"
    "${REPO_ROOT}/main/pokewalk"
    "${REPO_ROOT}/components/bsp/include"
    "${REPO_ROOT}/components/bsp/src"
    "${REPO_ROOT}/assets/fonts"
)

# PokeWalk's host path: device-only code compiles out, the dungeon store uses the
# host hooks in sim_pokewalk.c, and the CRC uses zlib (both matching the device
# values). zlib is linked for that CRC.
target_compile_definitions(passport_sim PRIVATE HOST_BUILD=1)

target_link_libraries(passport_sim PRIVATE lvgl m Threads::Threads z)

set_target_properties(passport_sim PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
