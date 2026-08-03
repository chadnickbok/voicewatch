# Tasks reference provenance

Captured 2026-07-30 for Project Parallax. These files are design references
only; they are not shipped in the doodad runtime or firmware.

| File | Source | SHA-256 |
| --- | --- | --- |
| `apple-reminders-list.png` | [Apple Watch User Guide: Use Reminders](https://support.apple.com/en-my/guide/watch/-apdf10efb1bf/watchos) ([direct image](https://help.apple.com/assets/692F45642C5AEF37BA06084A/692F4568B3C902EE690E2EBC/en_US/547ae4abfbb7610b756b48d67e75d208.png)) | `66a0390fed5c25ea9873cba0ba74d6e4622a513b19fdf345a1d419ab4c213099` |
| `apple-reminders-lists.png` | [Apple Watch User Guide: Use Reminders](https://support.apple.com/en-my/guide/watch/-apdf10efb1bf/watchos) ([direct image](https://help.apple.com/assets/692F45642C5AEF37BA06084A/692F4568B3C902EE690E2EBC/en_US/6e3f2e6ce37c8e12ca07259856c37651.png)) | `88563af0db1ab7873043cb2e4501f38cfd1df7bba66fe3917983af36a440a8be` |
| `wear-focused-task.png` | [Wear OS design principles](https://developer.android.com/training/wearables/principles) ([direct image](https://developer.android.com/static/wear/images/principles_1.png)) | `5c92bd86375f74e1d6feb781e2104bb9a3d301e2e23d1a93888efdca4e6bb407` |
| `wear-quick-interaction.png` | [Wear OS design principles](https://developer.android.com/training/wearables/principles) ([direct image](https://developer.android.com/static/wear/images/principles_4.png)) | `50a28ec3b1e9d5d8e52526b44eb7e0208a3422038e87b42c5a2934611d4b1256` |

## Observations carried into the oracle

- Launch directly into the actionable list instead of spending the first
  screen on navigation or an app title.
- Make each task a full-width, one-tap completion target; the checkbox is a
  state cue, not the only hit target.
- Keep the current list/count as compact content context.
- Optimize the decisive flow for a few-second wrist interaction.
- Use the square viewport intentionally: two tasks plus add, or three tasks,
  fit without clipping or a generic top bar.

Apple and Google retain copyright in the reference images.
