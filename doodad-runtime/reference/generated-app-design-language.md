# Generated app visual language

Generated app concepts are implementation targets, not shipping raster assets.
Each concept is a flat, full-bleed square framebuffer with no watch case,
bezel, perspective, presentation board, watermark, or generic app title bar.

Use the checked-in Doodad concepts as the visual-system master. Preserve their
near-black navy canvas, deep tonal surfaces, crisp sans-serif hierarchy,
restrained Material 3 Expressive shapes, spacing rhythm, and full-width inset
bottom action. Pick domain inspiration only for information hierarchy; do not
copy Apple, Google, or third-party branding.

The 240×240 product has one dominant value or action per screen. Keep secondary
copy quiet, use the whole square canvas, prefer one short flow over a dense
dashboard, and make every interactive target at least 48dp. Generated text must
be limited to exact product copy from the approved plan. Avoid glossy 3D,
neon glow, decorative outlines, tiny controls, ornamental illustration, and
invented features.

Image generation workflow:

1. Read the approved build plan and inspect two or three relevant images under
   `reference/design-language/` with the image-viewing tool.
2. Use the built-in image generation tool in `ui-mockup` mode. Treat the Doodad
   master as a style reference, not an edit target.
3. Generate one to three high-fidelity square screens. Keep the source outputs
   and create exact 240×240 PNG review targets under `design/targets/`.
4. Record the complete final prompt, chosen source references, and screen roles
   in `design/DESIGN_MANIFEST.json`.
5. The implementation must recreate the approved hierarchy with semantic
   AppSpec components. It must never embed the generated PNG in the app.

The independent verifier compares the primary 240×240 simulator capture with
the primary target. A visual miss is a build failure and may trigger a bounded
implementation repair; the target itself remains unchanged.
