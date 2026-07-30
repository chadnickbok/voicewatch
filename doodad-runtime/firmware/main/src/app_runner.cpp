#include "app_runner.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "bh_platform.h"
#include "display.hpp"
#include "esp_log.h"
#include "wasm_export.h"

namespace {

constexpr char kTag[] = "doodad";
constexpr std::uint32_t kWasmStackBytes = 16 * 1024;
constexpr std::uint32_t kWasmHeapBytes = 16 * 1024;
constexpr std::uint32_t kExecEnvStackBytes = 8 * 1024;
constexpr std::uint32_t kMaximumTextBytes = 128;

bool g_runtime_ready = false;
bool g_display_text_called = false;
bool g_invalid_guest_string = false;

bool valid_utf8(const std::uint8_t* bytes, std::size_t size) {
    std::size_t index = 0;
    while (index < size) {
        const std::uint8_t first = bytes[index++];
        if (first <= 0x7f) {
            continue;
        }

        std::uint32_t code_point = 0;
        std::size_t trailing = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            code_point = first & 0x1f;
            trailing = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            code_point = first & 0x0f;
            trailing = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            code_point = first & 0x07;
            trailing = 3;
        } else {
            return false;
        }

        if (index + trailing > size) {
            return false;
        }
        for (std::size_t offset = 0; offset < trailing; ++offset) {
            const std::uint8_t next = bytes[index++];
            if ((next & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (next & 0x3f);
        }

        if ((trailing == 2 && code_point < 0x800)
            || (trailing == 3 && code_point < 0x10000)
            || (code_point >= 0xd800 && code_point <= 0xdfff)
            || code_point > 0x10ffff) {
            return false;
        }
    }
    return true;
}

void reject_guest_string(wasm_module_inst_t module_instance, const char* reason) {
    ESP_LOGE(kTag, "[host] invalid guest string: %s", reason);
    g_invalid_guest_string = true;
    display_error("INVALID GUEST STRING");
    wasm_runtime_set_exception(module_instance, "INVALID GUEST STRING");
}

void host_display_text(wasm_exec_env_t execution_environment,
                       std::uint32_t guest_pointer,
                       std::uint32_t guest_length) {
    wasm_module_inst_t module_instance =
        wasm_runtime_get_module_inst(execution_environment);

    if (guest_length == 0 || guest_length > kMaximumTextBytes) {
        reject_guest_string(module_instance, "length outside 1..128 bytes");
        return;
    }
    if (!wasm_runtime_validate_app_addr(module_instance, guest_pointer, guest_length)) {
        reject_guest_string(module_instance, "pointer outside guest linear memory");
        return;
    }

    const auto* guest_bytes = static_cast<const std::uint8_t*>(
        wasm_runtime_addr_app_to_native(module_instance, guest_pointer));
    if (guest_bytes == nullptr || !valid_utf8(guest_bytes, guest_length)) {
        reject_guest_string(module_instance, "text is not valid UTF-8");
        return;
    }

    char host_copy[kMaximumTextBytes + 1]{};
    std::memcpy(host_copy, guest_bytes, guest_length);
    ESP_LOGI(kTag, "[guest] display_text: %u bytes",
             static_cast<unsigned>(guest_length));
    display_guest_text(host_copy, guest_length);
    g_display_text_called = true;
}

NativeSymbol g_native_symbols[] = {
    {
        .symbol = "display_text",
        .func_ptr = reinterpret_cast<void*>(host_display_text),
        .signature = "(ii)",
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

    std::unique_ptr<std::uint8_t, decltype(&std::free)> mutable_module(
        static_cast<std::uint8_t*>(std::malloc(image.size)), &std::free);
    if (mutable_module == nullptr) {
        ESP_LOGE(kTag, "[host] module buffer allocation failed: %u bytes",
                 static_cast<unsigned>(image.size));
        display_error("MODULE LOAD FAILED");
        return false;
    }
    std::memcpy(mutable_module.get(), image.data, image.size);

    wasm_module_t module = wasm_runtime_load(
        mutable_module.get(),
        static_cast<std::uint32_t>(image.size),
        error_buffer,
        sizeof(error_buffer));
    if (module == nullptr) {
        ESP_LOGE(kTag, "[host] module load failed: %s", error_buffer);
        display_error("MODULE LOAD FAILED");
        return false;
    }
    ESP_LOGI(kTag, "[host] module loaded");

    wasm_module_inst_t instance = wasm_runtime_instantiate(
        module, kWasmStackBytes, kWasmHeapBytes, error_buffer, sizeof(error_buffer));
    if (instance == nullptr) {
        ESP_LOGE(kTag, "[host] module instantiate failed: %s", error_buffer);
        display_error("MODULE INSTANTIATE FAILED");
        wasm_runtime_unload(module);
        return false;
    }
    ESP_LOGI(kTag, "[host] module instantiated");

    wasm_function_inst_t app_start =
        wasm_runtime_lookup_function(instance, "app_start");
    if (app_start == nullptr) {
        ESP_LOGE(kTag, "[host] app_start export not found");
        display_error("APP_START NOT FOUND");
        wasm_runtime_deinstantiate(instance);
        wasm_runtime_unload(module);
        return false;
    }

    wasm_exec_env_t execution_environment =
        wasm_runtime_create_exec_env(instance, kExecEnvStackBytes);
    if (execution_environment == nullptr) {
        ESP_LOGE(kTag, "[host] execution environment allocation failed");
        display_error("MODULE INSTANTIATE FAILED");
        wasm_runtime_deinstantiate(instance);
        wasm_runtime_unload(module);
        return false;
    }

    display_shell("WASM RUNNING", image.source);
    ESP_LOGI(kTag, "[host] invoking app_start");
    g_display_text_called = false;
    g_invalid_guest_string = false;
    const bool call_succeeded =
        wasm_runtime_call_wasm(execution_environment, app_start, 0, nullptr);

    bool succeeded = call_succeeded && g_display_text_called;
    if (!call_succeeded) {
        log_exception(instance);
        if (!g_invalid_guest_string) {
            display_error("GUEST TRAP");
        }
    } else if (!g_display_text_called) {
        ESP_LOGE(kTag, "[host] app returned without calling display_text");
        display_error("GUEST TRAP");
        succeeded = false;
    } else {
        ESP_LOGI(kTag, "[host] app completed successfully");
    }

    wasm_runtime_destroy_exec_env(execution_environment);
    wasm_runtime_deinstantiate(instance);
    wasm_runtime_unload(module);
    return succeeded;
}
