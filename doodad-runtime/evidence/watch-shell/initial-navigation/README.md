# Initial watch navigation flow

This evidence records the first-pass trusted-shell sequence at the production
240×240 RGB565 surface:

1. Watch face
2. Launcher
3. Timer Wasm application
4. Back to launcher
5. Home to watch face

The Watch Face and seeded Launcher states use the production native Material
catalog renderer. The Timer frame mounts the real `dev.doodad.timer` package
through WAMR and AppSpec. Firmware uses the same Watch Face renderer and a
dynamic installed-package launcher with the matching row treatment.

Regenerate the individual PNGs and contact sheet from `doodad-runtime`:

```bash
./tools/capture_watch_navigation_flow.py
```

The capture command also proves that Back restores the launcher pixels and
Home restores the watch-face pixels exactly. Route behavior is independently
covered by `tests/m3e/os_shell_test.cpp`. These are simulator captures; a
physical-watch photograph remains a separate hardware gate.
