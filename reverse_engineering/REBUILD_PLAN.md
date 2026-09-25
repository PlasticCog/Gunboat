# Gunboat Remake Plan

> **Superseded (2026-09-25).** This was the plan for a clean-room remake. The project is now a
> faithful C++/SDL3 source port; see `/CLAUDE.md` and `RE_GUIDE.md`. The Python prototype and
> the Three.js lab it describes are in `/archive/`.

## Guiding Rule

Use the DOS version to understand systems, pacing, and data organization. Build new code, new assets, and revised writing for the remake unless rights are explicitly cleared.

Steel Thunder is now available as a sibling-game DOS reference set at:

```text
Original DOS version\SteelThunder
reverse_engineering\STEEL_THUNDER_REFERENCE.md
```

Use it to compare engine behavior, file loaders, loose asset naming, palettes, object data, text resources, and mission/table formats. Treat it as research material, not as remake content to copy directly.

## Core Experience To Preserve

- Small-boat river combat with restricted sightlines, ambushes, and branching waterways.
- Four-person PBR crew with meaningful stations and damage consequences.
- Mission flow from headquarters to briefing, outfit/spec selection, tactical map, raid, debrief, and roster update.
- Region-based escalation: Vietnam, Colombia, Panama Canal.
- Practice modes for gunnery, grenades, and piloting.
- Tactical pressure from mines, shore weapons, boats, missiles, salvos, aircraft, and mission timers.

## Modern Improvements

- Mouse/gamepad/keyboard controls with optional station hotkeys.
- Clearer damage-control UI and crew state.
- Procedural river variations layered over authored mission objectives.
- Better enemy AI: suppression, fleeing, ambush triggers, patrol routes, target priority.
- Expanded outfitting: ammunition mix, armor/fuel tradeoffs, crew perks, sensor upgrades.
- Mission generator that can produce patrol, interdiction, extraction, demolition, escort, and timed strike variants.
- Campaign persistence with injuries, promotions, repairs, replacement boats, and regional threat levels.
- Fresh art/audio inspired by riverine warfare, not copied from the DOS assets.

## Suggested Technical Shape

- Engine: Godot, Unity, Thunder Engine, or a custom web/TypeScript prototype depending on desired scope.
- Simulation tick: fixed-step 2D gameplay with separate render interpolation.
- World model: river graph plus local tile/terrain grid.
- Entity model: boat, crew station, weapon mount, projectile, shore unit, vehicle, objective, pickup/dropoff.
- Data-driven missions: JSON/YAML definitions for region, map seed, objectives, spawns, briefing, success/failure rules.
- Renderer: top-down tactical layer plus station/weapon views as optional modes.

## Thunder Engine Candidate

Thunder Engine has been cloned for assessment at:

```text
reverse_engineering\references\thunder
```

Assessment notes are in:

```text
reverse_engineering\THUNDER_ENGINE_ASSESSMENT.md
```

Current conclusion: Thunder Engine is useful as a possible modern C++ rebuild target, but it is not currently evidence of a Steel Thunder/Gunboat source port. Treat it as a clean-room destination engine that can consume exported Gunboat research data, not as a shortcut to original Accolade code or data structures.

## First Prototype Milestone

1. Single playable river map with PBR movement, speed/reverse, and turn inertia.
2. Three weapon stations: bow, midship, rear.
3. Shore ambush enemy with line-of-sight and simple projectile fire.
4. Damage model for hull, two engines, two waterjets, fuel, and four crew.
5. One mission: identify and destroy supply boats, return to base.
6. Debrief screen with destroyed targets, crew injuries, boat loss, and mission result.

## Current Playable Prototype

A first Windows prototype now exists in:

```text
remake\gunboat_windows.py
```

Run from source:

```powershell
python .\remake\gunboat_windows.py
```

Run the packaged Windows build:

```powershell
.\dist\GunboatPrototype.exe
```

The prototype includes:

- Top-down river patrol movement with throttle, reverse, turn inertia, grounding damage, and fuel use.
- Mouse aiming and three weapon stations: bow gun, midship mortar burst, rear gun.
- Shore emplacements and moving sampans with simple hostile fire.
- Hull, engine, waterjet, fuel, and crew-station damage.
- Tactical map overlay.
- Mission loop: destroy 8 hostile contacts, then return to base.
- Candidate DOS `MAP.LZ` record sampling as a terrain seed when extracted assets are present, with a procedural fallback.

Controls:

- `W/S` or arrow up/down: throttle/reverse.
- `A/D` or arrow left/right: turn.
- Mouse: aim.
- Left click or `Space`: fire.
- `1`, `2`, `3`: select weapon station.
- `R`: emergency repairs.
- Hold `Tab`: tactical map.
- `P` or `Esc`: pause.

## Current 3D Map Workbench

A Three.js inspection lab now exists in:

```text
reverse_engineering\web\gunboat_3d_map
```

Launch it with:

```powershell
& ".\reverse_engineering\Launch 3D Map Lab.bat"
```

Or manually:

```powershell
python .\reverse_engineering\tools\export_gunboat_3d_map_assets.py
python -m http.server 8765 --bind 127.0.0.1 --directory .\reverse_engineering\web\gunboat_3d_map
```

It is modeled after the Test Drive 3 browser viewer interaction style: full-window 3D scene, mouse orbit/zoom, dataset switching, height scaling, water-level probing, and wireframe mode. Current terrain is an exploratory lift of decoded tactical-map material strips plus a raw record-83 probe, not yet a confirmed reconstruction of Gunboat's original 3D map engine.

## Reverse-Engineering Work Still Needed

- Reconstruct the executable relocation table or use Ghidra with the rough unpacked image.
- Identify the asset chunk names that correspond to each `DATAC.DAT` record.
- Finish `.LZ` graphics integration: palette assignment, row orientation, exact dimensions, and record naming. The current best decoder is Test Drive 3-style LZW followed by RLE.
- Trace the actual 3D/map loader. Test Drive 3 uses a confirmed `32 x 16` tile grid with tile id, rotation, and elevation; Gunboat's equivalent table has not yet been positively identified.
- Decode `GBROSTER.DAT` once real roster entries exist.
- Trace the file loader, mission table loader, input handling, damage routines, and scoring routines.
- Capture reference gameplay timing from DOSBox: acceleration, turning radius, projectile rates, enemy spawn behavior, and mission timers.
