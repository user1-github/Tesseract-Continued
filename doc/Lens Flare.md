# Lens Flare

## Overview

Lens flare is implemented as a fullscreen HDR compositing pass.

The system renders two source families:

- the sun source, driven from `sunlight` / `sunlightscale`,
- queued map flare sources, submitted from `particles` entities.

The shader builds the visible flare from layered procedural components:

- overexposed core,
- halo / corona,
- starburst shafts,
- anamorphic diagonal streak,
- lens ghosts.

Queued map flares currently use the same shader and visibility logic, but only enable shafts and optional ghosts.

## Where It Is Implemented

- Engine logic and tunables:
  - `src/engine/lensflare.cpp`
  - `src/engine/engine.h` (`namespace sunFlares`)
- Main frame integration:
  - `src/engine/rendergl.cpp`
  - `src/engine/renderlights.cpp` (clean up)
- Map entity submission:
  - `src/engine/renderparticles.cpp`
- Shader:
  - `config/glsl/lensflare.cfg` (`lazyshader 0 "lensflare"`)

## Current Architecture

This is not the legacy quad-based `lensflare.h` path.

Current flow:

1. Map entities call `sunFlares::addFlares(...)`.
2. `lensflare.cpp` queues flare descriptors for the current frame.
3. `sunFlares::render()` resolves sun and queued flare state.
4. The `lensflare` shader renders one fullscreen pass per active flare source.
5. The pass is additively composited into HDR before postfx.

This means flare entities are authored through the `particles` entity, but are no longer rendered by the particle renderer itself.

## Render Pipeline (Runtime)

`sunFlares::render()` in `src/engine/lensflare.cpp` is the active render path.

Flow:

1. Evaluate whether flares are globally enabled.
2. Build sun state if `sunflares` is enabled and a valid sun source exists.
3. Process queued map flare descriptors.
4. Bind scene depth (`tex0`), scene normal (`tex1`), and volumetric cloud composite (`tex2`).
5. Bind HDR framebuffer.
6. Enable additive blending:
   - `GL_SRC_ALPHA, GL_ONE`
7. Upload shared uniforms:
   - source screen position,
   - source color,
   - source exposure/strength,
   - ghost strength,
   - layer weights,
   - visibility override / occlusion data,
   - FOV scale,
   - cloud texture params.
8. Draw a fullscreen quad per source.

The pass runs in `rendergl.cpp` after fog overlay and before AA/postfx.

## Shader Logic (`lensflare`)

Fragment stage summary:

1. Reconstruct source-relative screen space from:
   - `sunFlareScreen.xy`
   - `sunFlareFovScale`
   - current viewport aspect
2. Sample source visibility from:
   - scene depth/normal,
   - volumetric cloud composite alpha
3. Build separate visibility terms:
   - `coreVis`
   - `haloVis`
   - `ghostVis`
   - `streakVis`
4. Generate procedural flare layers:
   - blown core,
   - halo/corona,
   - starburst shafts,
   - anamorphic streak,
   - chromatic lens ghosts
5. Apply exposure shaping from `sunFlareParams.w`.
6. Apply per-layer fade and compose the final additive color.

## Occlusion Model

### Sun source

The sun source uses visibility sampling around the projected sun center.

Each tap combines:

- sky test from scene depth/normal,
- cloud transmittance from the volumetric cloud composite.

This lets clouds and geometry attenuate the source differently across flare layers.

### Queued map flares

Queued flare sources use the same layered visibility structure, but not the same source test.

Instead of testing for sky, the engine passes:

- projected source depth,
- projected pixel radius for a camera-facing `10` unit occlusion disc.

The shader then estimates how much of that source disc is visible on screen. This is what fades the ghost layer for map flares.

## Sun Source Behavior

Sun flares inherit from:

- `sunlight` for color,
- `sunlightscale` for energy,
- current FOV for apparent scale.

The sun path enables all flare layers:

- core,
- halo,
- shafts,
- ghosts.

## Queued Flare Behavior

Queued flare sources come from `particles` entity modes `32..35`.

They use the same shader, but layer weights are restricted so they render:

- shafts only, or
- shafts + ghosts.

They do not currently enable the sun core/halo layers.

### Entity modes

- `32` = plain
  - shafts + ghosts
  - distance limited by `attr5`
- `33` = sparkle
  - shafts only
  - distance limited by `attr5`
- `34` = sun
  - shafts only
  - no distance limit
- `35` = sparklesun
  - shafts + ghosts
  - no distance limit

### Entity fields

- `attr2` = red
- `attr3` = green
- `attr4` = blue
- `attr5` = max distance for limited modes

Submission happens in `src/engine/renderparticles.cpp`, but rendering is handled later by `sunFlares::render()`.

## Exposed Variables

Declared in `src/engine/lensflare.cpp`:

- `flares` (0/1)
  - master enable for the whole flare system.
- `sunflares` (0/1)
  - enables or disables only the sun source.
- `flareghosts` (0/1)
  - enables or disables all ghost layers.
  - shafts remain active.
- `sunflareshaftsize`
  - controls shaft/streak reach.
- `sunflarestrength`
  - global flare intensity scaling.

## FOV Behavior

Flare scale is normalized against a reference FOV in `lensflare.cpp`.

In practice this means:

- zooming in makes the effect appear larger,
- wide FOV makes the effect appear smaller.

Because the code uses `curfov`, both normal `fov` and zoomed `zoomfov` feed into the same runtime scaling path.

## Important Notes

- The active shader is `lensflare`, not `sunFlares`.
- The engine namespace is still `sunFlares`; this is just naming history.
- The legacy `src/engine/lensflare.h` file still exists in the repository, but it is not the active runtime path described here.
- Lens dirt has been removed from the current implementation.

## Troubleshooting

- No flares at all:
  - check `flares = 1`,
  - check `sunflarestrength > 0`.

- No sun flare:
  - check `sunflares = 1`,
  - check `sunlight` is non-zero,
  - check `sunlightscale > 0`.

- No ghosts but shafts still visible:
  - check `flareghosts = 1`,
  - for map flares, check the entity mode is `32` or `35`.

- Map flare disappears too early:
  - check `attr5` for modes `32` and `33`.

- Flare looks wrong through clouds:
  - verify the volumetric cloud composite is available,
  - verify source visibility is not being overridden unexpectedly.
