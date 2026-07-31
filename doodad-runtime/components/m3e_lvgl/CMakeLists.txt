set(M3E_LVGL_SOURCES
    src/assets/calculator_font_20.c
    src/assets/calculator_result_font_40.c
    src/assets/nutrition_font_32.c
    src/assets/timer_font_55.c
    src/assets/timer_value_font_28.c
    src/assets/live_action_font_32.c
    src/assets/media_art_asset.c
    src/assets/wallet_qr_asset.c
    src/assets/image_assets.cpp
    src/assets/weather_font_55.c
    src/appspec/c_api.cpp
    src/appspec/command_batch.cpp
    src/appspec/renderer.cpp
    src/appspec/runtime.cpp
    src/appspec/scene_snapshot.cpp
    src/appspec/wire.cpp
    src/catalog/catalog.cpp
    src/components/components.cpp
    src/components/transforming_list.cpp
    src/conformance/deterministic_system.cpp
    src/foundation/color.cpp
    src/foundation/display_profile.cpp
    src/foundation/geometry.cpp
    src/foundation/semantic_tokens.cpp
    src/navigation/route_stack.cpp
    src/os/shell_state.cpp
    src/os/surface_registry.cpp
    src/semantics/semantic_tree.cpp
    src/services/exact_scheduler.cpp
    src/services/exact_scheduler_c.cpp
    src/services/provider_event_c.cpp
    src/state/binding_hub.cpp
    src/state/store.cpp
    src/theme/resolved_theme.cpp
    src/theme/style_registry.cpp
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${M3E_LVGL_SOURCES}
        INCLUDE_DIRS include
        REQUIRES lvgl
    )
    target_compile_options(${COMPONENT_LIB} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    )
else()
    add_library(m3e_lvgl STATIC ${M3E_LVGL_SOURCES})
    add_library(m3e::lvgl ALIAS m3e_lvgl)
    target_include_directories(m3e_lvgl PUBLIC include)
    target_compile_features(m3e_lvgl PUBLIC cxx_std_17)
    target_compile_options(m3e_lvgl PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    )
    target_link_libraries(m3e_lvgl PUBLIC lvgl::lvgl)
endif()
