# Tesseract Continued

**Tesseract Continued** is a project that aims to continue and expand the development of the Tesseract engine.

The goal is to modernize the engine by introducing new graphical effects, reducing hard-coded gameplay logic, and providing clear documentation so developers can more easily build FPS games using the Cube Engine ecosystem.

This project focuses on improving visual fidelity, engine flexibility, and developer accessibility while staying true to the fast and lightweight nature of Cube Engine.

## Goals

- Modernize the rendering pipeline with new graphical effects
- Reduce hard-coded systems to make the engine more flexible and moddable
- Provide clear and complete engine documentation
- Make the development of FPS games faster and easier on Cube Engine

## Current Features
* **Heat Haze Particles:** Creates realistic air distortion effects for heat sources such as explosions, fire, or environmental effects.
* **God Rays:** A lightweight sun shaft implementation that reuses the cascaded shadow map to produce convincing atmospheric light scattering at a very low performance cost.

## Planned Features:
### Core
* **Full internationalization (i18n) support:** Allow the engine interface and game content to be translated into any language.
* **TrueType font (TTF) support:** Replace pre-rendered character texture maps with dynamic font rendering for better scalability and flexibility.

### Gameplay
* **Fully data-driven weapons:** Weapons will become completely soft-coded, allowing new weapons to be added or modified easily without touching engine code.

### Graphics
* **Improved procedural atmosphere:** More advanced sky rendering including ray lighting, support for multiple suns, and a dynamic night sky.
* **Volumetric clouds:** Real-time volumetric cloud rendering for richer and more dynamic skies.

### Audio
* **Migration to OpenAL Soft:** Replace SDL_mixer with OpenAL Soft to enable modern spatial audio features and EFX-based effects.

## Vision

Tesseract Continued aims to push the Cube Engine forward while keeping its core philosophy intact:  
**fast, moddable, and accessible real-time 3D technology.**

The long-term objective is to turn the engine into a solid foundation for modern FPS projects while maintaining the simplicity that made Cube Engine attractive to developers in the first place.