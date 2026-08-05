# Roboto Flex watch-face instance

This directory vendors the static Roboto Flex instance used by the trusted
watch-face numeral asset.

- Upstream: `googlefonts/roboto-flex`
- Commit: `739e06dc46ebb14cddd88b9768a6c1504d4677f6`
- License: SIL Open Font License 1.1 (`OFL.txt`)
- Instance axes: `wdth=60`, `wght=760`, `opsz=120`; all remaining axes use
  upstream defaults and are pinned into the static TTF.
- Instance SHA-256:
  `4aff1d6457f2fb0f970a63d4ec764d43ed5f8625f25655e35eedbee77e23913a`

The checked-in 114px LVGL C asset is a 4bpp subset containing only
`0123456789:`.
Keeping the instantiated source TTF here makes that generated asset
reproducible without depending on a system font or a network fetch.
