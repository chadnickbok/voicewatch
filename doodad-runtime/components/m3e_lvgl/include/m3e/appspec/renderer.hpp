#pragma once

#include "lvgl.h"
#include "m3e/appspec/wire.hpp"
#include "m3e/theme/style_registry.hpp"

namespace m3e::appspec {

class Renderer {
 public:
    explicit Renderer(StyleRegistry& styles);
    bool mount(
        lv_obj_t* root,
        WireDocument& document,
        WireEventSink event_sink = nullptr,
        void* event_context = nullptr);

 private:
    StyleRegistry& styles_;
};

}  // namespace m3e::appspec
