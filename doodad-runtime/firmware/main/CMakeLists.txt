if(CONFIG_DOODAD_BOARD_TWATCH_ULTRA)
    set(DOODAD_BOARD_SOURCE "src/board_twatch_ultra.cpp")
    set(DOODAD_BOARD_REQUIREMENTS twatch_ultra twatch_ultra_ui)
elseif(CONFIG_DOODAD_BOARD_TWATCH_S3)
    set(DOODAD_BOARD_SOURCE "src/board_twatch_s3.cpp")
    set(DOODAD_BOARD_REQUIREMENTS esp_lcd)
else()
    set(DOODAD_BOARD_SOURCE "src/board_cores3.cpp")
    set(DOODAD_BOARD_REQUIREMENTS m5unified)
endif()

if(CONFIG_DOODAD_VOICE_TRANSPORT_MOQ)
    set(DOODAD_MEDIA_SOURCE "src/voice_media_moq.cpp")
else()
    set(DOODAD_MEDIA_SOURCE "src/voice_media_webrtc.cpp")
endif()

idf_component_register(
    SRCS
        "src/app_runner.cpp"
        "src/app_sources.cpp"
        "src/package_registry.cpp"
        "src/package_service.cpp"
        "src/personal_bundle.cpp"
        "${DOODAD_BOARD_SOURCE}"
        "src/display.cpp"
        "src/main.cpp"
        "src/network_service.cpp"
        "src/voice_service.cpp"
        "${DOODAD_MEDIA_SOURCE}"
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
        esp_partition
        esp_driver_sdspi
        esp_netif
        esp_websocket_client
        esp_wifi
        fatfs
        json
        lvgl
        m3e_lvgl
        # IDF discovers requirements before Kconfig board choices are available.
        # Keep this list unconditional; only the selected board source links.
        esp_lcd
        m5unified
        twatch_ultra
        twatch_ultra_ui
        mbedtls
        mdns
        nvs_flash
        wasm-micro-runtime
    EMBED_FILES
        "embedded/hello.wasm"
)

# Components are discovered before Kconfig. Link the selected implementation's
# dependencies after registration, when the selected transport is known.
if(CONFIG_DOODAD_VOICE_TRANSPORT_MOQ)
    idf_component_optional_requires(PRIVATE esp_moq_endpoint esp_moq_audio)
else()
    idf_component_optional_requires(PRIVATE espressif__esp_peer)
endif()

if(DOODAD_MOQ_DIAGNOSTIC_CONFIG_DIR)
    if(NOT CONFIG_DOODAD_VOICE_TRANSPORT_MOQ)
        message(FATAL_ERROR "The explicit MoQ diagnostic requires the Ultra MoQ transport")
    endif()
    target_sources(${COMPONENT_LIB} PRIVATE "src/voice_moq_diagnostic.cpp")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${DOODAD_MOQ_DIAGNOSTIC_CONFIG_DIR}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DOODAD_MOQ_DIAGNOSTIC=1)
endif()

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
