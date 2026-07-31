# Third-party notices and provenance

This file records intended and current upstream inputs. Bundled/generated
assets must retain their individual notices and exact revision metadata.

## AndroidX Wear Compose Material 3

- Version: 1.6.2
- Commit: `f65727cc5cc63d05724c0edb55900bc8790b14e8`
- License: Apache License 2.0
- Use: behavioral reference, token source, samples, screenshot oracle
- Source: `https://android.googlesource.com/platform/frameworks/support/`

No AndroidX runtime code executes on the watch. Adapted copyrightable source or
generated constants must carry the required Apache notice.

The Android reference lane adapts the test-device and screenshot-test structure
from Google's `android/wear-os-samples` ComposeStarter project. The adapted
source files retain Android Open Source Project copyright and Apache-2.0
notices.

## Roborazzi

- Version: 1.59.0
- License: Apache License 2.0
- Use: host-side Compose screenshot recording and comparison
- Source: `https://github.com/takahirom/roborazzi`

## Material Color Utilities

- License: Apache License 2.0
- Planned use: host/server theme generation and contrast validation
- Source: `https://github.com/material-foundation/material-color-utilities`

## Material Symbols

- License: Apache License 2.0
- Planned use: curated generated icon subset
- Source: `https://github.com/google/material-design-icons`

## Roboto Flex

- License: SIL Open Font License 1.1
- Planned use: static, subsetted LVGL bitmap-font instances
- Source: `https://github.com/googlefonts/roboto-flex`

## LVGL

- Version: 9.5.0
- License: MIT
- Use: native UI and renderer
- Source: `https://github.com/lvgl/lvgl`

## Espressif esp_lvgl_port

- Version: 2.8.0 revision 1
- License: Apache License 2.0
- Use: planned ESP display/input/task integration
- Source: `https://github.com/espressif/esp-bsp`

## WebAssembly Micro Runtime

- Component version: 2.4.0 revision 1
- License: Apache License 2.0 with documented third-party subcomponents
- Use: guest runtime on desktop and ESP32-S3
- Source is resolved by the Espressif Component Registry.

## M5Unified and M5GFX

- Versions: M5Unified 0.2.19; M5GFX 0.2.26
- License: MIT
- Use: current CoreS3 board and display bridge
- Source: `https://github.com/m5stack/`

“Material” and related names identify design compatibility targets and do not
imply Google endorsement.
