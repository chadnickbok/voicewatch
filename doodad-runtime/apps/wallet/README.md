# Pass + QR Wallet

Title-free Wear and LVGL conformance package for a deterministic SFO-to-JFK
boarding pass.

Screens:

- glanceable boarding summary
- full boarding-pass details
- high-contrast boarding code
- rejected unsafe update
- issuer-mismatch review

The QR fixture is a real, non-production code generated for the Doodad demo
pass. It is packaged as a content-addressed `DIMG` resource containing a
135×135 RGB565LE bitmap. Wear Compose and LVGL decode the same bytes
independently and render them through their native image/canvas paths.

Every transition crosses the domain-scoped mocked wallet capability before
mounting the next bounded AppSpec.
