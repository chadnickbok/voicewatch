# Smart Home

Deterministic interactive conformance package with five title-free,
full-screen home-control states:

- a 72% living-room favorite
- a detailed light control
- a rejected turn-off command that visibly rolls back
- an explicit trusted review before unlocking the front door
- an acknowledged unlocked state with an immediate relock action

The app uses the shared `live_action_detail` structure: one home/device context
line, one dominant state, one compact live card, and two 48dp actions. The
structure is selected independently by real Wear Compose and production LVGL;
neither renderer consults the Smart Home app ID.

Every transition is emitted by the real Rust/Wasm guest and crosses the
domain-scoped mocked home capability before mounting the next bounded AppSpec.
