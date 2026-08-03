# Sports

Deterministic interactive conformance package with five title-free,
full-screen baseball states:

- a glanceable live game
- a followed game with inning, outs, and runners
- a coalesced two-run score update
- a final score and ended follow activity
- the decisive scoring play

The app uses the shared `live_action_detail` structure: game context, one
dominant score, one compact live card, and two 48dp actions. The structure is
selected independently by real Wear Compose and production LVGL; neither
renderer consults the Sports app ID.

Every transition is emitted by the real Rust/Wasm guest and crosses the
domain-scoped mocked sports capability before mounting the next bounded
AppSpec.
