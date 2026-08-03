#include <cassert>
#include <array>
#include <cstring>
#include <cstdint>

#include "lvgl.h"
#include "m3e/assets/image_assets.hpp"
#include "m3e/assets/weather_icon_assets.hpp"
#include "m3e/appspec/command_batch.hpp"
#include "m3e/appspec/canvas_display_list.hpp"
#include "m3e/appspec/renderer.hpp"
#include "m3e/appspec/runtime.hpp"
#include "m3e/appspec/wire.hpp"
#include "m3e/components/components.hpp"
#include "m3e/components/transforming_list.hpp"
#include "m3e/foundation/geometry.hpp"
#include "m3e/foundation/semantic_tokens.hpp"
#include "m3e/generated/weather_icons.hpp"
#include "m3e/semantics/semantic_tree.hpp"
#include "m3e/navigation/route_stack.hpp"
#include "m3e/state/binding_hub.hpp"
#include "m3e/state/store.hpp"
#include "m3e/theme/resolved_theme.hpp"
#include "m3e/theme/style_registry.hpp"

LV_FONT_DECLARE(m3e_live_action_font_32);

int main() {
    using namespace m3e;

    {
        assert(appspec::validate_canvas_display_list(
            "v1|C0|R1,4,4,120,120,18|"
            "T2,8,8,14,14,8,8,"
            "00000000000000000000000002230400"
            "00000000000000000000000000000000",
            "07110d,10271c,52d88b,a8f279,ffcf66",
            184,
            128));
        assert(!appspec::validate_canvas_display_list(
            "v1|R1,4,4,120,120,18",
            "07110d,10271c",
            184,
            128));
        assert(!appspec::validate_canvas_display_list(
            "v1|C0|T2,8,8,14,14,8,8,2",
            "07110d,10271c,52d88b",
            184,
            128));
    }

    {
        ImageAssetView media_art{};
        assert(resolve_image_asset(
            "2fb9cd65b78719989e685e43a7179cb69f97e1dfb4604ebfad420cfb91d81028",
            media_art));
        assert(media_art.width == 96);
        assert(media_art.height == 64);
        assert(media_art.decoded_bytes == 96U * 64U * 2U);

        ImageAssetView wallet_qr{};
        assert(resolve_image_asset(
            "29ee6d97e8928b49fbbaa49c20a439a1930c4d82de2217490abbe5235a798254",
            wallet_qr));
        assert(wallet_qr.width == 135);
        assert(wallet_qr.height == 135);
        assert(wallet_qr.decoded_bytes == 135U * 135U * 2U);

        ImageAssetView remote_viewfinder{};
        assert(resolve_image_asset(
            "777d468ea847318acd22e2eb79f108e75e2674448e8b53fa8fba0bc08fd7b522",
            remote_viewfinder));
        assert(remote_viewfinder.width == 230);
        assert(remote_viewfinder.height == 150);
        assert(
            remote_viewfinder.decoded_bytes ==
            230U * 150U * 2U);

        ImageAssetView missing{};
        assert(!resolve_image_asset(
            "0000000000000000000000000000000000000000000000000000000000000000",
            missing));
        assert(missing.pixels == nullptr);
        assert(missing.decoded_bytes == 0);
    }

    constexpr std::array<std::uint8_t, 87> hello_appspec_cbor{
        0xa3, 0x00, 0x01, 0x01, 0x65, 0x68, 0x65, 0x6c, 0x6c, 0x6f,
        0x02, 0x82, 0xa4, 0x00, 0x6c, 0x68, 0x65, 0x6c, 0x6c, 0x6f,
        0x2e, 0x73, 0x63, 0x72, 0x65, 0x65, 0x6e, 0x01, 0x00, 0x02,
        0xf6, 0x03, 0xa2, 0x08, 0x03, 0x09, 0x01, 0xa5, 0x00, 0x6b,
        0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x2e, 0x74, 0x69, 0x74, 0x6c,
        0x65, 0x01, 0x04, 0x02, 0x00, 0x03, 0xa3, 0x00, 0x6b, 0x48,
        0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x77, 0x6f, 0x72, 0x6c, 0x64,
        0x04, 0x01, 0x09, 0x01, 0x06, 0x6b, 0x48, 0x65, 0x6c, 0x6c,
        0x6f, 0x20, 0x77, 0x6f, 0x72, 0x6c, 0x64,
    };
    {
        using namespace m3e::appspec;
        WireDocument document;
        const auto decoded = decode_canonical_cbor(
            hello_appspec_cbor.data(),
            hello_appspec_cbor.size(),
            document);
        assert(decoded.ok());
        assert(document.schema_version == 1);
        assert(std::strcmp(document.string_at(document.app_id_offset), "hello") == 0);
        assert(document.node_count == 2);
        assert(document.nodes[0].kind == ComponentKind::screen);
        assert(document.nodes[1].kind == ComponentKind::text);
        assert(std::strcmp(
                   document.string_at(
                       document.nodes[1].primary_text_offset),
                   "Hello world") == 0);
        assert(document.nodes[1].parent_index == 0);
        assert(document.nodes[0].child_count == 1);
        SemanticTree wire_semantics;
        assert(build_semantic_tree(document, wire_semantics));
        assert(wire_semantics.size() == 2);
        assert(
            wire_semantics.at(0).role == SemanticRole::screen);
        assert(
            wire_semantics.at(1).role == SemanticRole::heading);
        assert(std::strcmp(
                   wire_semantics.at(1).label,
                   "Hello world") == 0);

        assert(decode_canonical_cbor(
                   hello_appspec_cbor.data(),
                   hello_appspec_cbor.size() - 1,
                   document).error == WireError::truncated);
        auto trailing = std::array<std::uint8_t, 88>{};
        std::memcpy(
            trailing.data(),
            hello_appspec_cbor.data(),
            hello_appspec_cbor.size());
        trailing.back() = 0;
        assert(decode_canonical_cbor(
                   trailing.data(), trailing.size(), document).error ==
               WireError::trailing_data);

        auto noncanonical = std::array<std::uint8_t, 88>{};
        noncanonical[0] = 0xb8;
        noncanonical[1] = 0x03;
        std::memcpy(
            noncanonical.data() + 2,
            hello_appspec_cbor.data() + 1,
            hello_appspec_cbor.size() - 1);
        assert(decode_canonical_cbor(
                   noncanonical.data(),
                   noncanonical.size(),
                   document).error == WireError::non_canonical);

        auto wrong_properties = hello_appspec_cbor;
        bool replaced_kind = false;
        for (std::size_t index = 0;
             index + 4 < wrong_properties.size();
             ++index) {
            if (wrong_properties[index] == 0x01 &&
                wrong_properties[index + 1] == 0x04 &&
                wrong_properties[index + 2] == 0x02 &&
                wrong_properties[index + 3] == 0x00 &&
                wrong_properties[index + 4] == 0x03) {
                wrong_properties[index + 1] = 0x06;
                replaced_kind = true;
                break;
            }
        }
        assert(replaced_kind);
        assert(!decode_canonical_cbor(
                    wrong_properties.data(),
                    wrong_properties.size(),
                    document).ok());

        constexpr UiEvent event{
            1,
            "hello",
            "hello.screen",
            "hello.action",
            "say_hello",
            EventKind::tap,
            1000,
        };
        std::array<std::uint8_t, 256> event_bytes{};
        const auto event_size = encode_event_canonical_cbor(
            event, event_bytes.data(), event_bytes.size());
        assert(event_size > 0);
        assert(event_bytes[0] == 0xa7);
        assert(event_bytes[event_size - 3] == 0x19);
        assert(event_bytes[event_size - 2] == 0x03);
        assert(event_bytes[event_size - 1] == 0xe8);
        constexpr UiEvent key_event{
            1,
            "calculator",
            "calculator",
            "calculator.keys",
            "key_pressed",
            EventKind::tap,
            1001,
            EventValue::text("7"),
        };
        const auto key_event_size = encode_event_canonical_cbor(
            key_event, event_bytes.data(), event_bytes.size());
        assert(key_event_size > 0);
        assert(event_bytes[0] == 0xa8);
        assert(event_bytes[key_event_size - 3] == 0x07);
        assert(event_bytes[key_event_size - 2] == 0x61);
        assert(event_bytes[key_event_size - 1] == '7');
        constexpr UiEvent decrement_event{
            1,
            "workout",
            "active_set",
            "active_set.weight",
            "set_weight",
            EventKind::value_committed,
            1002,
            EventValue::integer(-5),
        };
        const auto decrement_event_size = encode_event_canonical_cbor(
            decrement_event, event_bytes.data(), event_bytes.size());
        assert(decrement_event_size > 0);
        assert(event_bytes[decrement_event_size - 2] == 0x07);
        assert(event_bytes[decrement_event_size - 1] == 0x24);

        constexpr std::array<std::uint8_t, 56> ui_batch_bytes{
            0xa2, 0x00, 0x01, 0x01, 0x82,
            0xa4, 0x00, 0x00, 0x01, 0x6b, 0x68, 0x65, 0x6c, 0x6c,
            0x6f, 0x2e, 0x74, 0x69, 0x74, 0x6c, 0x65, 0x02, 0x00,
            0x03, 0x6b, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x74,
            0x68, 0x65, 0x72, 0x65,
            0xa3, 0x00, 0x01, 0x01, 0x6c, 0x68, 0x65, 0x6c, 0x6c,
            0x6f, 0x2e, 0x61, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x03,
            0xf4,
        };
        CommandBatch ui_batch;
        const auto ui_decoded = decode_command_batch_canonical_cbor(
            ui_batch_bytes.data(), ui_batch_bytes.size(), ui_batch);
        assert(ui_decoded.ok());
        assert(ui_batch.schema_version == 1);
        assert(ui_batch.domain == CommandDomain::ui);
        assert(ui_batch.command_count == 2);
        assert(ui_batch.commands[0].kind == CommandKind::set_property);
        assert(
            ui_batch.commands[0].property == PropertyKind::primary_text);
        assert(std::strcmp(
                   ui_batch.string_at(ui_batch.commands[0].text_offset),
                   "Hello there") == 0);
        assert(
            ui_batch.commands[1].kind == CommandKind::set_visibility);
        assert(!ui_batch.commands[1].boolean_value);

        auto truncated_batch = ui_batch_bytes;
        assert(decode_command_batch_canonical_cbor(
                   truncated_batch.data(),
                   truncated_batch.size() - 1,
                   ui_batch).error == CommandError::truncated);
        auto unordered_batch = ui_batch_bytes;
        unordered_batch[1] = 0x01;
        assert(decode_command_batch_canonical_cbor(
                   unordered_batch.data(),
                   unordered_batch.size(),
                   ui_batch).error == CommandError::unexpected_key);

        constexpr std::array<std::uint8_t, 43> state_batch_bytes{
            0xa2, 0x00, 0x01, 0x01, 0x82,
            0xa4, 0x00, 0x03, 0x01, 0x6c, 0x73, 0x63, 0x72, 0x65,
            0x65, 0x6e, 0x2e, 0x63, 0x6f, 0x75, 0x6e, 0x74, 0x02,
            0x01, 0x03, 0x18, 0x2a,
            0xa2, 0x00, 0x04, 0x01, 0x6b, 0x61, 0x70, 0x70, 0x2e,
            0x73, 0x74, 0x61, 0x6c, 0x65,
        };
        CommandBatch state_batch;
        assert(decode_command_batch_canonical_cbor(
                   state_batch_bytes.data(),
                   state_batch_bytes.size(),
                   state_batch).ok());
        assert(state_batch.domain == CommandDomain::state);
        m3e::state::Store command_store;
        assert(apply_state_command_batch(
                   state_batch, command_store).ok());
        assert(command_store.get("screen.count") != nullptr);
        assert(
            command_store.get("screen.count")->value.integer_value == 42);
        assert(command_store.get("app.stale") == nullptr);

        std::array<std::uint8_t, 70> mixed_batch{};
        mixed_batch[0] = 0xa2;
        mixed_batch[1] = 0x00;
        mixed_batch[2] = 0x01;
        mixed_batch[3] = 0x01;
        mixed_batch[4] = 0x82;
        std::memcpy(
            mixed_batch.data() + 5,
            ui_batch_bytes.data() + 5,
            33);
        std::memcpy(
            mixed_batch.data() + 38,
            state_batch_bytes.data() + 5,
            27);
        assert(decode_command_batch_canonical_cbor(
                   mixed_batch.data(),
                   65,
                   state_batch).error == CommandError::mixed_domains);
    }

    assert(spacing_dp(SpacingRole::none) == 0);
    assert(spacing_dp(SpacingRole::xs) == 4);
    assert(spacing_dp(SpacingRole::lg) == 16);
    assert(state_opacity(StateKind::pressed) == 31);
    assert(state_opacity(StateKind::disabled) == 97);
    lv_font_glyph_dsc_t decimal_glyph{};
    assert(lv_font_get_glyph_dsc(
        &m3e_live_action_font_32,
        &decimal_glyph,
        '.',
        '3'));
    assert(decimal_glyph.adv_w > 0);
    lv_font_glyph_dsc_t percent_glyph{};
    assert(lv_font_get_glyph_dsc(
        &m3e_live_action_font_32,
        &percent_glyph,
        '%',
        '2'));
    assert(percent_glyph.adv_w > 0);

    const auto expressive = motion_spec(MotionToken::spatial_default);
    assert(expressive.duration_ms == 350);
    assert(expressive.spatial);
    assert(expressive.may_overshoot);
    const auto reduced = motion_spec(MotionToken::spatial_default, true);
    assert(reduced.duration_ms == 100);
    assert(!reduced.may_overshoot);
    assert(motion_progress_q16(MotionToken::spatial_default, 0) == 0);
    assert(
        motion_progress_q16(MotionToken::spatial_default, 350) == 65535);
    assert(
        motion_progress_q16(MotionToken::spatial_default, 100, true) ==
        65535);
    const auto success_haptic =
        haptic_pattern(HapticEvent::success);
    assert(success_haptic.pulse_count == 2);
    assert(success_haptic.intensity > 0);
    const auto reduced_haptic =
        haptic_pattern(HapticEvent::error, true);
    assert(reduced_haptic.pulse_count == 1);
    assert(
        reduced_haptic.intensity <
        haptic_pattern(HapticEvent::error).intensity);
    std::uint16_t previous_reduced = 0;
    for (std::uint32_t elapsed = 0; elapsed <= 100; elapsed += 25) {
        const auto value = motion_progress_q16(
            MotionToken::spatial_default, elapsed, true);
        assert(value >= previous_reduced);
        previous_reduced = value;
    }
    assert(shape_radius_dp(generated::ShapeRole::small, 40, 40) == 8);
    assert(shape_radius_dp(generated::ShapeRole::full, 40, 20) == 10);
    assert(interpolate_radius_dp(4, 18, 0) == 4);
    assert(interpolate_radius_dp(4, 18, 65535) == 18);
    const auto middle_group = button_group_layout(168, 3, 1);
    assert(middle_group.visual_widths_dp[1] >
           middle_group.visual_widths_dp[0]);
    assert(
        middle_group.visual_widths_dp[0] +
            middle_group.visual_widths_dp[1] +
            middle_group.visual_widths_dp[2] + 8 ==
        168);
    const auto centered_item =
        transforming_item_geometry(120, 240, 64);
    const auto edge_item =
        transforming_item_geometry(0, 240, 64);
    assert(centered_item.scale_q8_8 == 256);
    assert(centered_item.opacity == 255);
    assert(edge_item.scale_q8_8 < centered_item.scale_q8_8);
    assert(edge_item.transformed_height_px <
           centered_item.transformed_height_px);
    const auto reduced_edge =
        transforming_item_geometry(0, 240, 64, true);
    assert(reduced_edge.scale_q8_8 > edge_item.scale_q8_8);
    assert(preserve_anchor_scroll_offset(100, 60, 75) == 115);

    SemanticTree tree;
    assert(tree.add(
        {"screen", SemanticRole::screen, "Screen", nullptr, semantic_none,
         SemanticTree::kNoIndex, 1, 1}));
    assert(tree.add(
        {"save", SemanticRole::button, "Save", nullptr, semantic_none,
         0, SemanticTree::kNoIndex, 0}));
    assert(!tree.add(
        {"save", SemanticRole::button, "Duplicate", nullptr, semantic_none,
         0, SemanticTree::kNoIndex, 0}));
    assert(tree.size() == 2);
    assert(tree.find("save") != nullptr);
    assert(tree.validate());

    SemanticTree invalid;
    assert(invalid.add(
        {"screen", SemanticRole::screen, "Screen", nullptr, semantic_none,
         SemanticTree::kNoIndex, 1, 1}));
    assert(invalid.add(
        {"bad", SemanticRole::button, "", nullptr, semantic_none,
         0, SemanticTree::kNoIndex, 0}));
    assert(!invalid.validate());

    {
        using namespace m3e::appspec;
        constexpr Node nodes[] = {
            {"screen", ComponentKind::screen, 0xffffU, 0, 2, 1, "Home",
             false, true, true},
            {"title", ComponentKind::text, 0, 1, 0, 2, "Title",
             false, true, true},
            {"save", ComponentKind::button, 0, 1, 0, 3, "Save",
             true, true, true},
        };
        const auto validation = validate({nodes, 3});
        assert(validation.ok());

        Reconciler reconciler;
        assert(reconciler.mount({nodes, 3}).ok());
        assert(reconciler.size() == 3);
        assert(reconciler.generation() == 1);
        constexpr Patch valid_patches[] = {
            {PatchKind::set_properties, "title", nullptr,
             ComponentKind::text, 99, nullptr, false},
            {PatchKind::set_enabled, "save", nullptr,
             ComponentKind::button, 0, nullptr, false},
        };
        assert(reconciler.apply_transaction(valid_patches, 2));
        assert(reconciler.find("title")->props_hash == 99);
        assert(!reconciler.find("save")->enabled);
        const auto generation = reconciler.generation();
        constexpr Patch invalid_patch{
            PatchKind::set_properties, "missing", nullptr,
            ComponentKind::text, 55, nullptr, false};
        assert(!reconciler.apply_transaction(&invalid_patch, 1));
        assert(reconciler.generation() == generation);
        assert(reconciler.find("title")->props_hash == 99);

        const auto manifest = capabilities();
        assert(manifest.nodes_per_screen == 250);
        assert(manifest.tree_depth == 12);
        assert(manifest.component_set_hash == 0x2a68c009U);
        constexpr UiEvent event{
            1, "calories", "today", "quick_add", "open_quick_add",
            EventKind::tap, 100};
        assert(event_is_valid(event));
        for (std::uint64_t revision = 0; revision < 1000; ++revision) {
            const Patch patch{
                PatchKind::set_properties, "title", nullptr,
                ComponentKind::text, revision, nullptr, false};
            assert(reconciler.apply_transaction(&patch, 1));
        }
        assert(reconciler.find("title")->props_hash == 999);
    }

    {
        using namespace m3e::state;
        Store store;
        constexpr Permission nutrition{
            "shared.nutrition", true, true};
        const Operation initial[] = {
            {OperationKind::put, "app.goal", Value::integer(2000)},
            {OperationKind::put, "screen.filter", Value::string("today")},
            {OperationKind::put, "shared.nutrition.total",
             Value::integer(1420)},
        };
        assert(store.apply(initial, 3, &nutrition, 1));
        assert(store.size() == 3);
        assert(store.revision() == 1);
        assert(
            store.get("shared.nutrition.total")->value.integer_value ==
            1420);
        const auto revision = store.revision();
        const Operation forbidden[] = {
            {OperationKind::put, "app.goal", Value::integer(2100)},
            {OperationKind::put, "system.battery", Value::integer(80)},
        };
        assert(!store.apply(forbidden, 2, &nutrition, 1));
        assert(store.revision() == revision);
        assert(store.get("app.goal")->value.integer_value == 2000);
        const Operation unauthorized{
            OperationKind::put,
            "shared.workouts.last",
            Value::string("squat")};
        assert(!store.apply(&unauthorized, 1, &nutrition, 1));
        char formatted[32]{};
        assert(format_value(
            Value::integer(135), "lb", formatted, sizeof(formatted)));
        assert(std::strcmp(formatted, "135 lb") == 0);
        for (std::int64_t total = 0; total < 1000; ++total) {
            const Operation update{
                OperationKind::put,
                "shared.nutrition.total",
                Value::integer(total)};
            assert(store.apply(&update, 1, &nutrition, 1));
        }
        assert(
            store.get("shared.nutrition.total")->value.integer_value ==
            999);
    }

    {
        using namespace m3e::appspec;
        using namespace m3e::state;
        constexpr Node nodes[] = {
            {"screen", ComponentKind::screen, 0xffffU, 0, 2, 1, "Home",
             false, true, true},
            {"total", ComponentKind::text, 0, 1, 0, 2, "Total",
             false, true, true},
            {"save", ComponentKind::button, 0, 1, 0, 3, "Save",
             true, true, true},
        };
        Reconciler reconciler;
        assert(reconciler.mount({nodes, 3}).ok());

        Store store;
        const Operation initial[] = {
            {OperationKind::put, "screen.total", Value::integer(1420)},
            {OperationKind::put, "screen.can_save", Value::boolean(false)},
        };
        assert(store.apply(initial, 2));
        constexpr BindingSpec bindings[] = {
            {"total", "screen.total", BindingTarget::properties,
             BindingPredicate::value, 0, BindingFormat::number_with_unit,
             "kcal"},
            {"save", "screen.can_save", BindingTarget::enabled,
             BindingPredicate::value, 0, BindingFormat::raw, nullptr},
            {"save", "session.save_visible", BindingTarget::visible,
             BindingPredicate::exists, 0, BindingFormat::raw, nullptr},
        };
        BindingHub hub;
        assert(hub.mount(bindings, 3, reconciler).ok());
        const auto first_sync = hub.sync(store, reconciler);
        assert(first_sync.ok());
        assert(first_sync.patch_count == 3);
        assert(std::strcmp(
                   hub.rendered_value(
                       "total", BindingTarget::properties),
                   "1420 kcal") == 0);
        assert(!reconciler.find("save")->enabled);
        assert(!reconciler.find("save")->visible);

        const auto generation = reconciler.generation();
        assert(hub.sync(store, reconciler).patch_count == 0);
        assert(reconciler.generation() == generation);
        const Operation update[] = {
            {OperationKind::put, "screen.total", Value::integer(1500)},
            {OperationKind::put, "screen.can_save", Value::boolean(true)},
            {OperationKind::put, "session.save_visible", Value::boolean(true)},
        };
        assert(store.apply(update, 3));
        const auto second_sync = hub.sync(store, reconciler);
        assert(second_sync.ok());
        assert(second_sync.patch_count == 3);
        assert(reconciler.find("save")->enabled);
        assert(reconciler.find("save")->visible);
        assert(std::strcmp(
                   hub.rendered_value(
                       "total", BindingTarget::properties),
                   "1500 kcal") == 0);

        const BindingSpec invalid_binding{
            "save", "screen.total", BindingTarget::enabled,
            BindingPredicate::value, 0, BindingFormat::raw, nullptr};
        BindingHub invalid_hub;
        assert(invalid_hub.mount(
            &invalid_binding, 1, reconciler).ok());
        const auto before_invalid = reconciler.generation();
        const auto failed = invalid_hub.sync(store, reconciler);
        assert(failed.error == BindingError::type_mismatch);
        assert(reconciler.generation() == before_invalid);
    }

    {
        using namespace m3e::navigation;
        RouteStack routes;
        assert(routes.reset("home"));
        assert(routes.push("calories.today", LayerOwner::application, 4));
        assert(routes.snapshot_active(17, 220));
        assert(routes.show_overlay("system.voice", LayerOwner::system));
        assert(routes.overlay() != nullptr);
        assert(!routes.dismiss_overlay(LayerOwner::application));
        assert(routes.dismiss_overlay(LayerOwner::system));
        assert(routes.restore_target(4) != nullptr);
        assert(routes.restore_target(5) == nullptr);
        assert(routes.pop(LayerOwner::application));
        assert(std::strcmp(routes.active()->id.data(), "home") == 0);
        assert(!routes.pop(LayerOwner::system));
    }

    lv_init();
    {
        StyleRegistry styles;
        auto theme = baseline_dark_theme();
        assert(styles.initialize(theme));
        assert(styles.initialized());
        assert(styles.generation() == 1);
        theme.metadata.theme_id = "test-theme";
        assert(styles.apply_theme(theme));
        assert(styles.generation() == 2);
        assert(styles.theme().metadata.theme_id == theme.metadata.theme_id);
        auto invalid_theme = theme;
        invalid_theme.color.roles[0].rgb565.value ^= 1U;
        assert(!styles.apply_theme(invalid_theme));
        assert(styles.generation() == 2);
        for (std::uint32_t index = 0; index < 1000; ++index) {
            assert(styles.apply_theme(theme));
        }
        assert(styles.generation() == 1002);

        auto* display = lv_display_create(240, 240);
        assert(display != nullptr);
        ComponentFactory component_factory(styles);
        auto* component_root = lv_screen_active();
        component_factory.screen(component_root);
        auto* icon = component_factory.icon(
            component_root, IconName::microphone);
        assert(std::strcmp(
                   lv_label_get_text(icon),
                   LV_SYMBOL_AUDIO) == 0);
        auto* animated = component_factory.animated_text(
            component_root,
            "1",
            generated::TypographyRole::numeral_large);
        assert(component_factory.set_animated_text(
            animated, "2", true, true));
        assert(std::strcmp(
                   lv_label_get_text(lv_obj_get_child(animated, 0)),
                   "2") == 0);
        assert(lv_obj_has_flag(
            lv_obj_get_child(animated, 1),
            LV_OBJ_FLAG_HIDDEN));
        assert(component_factory.set_animated_text(
            animated, "3", true, false));
        assert(!lv_obj_has_flag(
            lv_obj_get_child(animated, 1),
            LV_OBJ_FLAG_HIDDEN));
        auto* icon_action = component_factory.icon_button(
            component_root,
            "settings",
            IconName::settings,
            ButtonVariant::outlined,
            ComponentSize::compact,
            false,
            true);
        assert(lv_obj_has_state(icon_action, LV_STATE_DISABLED));
        assert(lv_obj_has_state(icon_action, LV_STATE_CHECKED));
        assert(lv_obj_get_child_count(icon_action) == 1);
        auto* icon_toggle = component_factory.icon_toggle_button(
            component_root,
            "play",
            IconName::play,
            true);
        assert(lv_obj_has_flag(
            icon_toggle, LV_OBJ_FLAG_CHECKABLE));
        constexpr ButtonProps expressive_buttons[] = {
            {"one", "One", Tone::primary, ButtonVariant::filled,
             ComponentSize::compact, true, false},
            {"two", "Two", Tone::secondary, ButtonVariant::filled,
             ComponentSize::compact, true, false},
            {"three", "Three", Tone::tertiary, ButtonVariant::filled,
             ComponentSize::compact, true, false},
        };
        auto* expressive_group = component_factory.button_group(
            component_root,
            expressive_buttons,
            3);
        const auto animations_before_press =
            lv_anim_count_running();
        lv_obj_send_event(
            lv_obj_get_child(expressive_group, 1),
            LV_EVENT_PRESSED,
            nullptr);
        assert(
            lv_anim_count_running() >=
            animations_before_press + 3);
        lv_obj_send_event(
            lv_obj_get_child(expressive_group, 1),
            LV_EVENT_RELEASED,
            nullptr);
        auto* header = component_factory.list_header(
            component_root, "Today");
        auto* subheader = component_factory.list_header(
            component_root, "Recent", true);
        assert(lv_obj_get_height(header) == 42);
        assert(lv_obj_get_height(subheader) == 32);
        auto* segmented =
            component_factory.segmented_circular_progress(
                component_root,
                {"Segments", 3, 5, Tone::primary},
                5);
        assert(lv_obj_get_child_count(segmented) == 5);
        constexpr const char* picker_items[] = {
            "One", "Two", "Three"};
        auto* picker = component_factory.picker(
            component_root, {picker_items, 3, 1});
        assert(lv_obj_get_child_count(picker) == 3);
        auto* date_picker = component_factory.date_picker(
            component_root, 2026, 7, 30);
        assert(lv_obj_get_child_count(date_picker) == 3);
        assert(lv_obj_get_child_count(
                   lv_obj_get_child(date_picker, 0)) == 12);
        assert(lv_obj_get_child_count(
                   lv_obj_get_child(date_picker, 1)) == 31);
        lv_obj_delete(date_picker);
        auto* time_picker = component_factory.time_picker(
            component_root, 21, 45, true);
        assert(lv_obj_get_child_count(time_picker) == 2);
        assert(lv_obj_get_child_count(
                   lv_obj_get_child(time_picker, 0)) == 24);
        assert(lv_obj_get_child_count(
                   lv_obj_get_child(time_picker, 1)) == 60);
        lv_obj_delete(time_picker);
        auto* pager = component_factory.horizontal_pager(
            component_root, 3, 1);
        assert(lv_obj_get_child_count(pager) == 3);
        auto* pager_scaffold =
            component_factory.horizontal_pager_scaffold(
                component_root, 3, 1, "9:41");
        assert(lv_obj_get_child_count(pager_scaffold) == 3);
        auto* animated_page =
            component_factory.animated_page(component_root);
        assert(
            ComponentFactory::animated_page_slot(
                animated_page, 0) != nullptr);
        assert(
            ComponentFactory::animated_page_slot(
                animated_page, 2) == nullptr);
        component_factory.text(
            ComponentFactory::animated_page_slot(
                animated_page, 0),
            "First",
            generated::TypographyRole::title_medium);
        component_factory.text(
            ComponentFactory::animated_page_slot(
                animated_page, 1),
            "Second",
            generated::TypographyRole::title_medium);
        assert(ComponentFactory::show_animated_page(
            animated_page, 1, true, true));
        assert(lv_obj_has_flag(
            ComponentFactory::animated_page_slot(
                animated_page, 0),
            LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(
            ComponentFactory::animated_page_slot(
                animated_page, 1),
            LV_OBJ_FLAG_HIDDEN));
        assert(ComponentFactory::show_animated_page(
            animated_page, 0, false, false));
        auto* expanding =
            component_factory.fading_expanding_label(
                component_root,
                "A bounded label that can reveal more detail.",
                28,
                false);
        assert(lv_obj_get_height(expanding) == 28);
        assert(
            ComponentFactory::
                set_fading_expanding_label_expanded(
                    expanding, true, true));
        assert(lv_obj_has_state(
            expanding, LV_STATE_CHECKED));
        auto* swipe = component_factory.swipe_to_dismiss_box(
            component_root);
        int dismiss_events = 0;
        lv_obj_add_event_cb(
            swipe,
            [](lv_event_t* event) {
                auto* count = static_cast<int*>(
                    lv_event_get_user_data(event));
                ++*count;
            },
            LV_EVENT_CANCEL,
            &dismiss_events);
        lv_obj_scroll_to_x(swipe, 0, LV_ANIM_OFF);
        lv_obj_send_event(swipe, LV_EVENT_SCROLL_END, nullptr);
        assert(dismiss_events == 1);
        auto* reveal = component_factory.swipe_to_reveal(
            component_root, "Done", "Later");
        assert(lv_obj_get_child_count(reveal) == 2);
        int reveal_events = 0;
        lv_obj_add_event_cb(
            reveal,
            [](lv_event_t* event) {
                auto* count = static_cast<int*>(
                    lv_event_get_user_data(event));
                ++*count;
            },
            LV_EVENT_READY,
            &reveal_events);
        lv_obj_scroll_to_x(reveal, 0, LV_ANIM_OFF);
        lv_obj_send_event(reveal, LV_EVENT_SCROLL_END, nullptr);
        assert(reveal_events == 1);
        auto* split = component_factory.split_selection_row(
            component_root,
            "Notifications",
            SelectionKind::switch_control,
            true);
        assert(lv_obj_get_child_count(split) == 2);
        assert(lv_obj_has_state(
            lv_obj_get_child(split, 1), LV_STATE_CHECKED));
        assert(ComponentFactory::morph_shape_state(
            lv_obj_get_child(split, 1), false, true));
        assert(!lv_obj_has_state(
            lv_obj_get_child(split, 1), LV_STATE_CHECKED));
        auto* indicator = component_factory.page_indicator(
            component_root, 3, 1);
        assert(lv_obj_get_child_count(indicator) == 3);
        auto* transcript = component_factory.transcript(
            component_root, "Final phrase", "partial");
        assert(lv_obj_get_child_count(transcript) == 2);
        auto* progress = component_factory.build_progress(
            component_root,
            {"Compiling", 3, 8, true});
        assert(lv_obj_get_child_count(progress) == 3);
        constexpr const char* clarification_choices[] = {
            "Today", "Yesterday", "This week", "Ignored"};
        auto* clarification =
            component_factory.clarification_choice_group(
                component_root,
                clarification_choices,
                4);
        assert(lv_obj_get_child_count(clarification) == 4);
        auto* glance = component_factory.live_card(
            component_root,
            {
                "Calories",
                "1,420 kcal",
                "580 remaining",
                "Updated now",
                1420,
                2000,
                Tone::primary,
            });
        assert(lv_obj_get_child_count(glance) >= 5);
        assert(std::strcmp(
                   static_cast<const char*>(
                       lv_obj_get_user_data(glance)),
                   "Calories") == 0);
        auto* status = component_factory.status_chip(
            component_root,
            "Offline",
            IconName::warning,
            Tone::error);
        assert(lv_obj_get_child_count(status) == 2);
        constexpr TransformingListItem list_items[] = {
            {"item.0", "Item 0", "Detail", Tone::neutral},
            {"item.1", "Item 1", "Detail", Tone::neutral},
            {"item.2", "Item 2", "Detail", Tone::neutral},
            {"item.3", "Item 3", "Detail", Tone::neutral},
            {"item.4", "Item 4", "Detail", Tone::neutral},
            {"item.5", "Item 5", "Detail", Tone::neutral},
            {"item.6", "Item 6", "Detail", Tone::neutral},
            {"item.7", "Item 7", "Detail", Tone::neutral},
            {"item.8", "Item 8", "Detail", Tone::neutral},
            {"item.9", "Item 9", "Detail", Tone::neutral},
            {"item.10", "Item 10", "Detail", Tone::neutral},
            {"item.11", "Item 11", "Detail", Tone::neutral},
        };
        auto* transforming = component_factory.transforming_list(
            component_root,
            list_items,
            static_cast<std::uint16_t>(std::size(list_items)));
        assert(transforming != nullptr);
        assert(
            ComponentFactory::transforming_list_mounted_count(
                transforming) == 8);
        auto* transforming_content =
            lv_obj_get_child(transforming, 0);
        assert(std::strcmp(
                   static_cast<const char*>(lv_obj_get_user_data(
                       lv_obj_get_child(transforming_content, 0))),
                   "item.0") == 0);
        lv_obj_scroll_to_y(transforming, 480, LV_ANIM_OFF);
        lv_obj_send_event(transforming, LV_EVENT_SCROLL, nullptr);
        assert(
            ComponentFactory::transforming_list_mounted_count(
                transforming) <= 8);
        assert(std::strcmp(
                   static_cast<const char*>(lv_obj_get_user_data(
                       lv_obj_get_child(transforming_content, 0))),
                   "item.0") != 0);
        constexpr TransformingListItem inserted_list_items[] = {
            {"item.new", "New", "Detail", Tone::primary},
            {"item.0", "Item 0", "Detail", Tone::neutral},
            {"item.1", "Item 1", "Detail", Tone::neutral},
            {"item.2", "Item 2", "Detail", Tone::neutral},
            {"item.3", "Item 3", "Detail", Tone::neutral},
            {"item.4", "Item 4", "Detail", Tone::neutral},
            {"item.5", "Item 5", "Detail", Tone::neutral},
            {"item.6", "Item 6", "Detail", Tone::neutral},
            {"item.7", "Item 7", "Detail", Tone::neutral},
            {"item.8", "Item 8", "Detail", Tone::neutral},
            {"item.9", "Item 9", "Detail", Tone::neutral},
            {"item.10", "Item 10", "Detail", Tone::neutral},
            {"item.11", "Item 11", "Detail", Tone::neutral},
        };
        const auto scroll_before_insert =
            lv_obj_get_scroll_y(transforming);
        assert(ComponentFactory::update_transforming_list(
            transforming,
            inserted_list_items,
            static_cast<std::uint16_t>(
                std::size(inserted_list_items))));
        assert(
            lv_obj_get_scroll_y(transforming) ==
            scroll_before_insert + 60);
        assert(
            ComponentFactory::transforming_list_mounted_count(
                transforming) <= 8);

        auto* patched_progress = component_factory.linear_progress(
            component_root,
            {"Patched", 50, 100, Tone::primary});
        m3e::appspec::WireDocument progress_document;
        std::memcpy(
            progress_document.strings.data() + 1,
            "progress",
            9);
        progress_document.string_bytes = 10;
        progress_document.node_count = 1;
        progress_document.nodes[0].id_offset = 1;
        progress_document.nodes[0].kind =
            m3e::appspec::ComponentKind::progress;
        progress_document.nodes[0].value = 50;
        progress_document.nodes[0].maximum = 100;
        progress_document.nodes[0].mounted_object = patched_progress;
        m3e::appspec::CommandBatch progress_batch;
        std::memcpy(
            progress_batch.strings.data() + 1,
            "progress",
            9);
        progress_batch.string_bytes = 10;
        progress_batch.schema_version = 1;
        progress_batch.domain =
            m3e::appspec::CommandDomain::ui;
        progress_batch.command_count = 2;
        progress_batch.commands[0] = {
            m3e::appspec::CommandKind::set_property,
            m3e::appspec::PropertyKind::value,
            m3e::state::ValueType::integer,
            1,
            0,
            150,
            false,
        };
        progress_batch.commands[1] = {
            m3e::appspec::CommandKind::set_property,
            m3e::appspec::PropertyKind::maximum,
            m3e::state::ValueType::integer,
            1,
            0,
            200,
            false,
        };
        assert(m3e::appspec::apply_ui_command_batch(
                   progress_batch, progress_document).ok());
        assert(lv_bar_get_max_value(patched_progress) == 200);
        assert(lv_bar_get_value(patched_progress) == 150);
        progress_batch.command_count = 1;
        progress_batch.commands[0].property =
            m3e::appspec::PropertyKind::maximum;
        progress_batch.commands[0].integer_value = 100;
        assert(
            m3e::appspec::apply_ui_command_batch(
                progress_batch,
                progress_document).error ==
            m3e::appspec::CommandError::value_out_of_range);
        assert(progress_document.nodes[0].maximum == 200);
        assert(lv_bar_get_max_value(patched_progress) == 200);

        auto* patched_chart = lv_chart_create(component_root);
        lv_chart_set_point_count(patched_chart, 3);
        lv_chart_set_range(
            patched_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_add_series(
            patched_chart,
            lv_color_make(0x80, 0xC0, 0xFF),
            LV_CHART_AXIS_PRIMARY_Y);
        m3e::appspec::WireDocument chart_document;
        std::memcpy(chart_document.strings.data() + 1, "chart", 6);
        chart_document.string_bytes = 7;
        chart_document.node_count = 1;
        chart_document.nodes[0].id_offset = 1;
        chart_document.nodes[0].kind =
            m3e::appspec::ComponentKind::chart;
        chart_document.nodes[0].maximum = 100;
        chart_document.nodes[0].sample_count = 3;
        chart_document.nodes[0].samples = {10, 20, 30};
        chart_document.nodes[0].mounted_object = patched_chart;
        m3e::appspec::CommandBatch chart_batch;
        std::memcpy(chart_batch.strings.data() + 1, "chart", 6);
        chart_batch.string_bytes = 7;
        chart_batch.schema_version = 1;
        chart_batch.domain = m3e::appspec::CommandDomain::ui;
        chart_batch.command_count = 1;
        chart_batch.commands[0].kind =
            m3e::appspec::CommandKind::set_property;
        chart_batch.commands[0].property =
            m3e::appspec::PropertyKind::samples;
        chart_batch.commands[0].target_offset = 1;
        chart_batch.commands[0].samples = {15, 35, 70, 45};
        chart_batch.commands[0].sample_count = 4;
        assert(m3e::appspec::apply_ui_command_batch(
                   chart_batch, chart_document).ok());
        assert(chart_document.nodes[0].sample_count == 4);
        assert(chart_document.nodes[0].samples[2] == 70);
        assert(lv_chart_get_point_count(patched_chart) == 4);
        chart_batch.commands[0].samples[2] = 101;
        assert(
            m3e::appspec::apply_ui_command_batch(
                chart_batch, chart_document).error ==
            m3e::appspec::CommandError::value_out_of_range);
        assert(chart_document.nodes[0].samples[2] == 70);
        assert(lv_chart_get_point_count(patched_chart) == 4);

        auto* patched_icon = lv_image_create(component_root);
        lv_image_set_src(
            patched_icon,
            weather_icon_asset(
                generated::WeatherIcon::condition_clear_day,
                30));
        m3e::appspec::WireDocument icon_document;
        std::memcpy(
            icon_document.strings.data() + 1,
            "icon\0condition_clear_day",
            25);
        icon_document.string_bytes = 26;
        icon_document.node_count = 1;
        icon_document.nodes[0].id_offset = 1;
        icon_document.nodes[0].icon_offset = 6;
        icon_document.nodes[0].kind =
            m3e::appspec::ComponentKind::icon;
        icon_document.nodes[0].size = 1;
        icon_document.nodes[0].mounted_object = patched_icon;
        m3e::appspec::CommandBatch icon_batch;
        std::memcpy(
            icon_batch.strings.data() + 1,
            "icon\0condition_rain\0Rain\0rainy",
            31);
        icon_batch.string_bytes = 32;
        icon_batch.schema_version = 1;
        icon_batch.domain = m3e::appspec::CommandDomain::ui;
        icon_batch.command_count = 3;
        icon_batch.commands[0].kind =
            m3e::appspec::CommandKind::set_property;
        icon_batch.commands[0].property =
            m3e::appspec::PropertyKind::icon;
        icon_batch.commands[0].target_offset = 1;
        icon_batch.commands[0].text_offset = 6;
        icon_batch.commands[1].kind =
            m3e::appspec::CommandKind::set_property;
        icon_batch.commands[1].property =
            m3e::appspec::PropertyKind::semantic_label;
        icon_batch.commands[1].target_offset = 1;
        icon_batch.commands[1].text_offset = 21;
        icon_batch.commands[2].kind =
            m3e::appspec::CommandKind::set_property;
        icon_batch.commands[2].property =
            m3e::appspec::PropertyKind::semantic_value;
        icon_batch.commands[2].target_offset = 1;
        icon_batch.commands[2].text_offset = 26;
        assert(m3e::appspec::apply_ui_command_batch(
                   icon_batch, icon_document).ok());
        assert(std::strcmp(
                   icon_document.string_at(
                       icon_document.nodes[0].icon_offset),
                   "condition_rain") == 0);
        assert(std::strcmp(
                   icon_document.string_at(
                       icon_document.nodes[0].semantic_label_offset),
                   "Rain") == 0);
        assert(std::strcmp(
                   icon_document.string_at(
                       icon_document.nodes[0].semantic_value_offset),
                   "rainy") == 0);
        assert(lv_image_get_src(patched_icon) == weather_icon_asset(
            generated::WeatherIcon::condition_rain,
            30));

        auto* patched_pager = lv_obj_create(component_root);
        auto* first_page = lv_obj_create(patched_pager);
        auto* second_page = lv_obj_create(patched_pager);
        lv_obj_add_flag(second_page, LV_OBJ_FLAG_HIDDEN);
        m3e::appspec::WireDocument pager_document;
        std::memcpy(
            pager_document.strings.data() + 1,
            "pager\0page0\0page1",
            18);
        pager_document.string_bytes = 19;
        pager_document.node_count = 3;
        pager_document.nodes[0].id_offset = 1;
        pager_document.nodes[0].kind =
            m3e::appspec::ComponentKind::pager;
        pager_document.nodes[0].value = 0;
        pager_document.nodes[0].maximum = 2;
        pager_document.nodes[0].mounted_object = patched_pager;
        pager_document.nodes[1].id_offset = 7;
        pager_document.nodes[1].parent_index = 0;
        pager_document.nodes[1].mounted_object = first_page;
        pager_document.nodes[2].id_offset = 13;
        pager_document.nodes[2].parent_index = 0;
        pager_document.nodes[2].mounted_object = second_page;
        m3e::appspec::CommandBatch pager_batch;
        std::memcpy(pager_batch.strings.data() + 1, "pager", 6);
        pager_batch.string_bytes = 7;
        pager_batch.schema_version = 1;
        pager_batch.domain = m3e::appspec::CommandDomain::ui;
        pager_batch.command_count = 1;
        pager_batch.commands[0].kind =
            m3e::appspec::CommandKind::set_property;
        pager_batch.commands[0].property =
            m3e::appspec::PropertyKind::value;
        pager_batch.commands[0].target_offset = 1;
        pager_batch.commands[0].integer_value = 1;
        assert(m3e::appspec::apply_ui_command_batch(
                   pager_batch, pager_document).ok());
        assert(pager_document.nodes[0].value == 1);
        assert(lv_obj_has_flag(first_page, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(second_page, LV_OBJ_FLAG_HIDDEN));
        lv_obj_clean(component_root);

        m3e::appspec::WireDocument mounted_document;
        assert(m3e::appspec::decode_canonical_cbor(
                   hello_appspec_cbor.data(),
                   hello_appspec_cbor.size(),
                   mounted_document).ok());
        m3e::appspec::Renderer renderer(styles);
        assert(renderer.mount(
            lv_screen_active(), mounted_document));
        auto* original_object = static_cast<lv_obj_t*>(
            mounted_document.nodes[1].mounted_object);
        assert(original_object != nullptr);
        assert(std::strcmp(
                   lv_label_get_text(original_object),
                   "Hello world") == 0);

        constexpr std::array<std::uint8_t, 56> atomic_ui_batch_bytes{
            0xa2, 0x00, 0x01, 0x01, 0x82,
            0xa4, 0x00, 0x00, 0x01, 0x6b, 0x68, 0x65, 0x6c, 0x6c,
            0x6f, 0x2e, 0x74, 0x69, 0x74, 0x6c, 0x65, 0x02, 0x00,
            0x03, 0x6b, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x74,
            0x68, 0x65, 0x72, 0x65,
            0xa3, 0x00, 0x01, 0x01, 0x6c, 0x68, 0x65, 0x6c, 0x6c,
            0x6f, 0x2e, 0x61, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x03,
            0xf4,
        };
        m3e::appspec::CommandBatch atomic_failure;
        assert(m3e::appspec::decode_command_batch_canonical_cbor(
                   atomic_ui_batch_bytes.data(),
                   atomic_ui_batch_bytes.size(),
                   atomic_failure).ok());
        const auto rejected =
            m3e::appspec::apply_ui_command_batch(
                atomic_failure, mounted_document);
        assert(
            rejected.error ==
            m3e::appspec::CommandError::target_not_found);
        assert(std::strcmp(
                   lv_label_get_text(original_object),
                   "Hello world") == 0);

        std::array<std::uint8_t, 38> one_command{};
        std::memcpy(
            one_command.data(),
            atomic_ui_batch_bytes.data(),
            one_command.size());
        one_command[4] = 0x81;
        m3e::appspec::CommandBatch atomic_success;
        assert(m3e::appspec::decode_command_batch_canonical_cbor(
                   one_command.data(),
                   one_command.size(),
                   atomic_success).ok());
        assert(m3e::appspec::apply_ui_command_batch(
                   atomic_success, mounted_document).ok());
        assert(
            mounted_document.nodes[1].mounted_object ==
            original_object);
        assert(std::strcmp(
                   lv_label_get_text(original_object),
                   "Hello there") == 0);
        const auto compacted_string_bytes =
            mounted_document.string_bytes;
        auto* replacement_text =
            atomic_success.strings.data() +
            atomic_success.commands[0].text_offset;
        for (std::size_t update = 0; update < 1000; ++update) {
            std::memcpy(
                replacement_text,
                update % 2 == 0
                    ? "Hello world"
                    : "Hello there",
                12);
            assert(m3e::appspec::apply_ui_command_batch(
                       atomic_success,
                       mounted_document).ok());
            assert(
                mounted_document.string_bytes ==
                compacted_string_bytes);
        }
        assert(std::strcmp(
                   lv_label_get_text(original_object),
                   "Hello there") == 0);
        lv_display_delete(display);
    }
    lv_deinit();
    return 0;
}
