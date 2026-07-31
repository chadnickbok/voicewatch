# Sensor Recorder oracle references

These files are checked-in design references for the Sensor Recorder
conformance scene. They are not shipped in the Doodad package.

| File | Source | SHA-256 |
| --- | --- | --- |
| `google-exercise-live.png` | [Google Health Services Exercise Sample live metrics](https://github.com/android/health-samples/blob/8613828d6309c00bec8a7704cced9fc445bd018f/health-services/ExerciseSampleCompose/app/src/test/screenshots/ExerciseScreenTest_pixel_watch.png) | `aa07d0d5bdc344455f0d939b424fb1f3d6fb427a1e71b4da1c19bda9b6697fed` |
| `google-exercise-summary.png` | [Google Health Services Exercise Sample summary](https://github.com/android/health-samples/blob/8613828d6309c00bec8a7704cced9fc445bd018f/health-services/ExerciseSampleCompose/app/src/test/screenshots/SummaryScreenTest_pixel_watch.png) | `0f42152aa8dc5a332ec1382223f923978504bad973b9abd37c34e964eb8b0017` |
| `google-ongoing-activity.png` | [Google Health Services Exercise Sample ongoing notification](https://github.com/android/health-samples/blob/8613828d6309c00bec8a7704cced9fc445bd018f/health-services/ExerciseSampleCompose/screenshots/ongoing_notification.png) | `8b779b14e0ee3daa5ddf0690b8e93a92d909cba50c7cae3557d6e57032121d91` |

Captured from Google's official `android/health-samples` repository at commit
`8613828d6309c00bec8a7704cced9fc445bd018f` on 2026-07-30.

The oracle follows the same hierarchy as Google's sample: live status and a
dominant metric first, compact supporting metrics second, decisive
pause/finish controls, and a separate completed-session state. Sensor Recorder
uses deterministic XYZ data rather than claiming to be an exercise app.
Google's current [Health Services guidance](https://developer.android.com/health-and-fitness/health-services)
and [ongoing-activity guidance](https://developer.android.com/training/wearables/notifications/ongoing-activity)
define the background-session and return-path behavior.
