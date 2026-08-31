cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components/m3e_lvgl"
    "${CMAKE_CURRENT_LIST_DIR}/../../libs/moq-esp32/boards/twatch_ultra/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(doodad_runtime)

if(CONFIG_DOODAD_BOARD_TWATCH_ULTRA)
    # LVGL's fixed 128 KiB pool otherwise consumes scarce internal SRAM before
    # boot. Its supported large-array annotation keeps the bounded pool in
    # PSRAM without modifying a managed dependency or changing allocator policy.
    idf_component_get_property(DOODAD_LVGL_LIB lvgl__lvgl COMPONENT_LIB)
    target_compile_options(${DOODAD_LVGL_LIB} PRIVATE
        -include "${CMAKE_CURRENT_LIST_DIR}/boards/t-watch-ultra/lvgl_psram.h")
endif()
