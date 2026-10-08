cmake_minimum_required(VERSION 3.16)
project(passport_sim C CXX)

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

# The simulator library: LVGL + the real application UI code, with the
# ESP-IDF/BSP/FreeRTOS surface provided by host stubs in this directory.
add_library(passport_sim SHARED
    sim.c
    sim_display.c
    sim_audio.c
    sim_platform.c
    "${REPO_ROOT}/main/app_tuner.c"
    "${REPO_ROOT}/main/tuner_pitch.c"
    "${REPO_ROOT}/components/bsp/src/bsp_display_rounding.c"
    "${REPO_ROOT}/assets/fonts/tuner_font_20.c"
)

# Stub include dir first so host esp_*/FreeRTOS headers win over anything else.
target_include_directories(passport_sim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${REPO_ROOT}/main"
    "${REPO_ROOT}/components/bsp/include"
    "${REPO_ROOT}/components/bsp/src"
    "${REPO_ROOT}/assets/fonts"
)

target_link_libraries(passport_sim PRIVATE lvgl m Threads::Threads)

set_target_properties(passport_sim PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
