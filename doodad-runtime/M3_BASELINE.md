# Material 3 Expressive baseline

This repository implements Material 3 Expressive for Wear against one frozen
reference. Moving any pin below requires an architecture-decision update,
regenerated artifacts, visual comparison, and measured compatibility evidence.

## Reference oracle

| Item | Frozen value |
|---|---|
| Maven artifact | `androidx.wear.compose:compose-material3:1.6.2` |
| AndroidX source commit | `f65727cc5cc63d05724c0edb55900bc8790b14e8` |
| Source subtree | `wear/compose/compose-material3` |
| Oracle density | 200 dpi: 192dp maps to 240px |
| Product profile | square 192dp / 240px |
| Fidelity comparison | deterministic Android story versus RGB565 LVGL story |

Wear Compose 1.7 is tracked separately and does not define v1 behavior.

## Runtime baseline

| Dependency | Frozen value | Evidence |
|---|---|---|
| ESP-IDF | `5.5.5` | `firmware/main/idf_component.yml` |
| LVGL | `9.5.0` | `firmware/dependencies.lock` |
| esp_lvgl_port | `2.8.0~1` | `firmware/dependencies.lock` |
| WAMR | `2.4.0~1` | `firmware/dependencies.lock` |
| M5Unified | `0.2.19` | `firmware/dependencies.lock` |
| M5GFX | `0.2.26` | resolved lock entry |
| Rust | `1.95.0` | `rust-toolchain.toml` |
| Host UI dialect | C++17, exceptions and RTTI disabled |
| Hardware pixels | RGB565 |

The desktop simulator compiles the exact managed LVGL and WAMR sources used by
firmware. A browser may display a captured framebuffer, but it is not a second
Wasm or widget runtime.

## Source locations

- Stable source:
  `https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/`
- Tokens:
  `https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/src/main/java/androidx/wear/compose/material3/tokens/`
- Samples:
  `https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/samples/`
- Screenshot tests:
  `https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/src/androidTest/`

## Current migration state

The existing `ui-v0` simulator proves the package/WAMR/LVGL seam. It is not
the final AppSpec:

- current layout values are physical pixels rather than logical dp;
- the component vocabulary is only stack/text/button/progress;
- firmware flushes synchronously through M5GFX;
- guest display calls currently originate on the runtime thread;
- no Android oracle, token generator, semantic tree, or keyed reconciler exists.

These are tracked gaps, not accepted deviations from the implementation plan.
