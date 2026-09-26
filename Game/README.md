# Game: your original Gunboat files

Copy all the files of the original DOS *Gunboat* (Accolade, 1990) into this folder: the contents of
its disks or of the folder it was installed in. They are not part of this repository and are never
committed: Git ignores everything here except this README. The port reads them at run time.

The port reads:

| File | What |
| --- | --- |
| `GB.EXE` | the game (EXEPACK-packed as shipped; an unpacked copy works too) |
| `DATAA.DAT`, `DATAB.DAT`, `DATAC.DAT` | pictures, worlds, missions, texts |
| `VALK12.MUS` / `VALKPC.MUS` | the title music on the AdLib / on the PC speaker |
| `ADLIB.COM` | optional: the Ad Lib sound driver (without it the music plays on the speaker) |
| `GUNBOAT.CFG`, `GBROSTER.DAT` | optional: the configuration, and the roster that the game writes |

The names may be in any case. `gunboat.exe` finds this folder by itself (next to it, or at the top
of this repository for a build in `gunboat-port/build`); `--game-dir` or the launcher can point
elsewhere. The folder must be writable: the game saves its roster here.
