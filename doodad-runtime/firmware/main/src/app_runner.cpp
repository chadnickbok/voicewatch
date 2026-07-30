#include "app_runner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include "bh_platform.h"
#include "display.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "m3e/appspec/command_batch.hpp"
#include "m3e/appspec/wire.hpp"
#include "m3e/state/store.hpp"
#include "wasm_export.h"

namespace {

constexpr char kTag[] = "doodad";
constexpr std::uint32_t kWasmStackBytes = 16 * 1024;
constexpr std::uint32_t kWasmHeapBytes = 16 * 1024;
constexpr std::uint32_t kExecEnvStackBytes = 8 * 1024;
constexpr std::size_t kEventQueueDepth = 16;

struct GuestEvent {
    std::uint8_t schema;
    std::array<char, 65> app_id;
    std::array<char, 65> screen_id;
    std::array<char, 65> node_id;
    std::array<char, 65> action_id;
m3e::appspec::EventKind kind;
    std::uint64_t timestamp_monotonic_ms;
};

bool g_runtime_ready = false;
bool g_semantic_mount_called = false;
bool g_invalid_guest_message = false;
std::uint8_t* g_module_bytes = nullptr;
wasm_module_t g_module = nullptr;
wasm_module_inst_t g_instance = nullptr;
wasm_exec_env_t g_execution_environment = nullptr;
wasm_function_inst_t g_handle_event = nullptr;
QueueHandle_t g_event_queue = nullptr;
m3e::state::Store* g_app_store = nullptr;

void log_exception(wasm_module_inst_t module_instance);

bool copy_identifier(
    std::array<char, 65>& destination,
    const char* source) {
    if (source == nullptr) return false;
    const auto length = std::strlen(source);
    if (length == 0 || length >= destination.size()) return false;
    std::memcpy(destination.data(), source, length + 1);
    return true;
}

void release_app() {
    g_handle_event = nullptr;
    if (g_execution_environment != nullptr) {
        wasm_runtime_destroy_exec_env(g_execution_environment);
        g_execution_environment = nullptr;
    }
    if (g_instance != nullptr) {
        wasm_runtime_deinstantiate(g_instance);
        g_instance = nullptr;
    }
    if (g_module != nullptr) {
        wasm_runtime_unload(g_module);
        g_module = nullptr;
    }
    std::free(g_module_bytes);
    g_module_bytes = nullptr;
    delete g_app_store;
    g_app_store = nullptr;
}

bool dispatch_event(const GuestEvent& event) {
    if (g_instance == nullptr ||
        g_execution_environment == nullptr ||
        g_handle_event == nullptr) {
        ESP_LOGW(kTag, "[host] guest has no handle_event export");
        return false;
    }
    const m3e::appspec::UiEvent envelope{
        event.schema,
        event.app_id.data(),
        event.screen_id.data(),
        event.node_id.data(),
        event.action_id.data(),
        event.kind,
        event.timestamp_monotonic_ms,
    };
    std::array<std::uint8_t, 512> encoded{};
    const auto encoded_size =
        m3e::appspec::encode_event_canonical_cbor(
            envelope, encoded.data(), encoded.size());
    if (encoded_size == 0) {
        ESP_LOGE(kTag, "[host] failed to encode semantic UI event");
        return false;
    }
    void* guest_native = nullptr;
    const auto guest_pointer = wasm_runtime_module_malloc(
        g_instance, encoded_size, &guest_native);
    if (guest_pointer == 0 || guest_native == nullptr ||
        guest_pointer > std::numeric_limits<std::uint32_t>::max()) {
        ESP_LOGE(kTag, "[host] guest event allocation failed");
        return false;
    }
    std::memcpy(guest_native, encoded.data(), encoded_size);
    std::uint32_t arguments[2]{
        static_cast<std::uint32_t>(guest_pointer),
        static_cast<std::uint32_t>(encoded_size),
    };
    const bool called = wasm_runtime_call_wasm(
        g_execution_environment, g_handle_event, 2, arguments);
    wasm_runtime_module_free(g_instance, guest_pointer);
    if (!called) {
        log_exception(g_instance);
        ESP_LOGE(kTag, "[host] guest handle_event trapped");
        return false;
    }
    std::uint64_t packed_result = 0;
    std::memcpy(&packed_result, arguments, sizeof(packed_result));
    const auto result_pointer =
        static_cast<std::uint32_t>(packed_result >> 32U);
    const auto result_length =
        static_cast<std::uint32_t>(packed_result);
    if (result_pointer == 0 || result_length == 0 ||
        result_length > m3e::appspec::kMaximumCommandBatchBytes) {
        ESP_LOGE(kTag, "[host] invalid guest CommandBatch slice");
        return false;
    }
    if (!wasm_runtime_validate_app_addr(
            g_instance, result_pointer, result_length)) {
        ESP_LOGE(kTag, "[host] CommandBatch pointer outside guest memory");
        return false;
    }
    const auto* result_bytes = static_cast<const std::uint8_t*>(
        wasm_runtime_addr_app_to_native(
            g_instance, result_pointer));
    if (result_bytes == nullptr) {
        ESP_LOGE(kTag, "[host] CommandBatch address translation failed");
        return false;
    }
    auto* batch =
        new (std::nothrow) m3e::appspec::CommandBatch{};
    if (batch == nullptr) {
        ESP_LOGE(kTag, "[host] CommandBatch allocation failed");
        return false;
    }
    const auto decoded =
        m3e::appspec::decode_command_batch_canonical_cbor(
            result_bytes, result_length, *batch);
    if (!decoded.ok()) {
        ESP_LOGE(
            kTag,
            "[host] CommandBatch rejected: %s byte=%u command=%u",
            m3e::appspec::command_error_name(decoded.error),
            static_cast<unsigned>(decoded.byte_offset),
            static_cast<unsigned>(decoded.command_index));
        delete batch;
        return false;
    }
    const auto command_count = batch->command_count;
    if (batch->domain == m3e::appspec::CommandDomain::ui) {
        if (!display_apply_command_batch(batch)) {
            delete batch;
            ESP_LOGE(kTag, "[host] UI queue rejected CommandBatch");
            return false;
        }
    } else {
        if (g_app_store == nullptr) {
            g_app_store =
                new (std::nothrow) m3e::state::Store{};
            if (g_app_store == nullptr) {
                delete batch;
                ESP_LOGE(kTag, "[host] state Store allocation failed");
                return false;
            }
        }
        const auto applied =
            m3e::appspec::apply_state_command_batch(
                *batch, *g_app_store);
        delete batch;
        if (!applied.ok()) {
            ESP_LOGE(
                kTag,
                "[host] state CommandBatch rejected: %s command=%u",
                m3e::appspec::command_error_name(applied.error),
                static_cast<unsigned>(applied.command_index));
            return false;
        }
    }
    ESP_LOGI(
        kTag,
        "[host] delivered action=%s node=%s commands=%u",
        event.action_id.data(),
        event.node_id.data(),
        static_cast<unsigned>(command_count));
    return true;
}

std::int32_t reject_guest_appspec(
    wasm_module_inst_t module_instance,
    const char* reason) {
    ESP_LOGE(kTag, "[host] invalid guest AppSpec: %s", reason);
    g_invalid_guest_message = true;
    display_error("INVALID APPSPEC");
    wasm_runtime_set_exception(module_instance, "INVALID APPSPEC");
    return 0;
}

std::int32_t host_ui_mount(
    wasm_exec_env_t execution_environment,
    std::uint32_t guest_pointer,
    std::uint32_t guest_length) {
    wasm_module_inst_t module_instance =
        wasm_runtime_get_module_inst(execution_environment);

    if (guest_length == 0 ||
        guest_length > m3e::appspec::kMaximumWireBytes) {
        return reject_guest_appspec(
            module_instance, "length outside 1..4096 bytes");
    }
    if (!wasm_runtime_validate_app_addr(
            module_instance, guest_pointer, guest_length)) {
        return reject_guest_appspec(
            module_instance, "pointer outside guest linear memory");
    }

    const auto* guest_bytes = static_cast<const std::uint8_t*>(
        wasm_runtime_addr_app_to_native(module_instance, guest_pointer));
    if (guest_bytes == nullptr) {
        return reject_guest_appspec(
            module_instance, "guest address translation failed");
    }
    auto* document =
        new (std::nothrow) m3e::appspec::WireDocument{};
    if (document == nullptr) {
        return reject_guest_appspec(
            module_instance, "host document allocation failed");
    }
    const auto decoded = m3e::appspec::decode_canonical_cbor(
        guest_bytes, guest_length, *document);
    if (!decoded.ok()) {
        ESP_LOGE(
            kTag,
            "[host] AppSpec decode failed: %s byte=%u node=%u",
            m3e::appspec::wire_error_name(decoded.error),
            static_cast<unsigned>(decoded.byte_offset),
            static_cast<unsigned>(decoded.node_index));
        delete document;
        return reject_guest_appspec(
            module_instance, "canonical CBOR validation failed");
    }
    const auto node_count = document->node_count;
    if (!display_mount_appspec(document)) {
        delete document;
        return reject_guest_appspec(
            module_instance, "UI command queue rejected mount");
    }
    ESP_LOGI(
        kTag,
        "[guest] ui_mount: %u bytes, %u nodes",
        static_cast<unsigned>(guest_length),
        static_cast<unsigned>(node_count));
    g_semantic_mount_called = true;
    return 1;
}

NativeSymbol g_native_symbols[] = {
    {
        .symbol = "ui_mount",
        .func_ptr = reinterpret_cast<void*>(host_ui_mount),
        .signature = "(ii)i",
        .attachment = nullptr,
    },
};

void log_exception(wasm_module_inst_t module_instance) {
    const char* exception = wasm_runtime_get_exception(module_instance);
    if (exception != nullptr) {
        ESP_LOGE(kTag, "[host] WAMR exception: %s", exception);
    }
}

}  // namespace

