# Architecture decisions

Changes to a locked decision require measurements or a concrete compatibility
reason. Unmeasured targets are explicitly labeled as targets.

## ADR-001: Material reference

- **Status:** accepted
- **Decision:** Wear Compose Material 3 Expressive 1.6.2 at AndroidX commit
  `f65727cc5cc63d05724c0edb55900bc8790b14e8`.
- **Consequence:** 1.7 alpha APIs and visuals cannot enter the stable component
  contract without a deliberate baseline upgrade.

## ADR-002: Display model

- **Status:** accepted
- **Decision:** apps lay out a 192×192dp square mapped to 240×240 physical
  pixels with Q8.8 density 1.25. CoreS3 centers that viewport in 320×240.
- **Consequence:** the 40px side rails are host-owned and cannot affect app
  measurement.

## ADR-003: Native UI implementation

- **Status:** accepted
- **Decision:** `m3e_lvgl` is a narrow C++17 layer over LVGL 9.5.0, built
  without exceptions or RTTI.
- **Consequence:** it may provide ownership and typed semantic APIs but must not
  become an independent widget tree.

## ADR-004: Generated-app boundary

- **Status:** accepted
- **Decision:** authoring uses versioned JSON AppSpec; canonical CBOR is the
  eventual signed device format. Public nodes are semantic components only.
- **Consequence:** no raw pixels, LVGL objects, callbacks, style dictionaries,
  arbitrary colors, animation curves, or canvas escape hatch.

## ADR-005: Guest execution

- **Status:** accepted
- **Decision:** one Rust/Wasm artifact runs under the pinned WAMR source on
  desktop and device.
- **Consequence:** the browser simulator remains a framebuffer viewer rather
  than a second Wasm runtime.

## ADR-006: UI thread ownership

- **Status:** accepted; migration incomplete
- **Decision:** exactly one UI task mutates LVGL. Audio, network, storage,
  sensors, and WAMR communicate with it through bounded queues.
- **Consequence:** the current synchronous guest-to-display firmware call is
  temporary and must be replaced by message delivery before interactive apps.

## ADR-007: ESP display integration

- **Status:** accepted
- **Decision:** pin `esp_lvgl_port` 2.8.0 revision 1 and converge hardware BSPs
  on its task, lock, sleep, input, and asynchronous flush model.
- **Consequence:** the current M5Unified/M5GFX flush bridge remains permitted
  only for CoreS3 bring-up and measurements.

## ADR-008: Theme generation

- **Status:** accepted
- **Decision:** Material Color Utilities and contrast validation run on the
  build host/server. The device receives a complete bounded resolved theme.
- **Consequence:** HCT generation and arbitrary raw app colors are excluded
  from the frame loop and public AppSpec.

## ADR-009: Fonts and icons

- **Status:** accepted
- **Decision:** production uses pinned, subsetted LVGL bitmap fonts generated
  from static Roboto Flex instances and a curated Material Symbols corpus.
- **Consequence:** runtime variable fonts and unrestricted icon assets are not
  foundational dependencies.

## ADR-010: Rendering budgets

- **Status:** accepted as unmeasured gates
- **Decision:** target 30fps for full-screen motion and 60fps for small dirty
  regions; RGB565 partial rendering is the default.
- **Consequence:** results must be labeled unmeasured until captured on each
  board. Missing a gate triggers the documented degradation sequence, not a
  silent fidelity claim.

## ADR-011: System ownership

- **Status:** accepted
- **Decision:** Home, voice, permission, installation, crash/recovery, and
  critical-alert layers remain native and cannot be drawn or intercepted by an
  app.
- **Consequence:** generated apps can request semantic navigation and system
  actions but cannot impersonate trusted surfaces.
