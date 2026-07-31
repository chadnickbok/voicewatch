# Notifications reference provenance

Captured on 2026-07-30 for Project Parallax. These files are design references,
not product assets. Copyright remains with their respective owners.

## Sources

| Local file | Source | SHA-256 |
|---|---|---|
| `wear-standard-collapsed.png` | [Wear OS notification standard template, collapsed](https://developer.android.com/static/wear/images/design/notification_3.png) | `7fc2f0b98ccac8e7a826049d4210b2bf0e040299751f4895aac25f2f7c9bc0b2` |
| `wear-standard-expanded.png` | [Wear OS notification standard template, expanded](https://developer.android.com/static/wear/images/design/notification_7.png) | `61bd6e0926ebaeb88223fe72071fca0034e472327bdfa05223e81c4819ee5311` |
| `wear-big-text-collapsed.png` | [Wear OS big-text template, collapsed](https://developer.android.com/static/wear/images/design/notification_6.png) | `278627ea16a00840ddb2d41a432781c99f9c64a653a10d16662682ccec47261a` |
| `wear-big-text-expanded.png` | [Wear OS big-text template, expanded](https://developer.android.com/static/wear/images/design/notification_8.png) | `a533a881ed43a023da13a330724358433da83d78508b50340c5c232a929ea607` |
| `apple-notification-watch.png` | [Apple Watch notification example](https://cdsassets.apple.com/live/7WUAS350/images/apple-watch/ios-26-iphone-16-pro-watchos-26-series-10-notification-watch.png) | `4aec98be2563a1bf2ec4eae08863b3934c92b4a47e6c8246ca4efc508d3270e2` |
| `apple-clear-notifications.png` | [Apple Watch Clear All example](https://cdsassets.apple.com/live/7WUAS350/images/apple-watch/watchos-26-series-10-clear-notifications.png) | `10f1d25ce60281214a4da91fee2c389c93a436d3f05c242e2184e2433740df06` |

Supporting guidance:

- [Wear OS notifications](https://developer.android.com/design/ui/wear/guides/m2-5/surfaces/notifications)
- [Material 3 Expressive for Android and Wear OS](https://blog.google/products-and-platforms/platforms/android/material-3-expressive-android-wearos-launch/)
- [Apple Watch notifications](https://support.apple.com/en-gb/108369)
- [Read messages on Apple Watch](https://support.apple.com/en-ca/guide/watch/apdcf848d29e/watchos)
- [Send and reply to messages from Apple Watch](https://support.apple.com/en-euro/guide/watch/apd92a90f882/watchos)

## Design observations

- A notification should be glanceable before it is interactive: source, sender,
  recency, and message are visually distinct.
- The expanded state gives the message the most space and exposes one obvious
  contextual action.
- Suggested replies are full-width, wrist-sized actions beneath the message,
  not tiny inline affordances.
- The square Doodad adaptation should preserve the hierarchy and action
  semantics without copying round-screen clipping or geometry.
- Avatar/photo support is intentionally deferred to the shared `image`
  component validated by the Media app. Notification rendering must be able to
  adopt that component later without changing its interaction contract.
