# Steel Thunder Reference Set

## Location

The Steel Thunder DOS release is available at:

```text
Original DOS version\SteelThunder
```

This is a useful sibling-game reference for Gunboat because both games are credited to Tom Loughry / Accolade and Gunboat's manual explicitly describes Gunboat as building on the Steel Thunder engine lineage. Do not confuse this with the unrelated modern open-source `Thunder Engine` repository assessed separately in `THUNDER_ENGINE_ASSESSMENT.md`.

## Top-Level Contents

- `St.exe` - main Steel Thunder executable, DOS MZ, 129,647 bytes.
- `Setup.exe` - graphics/joystick setup executable, 26,603 bytes.
- `Dat1.dat` through `Dat7.dat` - small data tables or map/mission chunks.
- `*.mpp` - many fixed-size visual/data assets, including `Title.mpp`, `Small.mpp`, `Gena.mpp`, tank-specific files, and HQ/interface-looking files.
- `*.ppk` - packed visual/data assets, including `Acco.ppk`, `Cinsig.ppk`, `Mp1.ppk` through `Mp4.ppk`.
- `Gencolr.bin`, `Quizcolr.bin`, `Tactcolr.bin`, `Titlcolr.bin` - 224-byte palette/color-table candidates.
- `Objdata.bin` - 16,631-byte object/data table candidate.
- `Text.bin` - 24,007-byte text/resource candidate.
- `Roster.dat` - 675-byte roster/save data candidate.
- `Documentation` - manual and keyboard reference scans.
- `dosbox_windows` - bundled DOSBox launch environment.

## Initial Format Observations

- `St.exe` begins with a normal MZ header. Its size is close enough to `GB.EXE` to make executable-level comparison worthwhile.
- Steel Thunder assets are mostly loose named files, while Gunboat's shipping data is mostly combined into `DATAA.DAT`, `DATAB.DAT`, and indexed by `DATAC.DAT`.
- The Steel Thunder `Readme` confirms normal install/run commands and ends with Tom Loughry's sign-off.
- `Dat1.dat` starts as low-range byte values, suggesting table/map data rather than an MZ executable or obvious compressed stream.
- `Title.mpp` starts with repeated `07 FF` pairs and small command-like byte runs, suggesting a custom packed image or drawing stream.
- `Mp1.ppk` starts with a compact header-like sequence followed by repeated command bytes, suggesting a different packed visual/data format from `.mpp`.
- The 224-byte `*colr.bin` files are strong palette/color-table candidates and may help interpret Gunboat palette records.

## Useful Cross-Checks For Gunboat

1. Compare strings and loader code between `St.exe` and unpacked `GB.EXE`, especially file open calls and extension handling for `.mpp`, `.ppk`, `.bin`, and `Dat*.dat`.
2. Use Steel Thunder's loose asset names to infer what kinds of files Gunboat may have packed into `DATAA.DAT` and `DATAB.DAT`.
3. Test whether Gunboat's working Test Drive 3-style `.LZ` decoder is relevant only to Gunboat/Test Drive-era assets or whether any Steel Thunder streams share the same codec family.
4. Build quick viewers for `.mpp`, `.ppk`, and 224-byte color tables, then compare rendered output against the Steel Thunder manual screenshots.
5. Treat Steel Thunder's `Objdata.bin`, `Text.bin`, and `Dat*.dat` as candidate guides for Gunboat mission/object/text structures.

## Initial Hashes

```text
GB.EXE     905CE2A05F5E22E45E69A01F99C942000B083A721CB5EB9A0D34F9D12BFF7DE7
St.exe     826E3E17BA64632FF685343800F6D6C602436A8D5DDF08D9C262B80C42B6DE46
DATAA.DAT  E16FF3357263635B7D454986D13C60B523005BA3732E20F9A4FCFCCC1012C074
Dat1.dat   6090DEEEC83C8274E25B3729D1F85D5F293BE4027B27A70900AD9A17B82C3EFF
DATAB.DAT  68A6F62915E73CDF38BFD8D2B45FACB5ABE382A7228DBBAE2177FF040D3BB71C
Dat2.dat   156C0B87DE16D481AEE4DA33E5E23626F020AC3E0FE6FBEDF04618A953C9CFD4
```
