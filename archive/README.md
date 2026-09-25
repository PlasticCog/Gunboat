# Archive

Earlier directions from the Codex sessions, kept for reference. None of this is used by the
C++/SDL3 port in `gunboat-port/`, and none of it is maintained. Paths inside may need adjusting
before anything here runs again.

| Folder | What it was |
| --- | --- |
| `python-prototype/` | Clean-room top-down remake prototype in Python/Pygame (`remake/`), its PyInstaller spec, build folder and packaged `dist/GunboatPrototype.exe`. |
| `threejs-lab/` | Browser map lab and "River Patrol" playable prototype (Three.js), with its launchers, Playwright checks (`node_modules/`) and `PLAYABLE_BUILD.md`. `web/gunboat_3d_map/data/` keeps a snapshot of the world export; the live copy is regenerated into `reverse_engineering/out/world_export/` by `reverse_engineering/tools/export_original_world.py`. |
| `ghidra/` | The Codex-era `GunboatAudit` Ghidra project and its Java export script, which named only the terrain routines. The findings are recorded in `reverse_engineering/GHIDRA_VALIDATION.md` and `ORIGINAL_WORLD_FORMAT.md`. Superseded by the scripted pipeline in `reverse_engineering/tools/decompile.ps1`; the Ghidra install itself moved to `/_tools/`. |

Archived on 2026-09-25 when the project moved to a faithful C++/SDL3 source port.