bool app_runtime_init() {
    g_event_queue = xQueueCreate(kEventQueueDepth, sizeof(GuestEvent));
    if (g_event_queue == nullptr) {
        ESP_LOGE(kTag, "[host] semantic event queue allocation failed");
        display_error("WAMR INIT FAILED");
        return false;
    }
    RuntimeInitArgs arguments{};
    arguments.mem_alloc_type = Alloc_With_Allocator;
    arguments.mem_alloc_option.allocator.malloc_func =
        reinterpret_cast<void*>(os_malloc);
    arguments.mem_alloc_option.allocator.realloc_func =
        reinterpret_cast<void*>(os_realloc);
    arguments.mem_alloc_option.allocator.free_func =
        reinterpret_cast<void*>(os_free);

    if (!wasm_runtime_full_init(&arguments)) {
        ESP_LOGE(kTag, "[host] WAMR init failed");
        display_error("WAMR INIT FAILED");
        return false;
    }
    if (!wasm_runtime_register_natives(
            "doodad", g_native_symbols,
            sizeof(g_native_symbols) / sizeof(g_native_symbols[0]))) {
        ESP_LOGE(kTag, "[host] native ABI registration failed");
        display_error("WAMR INIT FAILED");
        wasm_runtime_destroy();
        return false;
    }

    g_runtime_ready = true;
    ESP_LOGI(kTag, "[host] WAMR ready (interpreter, stack=%u, heap=%u)",
             static_cast<unsigned>(kWasmStackBytes),
             static_cast<unsigned>(kWasmHeapBytes));
    return true;
}

