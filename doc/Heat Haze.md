# Heat Haze

## Overview

Heat haze is implemented as a distortion particle effect.

Each heat haze particle:
- uses a normal/noise texture (`tex0`) to generate a screen-space UV offset,
- samples a copy of the current rendered scene (`tex1`),
- uses scene depth (`tex2`) to avoid leaking through foreground geometry,
- blends back over the scene using particle alpha and soft/depth fades.

## Where It Is Implemented

- Engine logic and tunables:
  - `src/engine/heathaze.cpp`
  - `src/engine/engine.h` (`namespace heatHaze`)
- Particle renderer integration:
  - `src/engine/renderparticles.cpp`
- Shader:
  - `config/glsl/particle.cfg` (`shader 0 "particleHaze"`)
- Public particle IDs:
  - `src/shared/iengine.h`

## Heat Haze Particle Types

Two particle IDs are currently used:

- `PART_HAZE_NOISE_1`
  - Normal map: `media/particle/haze/noise_1.png`
  - Flags: `PT_HAZE|PT_PART|PT_FEW|PT_SOFT|PT_LERP|PT_SCROLL|PT_FADE`
  - Behavior: scrolling + edge fade enabled.

- `PART_HAZE_PULSE_MUZZLE_FLASH`
  - Normal map: `media/particle/haze/pulse_muzzle.png`
  - Flags: `PT_HAZE|PT_PART|PT_FEW|PT_FLIP|PT_TRACK|PT_LERP`
  - Behavior: no scrolling + no edge fade.

## Render Pipeline (Runtime)

`hazeRenderer::render()` in `renderparticles.cpp` is the custom renderer path.

Flow:
1. Check `heatHaze::shouldRender()`.
2. Copy the current scene color buffer to the haze source texture (`heatHaze::bindSceneTexture()`).
3. Bind shader `particleHaze`.
4. Upload haze params with `heatHaze::setShaderParams(scroll, fade)`.
5. Render particle quads.

### Important Copy Detail

`bindSceneTexture()` temporarily disables `GL_SCISSOR_TEST` while blitting scene color into the refract source texture.

Reason:
- transparent passes can leave scissor enabled,
- a partial blit leaves stale/black regions in the haze source,
- distortion then samples those stale regions and produces black artifacts.

This was explicitly guarded to keep haze clean behind transparent materials.

## Shader Logic (`particleHaze`)

Fragment stage summary:

1. Read normal/noise:
   - `noise = texture2D(tex0, fract(texcoord0 + heatHazeScroll))`
2. Build distortion vector:
   - `jitter = (noise.xy * 2 - 1) * scale`
3. Build modulation:
   - distance-based strength blend (`heathazenearstrength` / `heathazefarstrength`)
   - global strength (`heathazestrength`)
   - particle mask and lifetime fade
   - depth soft-fade
   - color modulation (`color` field)
4. Refract scene UV:
   - sample candidate refracted UV from copied scene (`tex1`)
   - depth-check candidate UV against scene depth (`tex2`)
   - reject/attenuate invalid refraction where foreground would leak
5. Final blend alpha:
   - includes soft/depth/lifetime
   - includes edge fade only when fade option is enabled (`PT_FADE` path)

## Strength Input (Color Field)

Heat haze uses particle color as strength input, interpreted as `0..100`.

In `newparticle(...)`:
- `PART_HAZE_NOISE_1` and `PART_HAZE_PULSE_MUZZLE_FLASH` convert color through:
  - `heatHaze::strengthToColor(int strength)`

Mapping:
- `0` -> `0x000000` (no haze),
- `100` -> `0xFFFFFF` (max haze).

This keeps haze strength authoring simple and consistent.

## Exposed Variables

Declared in `src/engine/heathaze.cpp`:

- `heathaze` (0/1)
  - master enable.
- `heathazestrength`
  - global multiplier for distortion offset.
- `heathazenearstrength`
  - strength near the camera (`heathazenear`).
- `heathazefarstrength`
  - strength near `heathazefar`.
- `heathazenear`
  - start distance for near/far interpolation.
- `heathazefar`
  - end distance for near/far interpolation.
- `heathazedepthscale`
  - controls softness/depth rejection around geometry intersections.
- `heathazescrollx`
  - X UV scroll speed.
- `heathazescrolly`
  - Y UV scroll speed.

## Flags Used by Heat Haze

Renderer flags in `renderparticles.cpp`:

- `PT_HAZE`
  - marks haze particle families.
- `PT_SCROLL`
  - enables UV scroll in shader params.
- `PT_FADE`
  - enables texture alpha edge fade in shader params.
- `PT_LERP`
  - uses alpha blending for proper distortion compositing.
- `PT_TRACK`
  - used by muzzle haze to follow the owner/muzzle.

## How To Spawn It

### Map entity (`particles`)

Entity mode:
- `attr1 = 36` (heat haze)
- `attr2 = size`
- `attr3 = strength 0..100`

Current implementation uses:
- `PART_HAZE_NOISE_1` from this map entity mode.

### Gameplay effect example

- Applied on Pulse Gun and Rail Gun muzzle flashes:
  - `PART_HAZE_PULSE_MUZZLE_FLASH` in `src/game/weapon.cpp`.
- Added a fire and a heat haze emitter on the map "Complex"
  - `newent particles 36 140 50`.

## Troubleshooting

- No haze visible:
  - check `heathaze=1`,
  - check `heathazestrength > 0`,
  - check near/far strengths are not both `0`,
  - check particle strength (`color`) is not `0`.

- Haze leaks over foreground edges:
  - verify depth textures are available,
  - verify depth-scale tuning (`heathazedepthscale`).

- Black artifacts near transparent materials:
  - ensure full-frame scene copy in `bindSceneTexture()` (scissor temporarily disabled).
