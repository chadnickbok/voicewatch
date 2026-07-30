# Doodad simulator and package slice

## The development loop

Run this from `doodad-runtime`:

```bash
./doodad dev apps/hello
```

The command builds the Rust guest, stages a package, checks the package against
the versioned contracts, starts `app_start` in WAMR, validates the guest's
canonical CBOR AppSpec through the same native decoder used on-device, and
serves the resulting 240×240 RGB565 LVGL framebuffer to a local browser page.

The browser is a viewer and inspector, not the application runtime. WebAssembly
and LVGL both execute in the native host process. That keeps runtime behavior
aligned with firmware and avoids a browser-only Wasm tier.

On source changes, the command completes build and static validation before
stopping the current instance, then starts one fresh instance. A runtime
failure leaves no guest running, but the browser retains the last good frame
and marks it as a stale preview.

## Package v1

An app source directory contains:

```text
apps/example/
├── Cargo.toml
├── manifest.json
├── appspec.json
└── src/
```

`doodad build` produces:

```text
target/doodad/dev.example.app/
├── manifest.json
└── app.wasm
```

The authoritative definitions are:

- `contracts/manifest-v1.schema.json`
- `contracts/abi/v1.json`
- `contracts/appspec-v1.schema.json`
- `contracts/appspec-v1.cddl`

Manifest v1 is intentionally narrow. It has an app ID, display name, semantic
version, required host ABI, a capability list, and fixed package filenames.
Unknown fields fail closed so an accidentally misspelled policy field is never
silently ignored.

## Semantic AppSpec v1

The public vocabulary is the semantic component set documented in
`docs/appspec-v1.md`. JSON is for authoring; canonical CBOR with stable numeric
tags is the package/device form. The device boundary allows at most 250 nodes,
12 levels, 32 children per container, one primary scroll axis, 128 events, and
4096 wire bytes. No node can supply colors, fonts, pixel coordinates,
arbitrary LVGL properties, native callbacks, or expressions.

## Display invariant

All application and shell design happens on a 240×240 logical display.

```text
Simulator:  ┌──────────── 240 ────────────┐
            │                             │ 240
            └─────────────────────────────┘

CoreS3:     40 px  ┌────── 240 ──────┐  40 px
            gutter │  app + shell UI  │  gutter
                   └──────────────────┘
```

The physical gutters are deliberately absent from the package contract. This
prevents simulator work from drifting into 320×240 widescreen layouts that
will not fit the eventual watch.

## Scope boundary and next seam

This slice establishes one foreground package on a resident native host. It
does not yet load the complete package on the device; the CoreS3 loader still
selects a bare embedded or microSD Wasm module.

The next architecture pass should specify the always-available system/voice
plane: audio streaming, system status overlays, navigation actions, app
replacement, and typed delivery of app events. That should extend the resident
host lifecycle rather than grant audio, networking, or raw navigation control
to an app.
