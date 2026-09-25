# Thunder Engine Assessment

Source checked:

- Repository: https://github.com/thunder-engine/thunder
- Local clone: `reverse_engineering/references/thunder`
- Checked commit: `806b535` (`2026-08-22`, `Uikit: AbstractItemView widget #1301 (#1384)`)
- License: Apache-2.0

## Lineage Finding

This repository should not be treated as a source port of Accolade's Steel Thunder/Gunboat engine.

Searches of the cloned source did not find meaningful references to:

- `Steel Thunder`
- `Gunboat`
- `Accolade`
- `Tom Loughry`
- `Test Drive 3`
- Gunboat-specific PBR/river-combat terms

The hits for `DOS`, `PBR`, and similar terms are generic third-party/library or modern-rendering references, not evidence of original Accolade technology.

## What It Is

Thunder Engine is a modern open-source modular C++ game engine. Its README describes it as a lightweight engine for 2D and 3D games with:

- Cross-platform targets.
- Physically based rendering.
- AngelScript support.
- Modular architecture.
- Editor tooling.

The local source confirms a CMake/C++17 build, Qt6 desktop editor dependencies, engine/editor/worldeditor projects, resource systems, actor/world/component structure, sprite/tilemap resources, UI kit, render backends, physics, and scripting support.

## Usefulness For Gunboat

Thunder could still be a viable target for a clean-room remake prototype because it already has many runtime pieces we would otherwise need to build:

- 2D sprite/tilemap rendering for tactical screens and UI-like station panels.
- 3D mesh/render support for a modernized river view.
- Component systems for PBR boat systems, crew stations, weapons, damage, enemies, and mission triggers.
- UI kit/editor infrastructure for menus, briefing screens, map overlays, and debug tools.
- AngelScript if we want data-driven mission behavior.

## Risks

- It does not decode Gunboat assets or tell us the original game logic.
- It may be heavier than the current Python/Pygame prototype for quick reverse-engineering iteration.
- Desktop/editor builds require modern CMake and Qt6 setup.
- We still need a neutral asset export pipeline from the DOS formats into PNG/JSON/mesh data before Thunder can consume anything cleanly.

## Recommended Role

Use Thunder as an optional modern engine target after the format work stabilizes.

Keep the immediate reverse-engineering workflow in the existing Python tools:

1. Decode and label DOS records.
2. Export clean research artifacts to neutral formats.
3. Identify real map/world/mission structures.
4. Build a small Thunder prototype that consumes exported JSON/PNG/mesh data.
5. Recreate gameplay systems cleanly rather than depending on original code.

