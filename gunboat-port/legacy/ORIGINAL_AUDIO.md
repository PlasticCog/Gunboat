# Original PC speaker effects

`src/original_audio.cpp` is a native C++ translation of the original Gunboat
gameplay sound-effect player. It reads the original tuning table and thirteen
effect programs directly from the unpacked `GB.EXE` data already loaded by the
asset loader. It executes no x86 instructions at runtime.

## Verified scope

The DOS timer reload is **0x13b1 = 5041**, with a nominal PIT clock of
1,193,182 Hz: approximately 236.695 effect updates per second. The native PCM
renderer schedules these updates in sample time, independently of graphics
frame rate and SDL audio buffer size. The first update occurs at the next full
timer period after `reset()`; in DOS the phase at a particular trigger depends
on when the interrupt was installed.

The development test `tests/verify_original_audio.py` runs the original x86
routines in Unicorn as an oracle. It compares speaker enable, PIT divisor,
sequence ownership, pointers, counters and **all 214 bytes at DS:DA48..DB1D**
after every update. Tests cover all thirteen effects, seven throttle conditions,
effect interruptions, changing engine speed, and muting. Result:
**68,700 updates across 92 cases matched exactly**. The machine-readable report
is `out/original-audio-verification.json`.

`tests/native_audio_tests.cpp` separately verifies PCM independence from buffer
size, finite effect completion, engine looping and interruption, mute and reset.
It can produce `out/original-pc-speaker-effects.wav`, containing effects 0..12 in
order, two seconds each. Effect 6 is an infinite loop and is deliberately cut
at two seconds in this test recording.

## Integration

```cpp
gb::OriginalAudio audio(archive.exe);
audio.set_engine(portThrottle, starboardThrottle, engineSoundDisabled);
audio.play(originalEffectId);
audio.render(interleavedStereo, frameCount, sampleRate);
```

Call these functions on the same thread. `render` writes signed 16-bit stereo
PCM. `play` replaces the current sequence, as the original does. `set_engine`
changes the same six pitch bytes and tempo/loop bytes modified by the original
engine routine; it starts effect 6 only when no effect owns the sequencer.
Throttle inputs are the original eight-bit values from DS:B816/B817.
`set_muted(true)` silences and suspends the sequencer, matching the original
DS:007E path. `reset()` restores original executable defaults and silence.

### Audited original addresses

Addresses below are offsets in the unpacked EXE image; add 0x10000 for the
development oracle's physical address.

| Address | Role |
| --- | --- |
| 0x12EF5 | Trigger effect, ID 0..12; all effects use the same sequence owner |
| 0x12FBB | Timer update |
| 0x13188 | Channel state transfer |
| 0x13294 | Effect command parser |
| 0x13342 | Original PIT note divisor lookup |
| 0x133F3 | Note duration and tie state |
| 0x13525 | Tempo, gating and loop commands |
| 0x13622 | Raw pitch command |
| 0x136D0 | PC speaker/Tandy initialization |
| 0x137B7 | PIT timer reload 0x13B1 |
| 0x0CE59 | Live engine effect program updates |
| DS:DA58 | Twelve original PC speaker note divisors |
| DS:DB1E | Thirteen effect program pointers |

Verified trigger callsites: effects 1, 2, 3 and 8 are selected by the player
weapon routines at 0x9E84, 0x9ED5 and 0x9F4D; effect 6 is the engine routine.
Other callers include 4 at 0xB148/0xCD24; 5 at 0xFD17; 7 at 0xBEC5/0xC50B;
9 at 0xC57E; 10/11 at 0xC4BB; and 12 in menu/pause handling. Names for these
events should follow the gameplay caller translation, rather than be guessed
from the sound alone.

## Remaining fidelity limits

- This translates the **PC speaker gameplay path**. Tandy sound, the separate
  title-music sequencer, AdLib driver and CMS driver are not implemented here.
- Tests prove identical sound programs, digital tuning and sequencing for the
  covered cases. They do not prove equivalence of physical speaker acoustics,
  an analog filter, exact PIT counter reload transients, or the entire game's
  event timing. PCM currently uses an ideal square wave.
- Correct effect triggering depends on completing the original weapons,
  damage and mission routines. This module does not make replacement gameplay
  rules faithful by itself.
- The secondary legacy sequence at DS:DAFC is not used by the thirteen
  gameplay effects tested here and is not translated.

## Reproduce

Use UCRT64 GCC from `C:\msys64\ucrt64\bin` on PATH:

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -Igunboat-port/src gunboat-port/tests/original_audio_trace.cpp gunboat-port/src/original_audio.cpp gunboat-port/src/assets.cpp -static -o gunboat-port/out/original_audio_trace.exe
python gunboat-port/tests/verify_original_audio.py
g++ -std=c++17 -Wall -Wextra -Wpedantic -Igunboat-port/src gunboat-port/tests/native_audio_tests.cpp gunboat-port/src/original_audio.cpp gunboat-port/src/assets.cpp -static -o gunboat-port/out/native_audio_tests.exe
gunboat-port/out/native_audio_tests.exe "Original DOS version" gunboat-port/out/original-pc-speaker-effects.wav
```

Python and Unicorn are needed only to run the development oracle comparison.
