# God Rays

## Overview

God rays are implemented as a screen-space volumetric light shaft pass that reuses the sunlight CSM shadow atlas.

Per pixel, the shader:
- reconstructs a world-space ray from camera to the visible scene depth (or a far point for sky pixels),
- ray-marches along that segment,
- samples sunlight visibility from CSM at each step,
- accumulates and clamps intensity,
- writes the result to a low-resolution HDR buffer.

The result is then optionally blurred (depth-aware), upsampled (depth-aware), and additively composited into HDR.

## Where It Is Implemented

- Engine logic and tunables:
  - `src/engine/godrays.cpp`
- Public API:
  - `src/engine/engine.h` (`namespace godRays`)
- Sunlight shadow atlas binding (CSM integration):
  - `src/engine/renderlights.cpp` (`godRays::bindSunlightShadowMap()`)
- Frame integration point:
  - `src/engine/rendergl.cpp` (`godRays::render()`)
- Cleanup hook:
  - `src/engine/renderlights.cpp` (`godRays::cleanup()` in `cleanuplights()`)
- Shaders:
  - `config/glsl/volumetric.cfg`
    - `godRaysVariantShader` (ray march)
    - `godRaysBilateralVariantShader` (optional bilateral blur)
    - `godRaysUpsampleVariantShader` (depth-aware upsample)

## Render Pipeline (Runtime)

`godRays::render()` flow:

1. Early-out via `godRays::shouldRender()`.
2. Ensure shader variant and internal render targets exist at `godraysscale`.
3. Bind sunlight shadow atlas (`bindSunlightShadowMap()`).
4. Low-res god ray pass:
   - read scene depth,
   - world reconstruction,
   - CSM ray march,
   - write to ping-pong buffer 0.
5. Optional bilateral blur (X then Y) in low resolution.
6. Composite into HDR:
   - if reduced resolution: depth-aware upsample shader,
   - else: direct linear sample.
7. Additive blend (`GL_ONE, GL_ONE`) into HDR target.

## Ray March Pass Details

Main shader: `godRaysVariantShader` in `volumetric.cfg`.

Important behavior:

- Depth decode:
  - uses `gfetch(tex3, depthTc)` and `gdepthunpack` helpers.
- Geometry pixels:
  - world position reconstructed from depth and `worldmatrix`.
- Sky pixels:
  - detected from raw depth extremes (`gdepthformat` aware),
  - reconstructed to a far point (`godRayParams.x`, based on `csmfarplane`),
  - allows rays on sky/background.
- Shadow sampling:
  - `sampleSunlight(worldPos)` maps sample position into CSM split space and calls `filtershadow(...)`.
- Phase term:
  - forward-biased term from `dot(rayDir, sunlightdir)`.
- Soft intensity clamp:
  - `intensity = maxAmount * (1.0 - exp2(-intensity/maxAmount))`
  - geometry uses `godraysmax`,
  - sky uses `godraysskymax`.

## Blur and Upsample

### Bilateral Blur (optional, low-res)

`godRaysBilateralVariantShader`:
- separable X/Y pass,
- Gaussian-like spatial term + depth similarity term,
- preserves depth edges while smoothing banding/noise in low-res shafts.

### Depth-Aware Upsample

`godRaysUpsampleVariantShader`:
- gathers low-res neighborhood around full-res pixel,
- computes depth-weighted blending from full-res depth differences,
- fallback to nearest-depth sample when neighborhood weights collapse at hard edges,
- blends linear vs depth-aware result using `blurBlend`.

## Shader Variants

`godrays.cpp` generates shader names from runtime options:

- Shadow filter mode option: `N`, `f`, `F`, `E`, `g`, `G`
- MSAA depth option: `m` suffix when active
- CSM split count
- God ray step count

This keeps shader specialization aligned with current shadow/MSAA settings.

## Exposed Variables

Declared in `src/engine/godrays.cpp`:

- `godrays` (`VARP`, `0..1`, default `1`)
  - master enable.
- `godraysscale` (`FVARP`, `0.0625..1.0`, default `0.5`)
  - internal rendering scale.
- `godraysblur` (`VARP`, `0..3`, default `1`)
  - bilateral blur radius in low-res buffer.
- `godrayssteps` (`VARP`, `1..256`, default `16`)
  - ray-march step count.
- `godraysstrength` (`VARR`, `0..200`, default `10`)
  - global intensity multiplier.
- `godraysmax` (`VARR`, `1..10`, default `2`)
  - max shaft intensity clamp for geometry (`value/100`).
- `godraysskymax` (`VARR`, `0..10`, default `2`)
  - max shaft intensity clamp for sky/background (`value/100`).
- `godrayscolor` (`CVAR1R`, default `0`)
  - `0`: inherit sunlight color,
  - otherwise: explicit override color.

## Preconditions and Dependencies

God rays only render when all conditions are true:

- `godrays == 1`
- `godraysstrength > 0`
- `godraysscale > 0`
- `godrayssteps > 0`
- not in drawtex/debug texture mode
- `csmshadowmap` enabled
- sunlight color is non-zero
- shadow atlas for CSM is valid and rendered

## Resource Lifecycle

- Internal ping-pong textures/FBOs are recreated when view size or `godraysscale` changes.
- `godRays::cleanup()` releases all FBO/texture resources and resets cached shader handles/keys.
- Cleanup is called from global light cleanup (`cleanuplights()`).

## Troubleshooting

- No rays visible:
  - verify `godrays=1`,
  - verify `godraysstrength > 0`,
  - verify CSM/shadow atlas is enabled and sunlight exists.

- Geometry too bright:
  - lower `godraysstrength`,
  - lower `godraysmax`.

- Sky too bright:
  - lower `godraysskymax` (or set to `0` to disable sky contribution).

- Aliasing/banding at low scale:
  - increase `godraysscale`,
  - increase `godraysblur`,
  - tune `godrayssteps` for quality/performance balance.