bool run_app(const AppImage& image) {
    if (!g_runtime_ready || image.data == nullptr || image.size == 0) {
        ESP_LOGE(kTag, "[host] invalid app image");
        display_error("MODULE LOAD FAILED");
        return false;
    }

    char error_buffer[192]{};
    ESP_LOGI(kTag, "[host] %s app size: %u bytes", image.source,
             static_cast<unsigned>(image.size));

    release_app();
    g_module_bytes = static_cast<std::uint8_t*>(std::malloc(image.size));
    if (g_module_bytes == nullptr) {
        ESP_LOGE(kTag, "[host] module buffer allocation failed: %u bytes",
                 static_cast<unsigned>(image.size));
        display_error("MODULE LOAD FAILED");
        return false;
    }
    std::memcpy(g_module_bytes, image.data, image.size);

    g_module = wasm_runtime_load(
        g_module_bytes,
        static_cast<std::uint32_t>(image.size),
        error_buffer,
        sizeof(error_buffer));
    if (g_module == nullptr) {
        ESP_LOGE(kTag, "[host] module load failed: %s", error_buffer);
        display_error("MODULE LOAD FAILED");
        release_app();
        return false;
    }
    ESP_LOGI(kTag, "[host] module loaded");

    g_instance = wasm_runtime_instantiate(
        g_module,
        kWasmStackBytes,
        kWasmHeapBytes,
        error_buffer,
        sizeof(error_buffer));
    if (g_instance == nullptr) {
        ESP_LOGE(kTag, "[host] module instantiate failed: %s", error_buffer);
        display_error("MODULE INSTANTIATE FAILED");
        release_app();
        return false;
    }
    ESP_LOGI(kTag, "[host] module instantiated");

    wasm_function_inst_t app_start =
        wasm_runtime_lookup_function(g_instance, "app_start");
    if (app_start == nullptr) {
        ESP_LOGE(kTag, "[host] app_start export not found");
        display_error("APP_START NOT FOUND");
        release_app();
        return false;
    }

    g_execution_environment =
        wasm_runtime_create_exec_env(g_instance, kExecEnvStackBytes);
    if (g_execution_environment == nullptr) {
        ESP_LOGE(kTag, "[host] execution environment allocation failed");
        display_error("MODULE INSTANTIATE FAILED");
        release_app();
        return false;
    }
    g_handle_event =
        wasm_runtime_lookup_function(g_instance, "handle_event");
    xQueueReset(g_event_queue);

    display_shell("WASM RUNNING", image.source);
    ESP_LOGI(kTag, "[host] invoking app_start");
    g_semantic_mount_called = false;
    g_invalid_guest_message = false;
    const bool call_succeeded =
        wasm_runtime_call_wasm(g_execution_environment, app_start, 0, nullptr);

    bool succeeded = call_succeeded && g_semantic_mount_called;
    if (!call_succeeded) {
        log_exception(g_instance);
        if (!g_invalid_guest_message) {
            display_error("GUEST TRAP");
        }
    } else if (!g_semantic_mount_called) {
        ESP_LOGE(kTag, "[host] app returned without mounting AppSpec");
        display_error("GUEST TRAP");
        succeeded = false;
    } else {
        ESP_LOGI(kTag, "[host] app started; instance remains resident");
    }

    if (!succeeded) {
        release_app();
    }
    return succeeded;
}

