if(CONFIG_DOODAD_BOARD_TWATCH_S3)
    set(DOODAD_BOARD_SOURCE "src/board_twatch_s3.cpp")
    set(DOODAD_BOARD_REQUIREMENTS esp_lcd)
else()
    set(DOODAD_BOARD_SOURCE "src/board_cores3.cpp")
    set(DOODAD_BOARD_REQUIREMENTS m5unified)
endif()

idf_component_register(
    SRCS
        "src/app_runner.cpp"
        "src/app_sources.cpp"
        "${DOODAD_BOARD_SOURCE}"
        "src/display.cpp"
        "src/main.cpp"
        "src/network_service.cpp"
        "src/voice_service.cpp"
        "src/weather_provider.cpp"
        "../../ui/doodad_lvgl_ui.c"
    INCLUDE_DIRS
        "include"
        "../../ui"
    REQUIRES
        driver
        esp_event
        esp_audio_codec
        esp_http_client
        esp_peer
        esp_driver_sdspi
        esp_netif
        esp_websocket_client
        esp_wifi
        fatfs
        json
        lvgl
        m3e_lvgl
        ${DOODAD_BOARD_REQUIREMENTS}
        mbedtls
        mdns
        nvs_flash
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
