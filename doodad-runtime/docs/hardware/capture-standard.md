# CoreS3 hardware-capture standard

Every CoreS3 image intended to be shared as visual evidence must use the
checked-in sharp UI capture profile and must be color-corrected before it is
shared. This is the default contract for manual verification, app galleries,
and simulator-to-hardware comparisons.

## Required pipeline

The canonical profile is
[`config/capture/streamcam-cores3-sharp.json`](../../config/capture/streamcam-cores3-sharp.json).
It records the camera controls, registered viewport geometry, and fitted
affine sRGB correction matrix.

The shareable path is:

1. capture the full Logitech StreamCam frame with automatic exposure disabled;
2. use exposure `16`, gain `58`, white-balance temperature `4000 K`, and an
   explicit five-second autofocus settle;
3. crop the registered CoreS3 app viewport and resize it to exactly `240×240`;
4. apply the correction matrix from the same capture profile;
5. share the corrected output from `hardware-corrected/` or a comparison made
   from that output.

The capture scripts retain the full raw frame and the uncorrected normalized
crop for diagnosis. Those are not final evidence and should only be shared
when a raw/corrected diagnostic comparison is explicitly useful and clearly
labelled.

No Gaussian blur, low-pass filter, denoise pass, or unsharp mask is applied by
default. `--moire-sigma` remains an opt-in diagnostic control, with a default
of `0`. The required Lanczos resize is the only spatial resampling in the
standard path; affine color correction is independent per pixel and cannot
soften edges.

## Commands

Capture one or all conformance apps:

```bash
./scripts/capture-hardware-suite.sh \
  --port /dev/cu.usbmodem21101 \
  --app tasks
```

Share the resulting
`target/hardware-gallery/apps/hardware-corrected/tasks.png` or
`target/hardware-gallery/apps/comparison/tasks.png`, not the raw/crop
intermediates.

Regenerate the profile after the camera, device, lighting, or display
brightness changes:

```bash
./scripts/capture-color-bars.sh \
  --port /dev/cu.usbmodem21101 \
  --profile-output config/capture/streamcam-cores3-sharp.json
```

The app-capture command rejects exposure, gain, white-balance, or focus-mode
settings that do not match the selected calibration. Its crop geometry also
comes from that profile, so moving the fixture requires a new color-bars
capture before more evidence is shared.