bool app_post_ui_event(const m3e::appspec::UiEvent& event) {
    if (g_event_queue == nullptr ||
        !m3e::appspec::event_is_valid(event)) {
        return false;
    }
    GuestEvent copy{};
    copy.schema = event.schema;
    copy.kind = event.kind;
    copy.timestamp_monotonic_ms = event.timestamp_monotonic_ms;
    if (!copy_identifier(copy.app_id, event.app_id) ||
        !copy_identifier(copy.screen_id, event.screen_id) ||
        !copy_identifier(copy.node_id, event.node_id) ||
        !copy_identifier(copy.action_id, event.action_id)) {
        return false;
    }
    if (xQueueSend(g_event_queue, &copy, 0) != pdTRUE) {
        ESP_LOGE(kTag, "[host] semantic event queue overflow");
        return false;
    }
    return true;
}

void app_runtime_update(std::uint32_t maximum_wait_ms) {
    if (g_event_queue == nullptr) return;
    GuestEvent event{};
    if (xQueueReceive(
            g_event_queue,
            &event,
            pdMS_TO_TICKS(maximum_wait_ms)) != pdTRUE) {
        return;
    }
    dispatch_event(event);
    while (xQueueReceive(g_event_queue, &event, 0) == pdTRUE) {
        dispatch_event(event);
    }
}
