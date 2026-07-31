# Navigation

Deterministic, title-free walking-navigation conformance package for the
square 240×240 profile.

The real Wasm flow mounts renderer-neutral AppSpec states for route preview,
turn guidance, route overview, cached guidance after GPS loss, and monotonic
route recovery. Every transition crosses the domain-scoped mocked navigation
capability before mounting the next bounded scene.

The design references and their provenance are preserved in
`reference/inspiration/navigation`. They are not shipped with the app. The
fixture validates glanceable guidance and state recovery; it does not claim
live map data.
