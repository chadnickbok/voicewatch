# Voice Notes reference provenance

Captured 2026-07-30 for the Project Parallax Voice Notes oracle.

## First-party visual references

| File | Source | SHA-256 |
| --- | --- | --- |
| `apple-voice-memos-home.png` | [Apple Watch User Guide: Record and play voice memos](https://support.apple.com/guide/watch/voice-memos-apd441786282/watchos) | `cef2eb34e965faf740c3a331e9cda0f2a372c5ba04818530a9ad135d80209ba8` |
| `apple-voice-memos-recording.png` | [Apple Watch User Guide: Record and play voice memos](https://support.apple.com/guide/watch/voice-memos-apd441786282/watchos) | `c35b5ae2695906a13da7e625db17bccf53ef4e892e570417606291741ad92f18` |
| `pixel-watch-recorder.png` | [Google Pixel Watch Help: Record audio on your Pixel Watch](https://support.google.com/googlepixelwatch/answer/15407317?hl=en-UK) | `0f8f6231531272a9eb7708a7a155a1dba29b548db638ac03ebba24e5905c9a4a` |

## Behavioral reference

Google documents record, pause/resume, save/delete, screen-sleep-safe
recording, a 30-minute watch limit, and optional phone sync/transcription.
Android's [Wear OS voice input guidance](https://developer.android.com/training/wearables/user-input/voice)
separates raw audio recording from free-form speech recognition and system
voice actions.

## Design decisions carried into the oracle

- The idle screen gives the record affordance most of the available area.
- Recording prioritizes elapsed time and a single finish action.
- Local capture safety remains visible when the screen sleeps or networking is
  unavailable.
- Transcript review and save are explicit states rather than hidden provider
  side effects.
- The generic app-title bar is omitted; the compact top label communicates
  state and storage context instead.
