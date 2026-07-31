# Remote Control

A title-free camera remote that uses a real package-loaded viewfinder across
Wear Compose and LVGL.

Screens:

- connected camera preview
- three-second shutter control
- active countdown
- captured confirmation
- disconnected preview with recovery

Every transition crosses the domain-scoped mocked remote capability before
mounting the next bounded AppSpec. The viewfinder is an original generated
photo fixture, reduced deterministically to a 230×150 RGB565 asset and
addressed by its package hash. It proves photo loading without adding a
network dependency or shipping any of the third-party research images.
