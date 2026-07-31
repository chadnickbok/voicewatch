idf_component_register(
    SRCS
        "src/app_runner.cpp"
        "src/app_sources.cpp"
        "src/display.cpp"
        "src/main.cpp"
        "../../ui/doodad_lvgl_ui.c"
    INCLUDE_DIRS
        "include"
        "../../ui"
    REQUIRES
        driver
        esp_driver_sdspi
        fatfs
        lvgl
        m3e_lvgl
        m5unified
        wasm-micro-runtime
    EMBED_FILES
        "embedded/hello.wasm"
)

option(
    DOODAD_SHOW_APP_AT_BOOT
    "Keep the selected embedded app visible instead of entering system Home"
    OFF
)
if(DOODAD_SHOW_APP_AT_BOOT)
    target_compile_definitions(
        ${COMPONENT_LIB}
        PRIVATE DOODAD_SHOW_APP_AT_BOOT=1
    )
endif()

set(
    DOODAD_BOOT_CATALOG_STORY
    "-1"
    CACHE STRING
    "Native catalog story to show at boot; -1 enters the normal shell"
)
target_compile_definitions(
    ${COMPONENT_LIB}
    PRIVATE DOODAD_BOOT_CATALOG_STORY=${DOODAD_BOOT_CATALOG_STORY}
)
