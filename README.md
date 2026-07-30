# CleanCam

CleanCam is a small native macOS utility for capturing the bright display on an
M5Stack CoreS3 SE through a USB webcam.

It:

- opens the Logitech StreamCam directly through AVFoundation;
- shows the exposure controls actually reported by the camera driver;
- meters a draggable region around the CoreS3 screen;
- can lower exposure automatically until screen highlights stop clipping;
- saves full-resolution PNG frames to `~/Pictures/CleanCam`.

## Build and run

```sh
chmod +x Scripts/build-app.sh
Scripts/build-app.sh
open .build/CleanCam.app
```

macOS will ask for camera access the first time. Select **Logitech StreamCam**
from the camera menu, drag a tight cyan rectangle around the CoreS3 display,
and click **Start display auto-tune**. When the display text is clean, click
**Capture full-resolution PNG**.

To inspect the controls reported by each connected camera without opening the
UI:

```sh
swift run CleanCam --probe
```

For an unattended capture, or to recover a wedged external UVC stream:

```sh
.build/CleanCam.app/Contents/MacOS/CleanCam --capture /tmp/cores3.png
.build/CleanCam.app/Contents/MacOS/CleanCam --reset-camera
```
