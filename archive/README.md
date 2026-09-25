# Archive

Earlier directions from the Codex sessions, kept for reference. None of this is used by the
C++/SDL3 port in `gunboat-port/`, and none of it is maintained. Paths inside may need adjusting
before anything here runs again.

| Folder | What it was |
| --- | --- |
| `python-prototype/` | Clean-room top-down remake prototype in Python/Pygame (`remake/`), its PyInstaller spec, build folder and packaged `dist/GunboatPrototype.exe`. |
| `threejs-lab/` | Browser map lab and "River Patrol" playable prototype (Three.js), with its launchers, Playwright checks (`node_modules/`) and `PLAYABLE_BUILD.md`. `web/gunboat_3d_map/data/` keeps a snapshot of the world export; the live copy is regenerated into `reverse_engineering/out/world_export/` by `reverse_engineering/tools/export_original_world.py`. |
| `ghidra/` | Ghidra 12.0.4 install, the `GunboatAudit` project and its Java export script. The findings it produced are recorded in `reverse_engineering/GHIDRA_VALIDATION.md` and `ORIGINAL_WORLD_FORMAT.md`. The project now uses Python + Capstone for the executable map instead (`reverse_engineering/tools/gbindex.py`). |

Archived on 2026-09-25 when the project moved to a faithful C++/SDL3 source port.
