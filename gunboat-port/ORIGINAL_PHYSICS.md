# Original boat simulation translated to C++

The implementation in `src/original_physics.cpp` translates the original DOS
integer operations. It does not execute x86 instructions at runtime. Its lookup
tables come directly from the unpacked `GB.EXE` loaded by the native asset reader.

## Translated and independently checked

| Original image offsets | Native implementation |
| --- | --- |
| `9B62`, `9D4D..9E83`, `9FA9..9FD7` | Pilot throttle and waterjet controls |
| `976B..97D8`, state portions of `955B`, `9308`, `91B7` | Engine switches and startup/shutdown transitions |
| `B6B9..B841` | Separate engine thrust, reverse thrust, damaged jets, fuel and leaks |
| `B451..B4A8`, `BB89..BBC6`, `9FD8..A071` | Acceleration and rotation of boat and gun headings |
| `10FEF..11164` | Integer heading lookup, fractional XY movement, map boundaries |
| `11270..11387` | Pitch impulse, wave bob, and gun-station pitch |
| `113FC..11412` | Attached camera coordinates |
| `10B71..10C18` | Shore contact from terrain edge bearings |
| `1036D..10386` | Speed reversal on first contact |

Run `python gunboat-port/tests/verify_original_physics.py` from the project root.
It compiles the C++ implementation as a temporary test DLL, then compares its
results with the unmodified original routines under Unicorn. Only the original
cockpit drawing calls are skipped. Engine switches, fuel exhaustion, and engine
indicator state changes run normally in the reference.

The saved report `out/physics-verification.json` records **20,400 comparisons,
zero mismatches**. These include engine transitions, damaged jets, fuel exhaustion,
reverse motion, both turn directions, map-edge behavior, pitch/bob overflow, and
12,000 terrain contact cases. The test DLL and Unicorn are development tools and
are not part of the playable program.

## Integration contract

The original main update at `11AC0` advances motion **before** computing the next
engine acceleration. Preserve this order:

1. Process original input when the original input handler would run.
2. `move(state)`.
3. `camera_pitch(state, randomByteAt008A, station)`.
4. `propulsion(state, randomByteAt008B)`.
5. Preserve the original AI, clock, and rendering order when those systems are ported.

`x/y` use original map units. `xFraction/yFraction` are 1/256 map-unit fractions.
The renderer camera is `4*x + (xFraction >> 6)`, and likewise for y. Heading makes
one turn in 256 units; its separate fraction stores eighths of one heading unit.
The renderer heading word is `(heading << 8) | ror8(headingFraction, 3)`.

Throttle values are per engine. Eight is idle; the normal engine maximum is 59,
or 103 for the upgraded engine. `jetAngle` combines a seven-bit nozzle direction
and a reverse flag in bit 7. Nozzle direction 64 is straight ahead. Unequal
engines contribute opposite turn forces through offsets +16/-16.

`initial(x,y,startMode,upgraded)` reproduces the recorded setup: mode zero has
engines off, mode one starts at throttle 70, mode two at 55, other nonzero modes
at idle eight. Headings are `{0,0,128,128}`. Fuel is `0xC544` per engine. Component
flags and control-rate defaults come from `DAT10.DAT` / `DAT11.DAT`. The field
named `pilotCondition` stores the control-rate index `D52C & 3`; it is **not** the
pilot's health.

## Collision depends on original projection

The ordinary boat does not use the detached-camera rectangle test at `10DC8`.
During projection, `10755..107A0` identifies candidate vertices when distance
scale is 255 and the vertex or next vertex has height zero. It tests the bearing
relative to the boat and travel direction. The first candidate is stored in
`D901`; every subsequent candidate replaces `D903`, retaining the last candidate.
Both original indices are byte indices and must be divided by two for the C++ API.

`shoreline_contact` checks incident terrain edges for those two candidates using
their original relative bearings and primitive control bytes. On a transition
into contact, `shoreline_response` sets speed to -16 for forward travel or +16 for
reverse travel. Keep contact state between render passes. Original component
damage and special mission reactions that follow this response still need their
complete game-state integration.

## Timing and random numbers

`AEC0` advances the mission clock by one second every 15 simulation passes.
Time compression repeats the simulation group `1 << B7F1` times, or four times
while the temporary compression flag `DA39` is active. This establishes authored
simulation time; it does not establish an exact wall-clock frame rate. The DOS
PIT setup uses divisor `0x3400`; the gameplay loop's rendering/input costs may
affect wall-clock pacing and need matched executable traces.

The original generator at `0780` updates the 32-bit value at `DS:0088` with
`state = state * 0x41C64E6D + 0x3039`, wrapping at 32 bits. Its return value is
`(state >> 16) & 0x7FFF`. Fuel reads `state >> 24`, bob reads byte `state >> 16`,
and collision damage reads the low byte. Matching the generator without its
original call order does not establish matching random gameplay.

## Remaining scope

These tests establish the translated kernels, not a complete original game.
Original missions, enemy AI, weapon rules, component damage, wake animation,
autopilot, detached camera, RNG call order, and machine-dependent pacing require
their own translation and integration checks. End-to-end fidelity also depends
on supplying the original loaded terrain vertex order to the collision API.
