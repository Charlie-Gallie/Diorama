# Test models

Generated, not authored — no third-party assets, nothing to license.

**Delete this directory before shipping.** Nothing here is game content.

| File | Purpose |
|---|---|
| `orientation_16.vox` | Axis-coloured shell: +X red, +Y green (engine up), +Z blue, with an asymmetric L marker on the top face. If the L reads backwards, the Z-up to Y-up swizzle has become a mirror rather than a rotation. |
| `sphere_16/32/64.vox` | The same shape at three densities. For judging voxels-per-screen-pixel without re-authoring anything. |
| `metre_cube_40.vox` | A 1 m cube at the leaning density of 40 voxels/m, with a chequered top so the grid itself is legible. The scale reference for every density judgement. |
| `scene.vox` | Everything laid out together at 40 voxels/m on a floor chequered every metre. Row 1: a 1 m reference cube, then the same chair at 40 and 20 voxels/m. Row 2: one sphere, 1 m across, at 40, 20 and 10 voxels/m. This is the one to look at for scale and density judgements. |
| `density_40/30/20/10.vox` | **The density test.** One 6.2 x 3.5 m scene resampled at 40, 30, 20 and 10 voxels/m. Every shape is rasterised analytically at each density rather than upscaled, so the files differ only in resolution, and the grid proportions match exactly - the renderer normalises the longest axis, so switching between them changes voxels per metre and nothing else about the framing. Contents: sphere, torus, lathed vase, cone, rounded cube, twisted column, diagonal capsule, spotted mushroom, and a 1 m cube quartered every 25 cm. Colour ramps rather than flat colours, so colour banding is visible too. Keys 7/8/9/0 in the client. |
| `car_50/40/30/20/10.vox` | **The scale and curvature test.** A 4.30 x 1.80 x 1.50 m hatchback, wheelbase 2.62 m, on 0.65 m wheels, resampled at five densities. Built from signed-distance primitives and sampled analytically, so the wheels are true circles at every resolution rather than one circle scaled down. Carries the things a car is useful for: circular wheels with a recessed rim behind a proud tyre sidewall, wheel arches and a hollow cabin as genuinely concave geometry, one paint colour across panels at every angle, one-voxel panel shut lines at any density, and separate trim, glass, lights and rims. 50 voxels/m exists only here - the shapes scene is 6.2 m on its longest axis, which would exceed the 256 voxel limit of a `.vox` axis. |
| `chair_20/40.vox` | Recognisable silhouette with thin features (legs), at 20 and 40 voxels/m. Tests whether thin geometry survives at each density. |

Regenerate with the scripts in the session scratch directory, or rewrite them —
the format is simple enough that the writer is about thirty lines.
