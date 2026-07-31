# Transit Departures

Deterministic interactive conformance package with five title-free,
full-screen transit states:

- Castro northbound arrival at 3 minutes
- complete N and L departure context
- an 18-minute-old cached schedule
- a recovered live schedule that preserves the station selection
- a six-minute N-line service delay

The app uses the shared `live_action_detail` structure: one route/station
context line, one dominant arrival metric, one compact live card, and two
48dp actions. The structure is selected independently by real Wear Compose and
production LVGL; neither renderer consults the Transit app ID.

Every transition is emitted by the real Rust/Wasm guest and crosses the
domain-scoped mocked transit capability before mounting the next bounded
AppSpec.
