# Original weapon routines

`src/original_weapons.cpp` translates the original station firing routines,
reload timers, muzzle flash counters, projectile allocation/aim calculations,
flight timers and the isolated object damage transition to native C++.

## Evidence

- **4,000 original-code comparisons**, including 3,436 shots, matched firing
  state and all 32 projectile slots exactly. Cases include disabled mounts,
  all loadouts, non-ready reload states, every heading quadrant, sub-headings,
  aim clamps, pitch correction, coordinate wraparound, a full projectile pool,
  reload/flash updates and frozen projectile updates.
- **10,240 original-code comparisons** matched object kind, object flags and
  damage sound triggers for all 256 classification bytes, five weapon IDs and
  eight existing damage levels. These isolate the damage transition from
  terrain changes, secondary explosion allocation and mission callbacks.
- The development oracle uses Unicorn. The game uses only the C++ functions
  and original data tables; it does not execute a CPU emulator.

Reports are `out/original-weapons-verification.json` and
`out/original-damage-verification.json`. The corresponding scripts and native
trace harnesses are in `tests/verify_original_weapons.py`,
`tests/original_weapons_trace.cpp`, `tests/verify_original_damage.py`, and
`tests/original_damage_trace.cpp`.

## Interface and timing

`gb::original::Weapons` reads the original aim limits, sine table and damage
matrix from the unpacked executable. `WeaponState` stores original integer
values. `fire(state, station)` returns a `FiredShot` or no shot; stations are
the original IDs **2, 3 and 4**. Call `OriginalAudio::play(shot.sound)` when a
shot is accepted.

`fire()` represents one original input/AI firing request. Its caller must
preserve the original request cadence. It does not impose a new time-based
cooldown. `reload_tick()` corresponds to image 0xAE89, called inside the
original simulation iteration loop at 0x11B6F. `flash_tick()` corresponds to
0x11388, called once by the outer loop at 0x11BAD. `projectile_tick()` matches
0xCA5C and returns impacts in the original descending slot order. Pass the
original DS:D8BC frozen condition to suspend projectile updates.

| Station | Original loadout | Weapon ID | Sound ID | Firing rule |
| --- | --- | --- | --- | --- |
| 2 / bow | 0 | 4 | 1 | Alternating barrels |
| 2 / bow | nonzero | 5 | 3 | Each accepted request |
| 3 | 0 | 3 | 8 | Reload ready sentinel 48 |
| 3 | 1 | 4 | 1 | Timing mode nonzero or odd original frame counter |
| 3 | >1 | 1 | 2 | Each accepted request |
| 4 | 0 | 2 | 8 | Reload ready sentinel 8 |
| 4 | nonzero | 1 | 2 | Each accepted request |

Firing decrements a ready reload counter once; simulation updates count it down
to zero, then restore its ready sentinel. These counters are **not ammunition**.
The translated firing paths contain no heat accumulator or ammunition
decrement. This observation does not prove the absence of every such mechanism
elsewhere in the game.

Mount condition bytes are original DS:D525/D526, D529/D52A, and D52E/D52F;
bit zero in either byte prevents firing. The station dispatcher also checks
the pilot/global control state before calling these routines; that is outside
`Weapons::fire()`.

## Original data and coordinates

| Original image address | Role |
| --- | --- |
| 0x9E84 | Station 4 firing |
| 0x9ED5 | Station 3 firing |
| 0x9F4D | Bow firing |
| 0xA072 | Projectile launch wrapper |
| 0xC692 | 129-word projectile sine table |
| 0xC91F | 32-byte weapon/armor damage matrix |
| 0xC93F | Allocate a free projectile slot, descending from slot 31 |
| 0xC957 | Integer aim, flight time and impact XY |
| 0xCA5C | Flight countdown and impact dispatch |
| 0xCB8C..0xCC5B | Object damage and destroyed variant selection |
| DS:D63E | Six original aim limits, indexed by weapon ID |

Projectile positions use the original world coordinate units. They are aimed
impact locations with an original flight countdown, not a new hitscan ray.
The full pool reuses slot zero. Object classification lives in the world B
asset at **0x73 + 2 × object kind** (runtime DS:541B + 2 × kind).

`hit(kind, flags, classification, weaponId)` applies the original matrix to
damage bits 0x38 in an object's flags. Some matrix entries OR a damage bit;
repeated hits with those entries do not add more damage. Reaching 0x38 selects
the original destroyed kind. Class 1 has a separate weapon/type rule.

## Still outside this module

Original target intersection tests, autonomous gunner decisions, enemy fire,
boat component damage, explosion allocation, terrain changes from destroyed
structures, rescue/scoring/radio callbacks and campaign persistence require
their own translations. A caller that uses new hit detection or mission rules
still has incomplete gameplay fidelity, even when these isolated functions
match the original exactly.

`tests/native_weapons_tests.cpp` supplies native headless checks for reloads,
continuous bow fire, full projectile pool behavior, disabled mounts, armor,
destroyed variants, impact order and frozen updates. Build it with
`src/assets.cpp` and `src/original_weapons.cpp`; pass the original DOS directory
as its first argument.
