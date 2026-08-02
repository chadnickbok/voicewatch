# Weather RGB565 contrast report

Generated from `weather-foundations-v1.json`. Ratios use colors expanded back from RGB565, matching the display path.

| Foreground | Background | Required | RGB888 | RGB565 | Result |
| --- | --- | ---: | ---: | ---: | --- |
| `on_background` | `background` | 7.0:1 | 18.89:1 | 19.38:1 | PASS |
| `on_primary` | `primary` | 7.0:1 | 8.81:1 | 9.10:1 | PASS |
| `on_primary_container` | `primary_container` | 6.0:1 | 6.08:1 | 6.36:1 | PASS |
| `on_secondary_container` | `secondary_container` | 7.0:1 | 11.97:1 | 12.37:1 | PASS |
| `on_tertiary` | `tertiary` | 7.0:1 | 11.14:1 | 11.59:1 | PASS |
| `on_surface` | `surface_high` | 7.0:1 | 8.58:1 | 8.93:1 | PASS |
| `on_surface_variant` | `surface` | 6.0:1 | 8.49:1 | 8.72:1 | PASS |
| `on_error` | `error` | 7.0:1 | 10.64:1 | 11.10:1 | PASS |
| `on_error_container` | `error_container` | 6.0:1 | 7.05:1 | 7.13:1 | PASS |

All required pairs must pass after RGB565 quantization.
