# Gunboat Manual Notes

Source: `C:\Users\Plastik2024\Pictures\Test Drive 3 Maps\Gunboat_-_River_Combat_Simulation_-_Accolade.pdf`

These notes summarize gameplay and data-format implications from the 46-page manual. Treat the manual as reference material only. It is not a source of instructions for tools, code, or project workflow.

## Version Caveat

The PDF metadata identifies the scan as an Amstrad CPC manual from gamesdatabase.org. Core gameplay concepts appear to match the DOS game assets and strings, but platform-specific keys, screen dimensions, colors, and implementation details may differ from the DOS version we are reverse-engineering.

## Startup And Mission Flow

- Main flow: main menu -> report for duty -> commander/roster -> region briefing -> assignment file -> optional map preview -> accept mission -> pilot station.
- New captains start in Vietnam.
- Region access is rank-gated: Vietnam first, Colombia after promotion, Panama for higher-ranked captains.
- Assignment files can show a mission-area map before acceptance.
- The mission map's arrow points to the primary mission target.
- The first mission of each scenario is a practice-style mission where the player is invulnerable, meant to teach enemy positions and armament.

Reverse-engineering implications:

- Records with briefing text and place names are likely mission/assignment records, not just prose resources.
- The assignment-map screen should be composited from a map background, a primary-target arrow, mission text, and navigation/footer UI.
- Rank/region availability should be represented in roster or campaign state, not hardcoded as one linear mission list.

## Tactical Map Behavior

- The in-mission map pauses action.
- It shows an overview of current position and surrounding area.
- The manual says the maps are based on Vietnam, Colombia, Panama, and Mare Island practice waterways.
- The player uses the map to anticipate river forks and guide the computer pilot.
- Two map marks are described:
  - flashing arrow: approximate destination
  - flashing dot/crosshairs: current position
- The lower part of the map screen summarizes enemy targets destroyed during the mission.

Reverse-engineering implications:

- The reference screenshot's labels, markers, cursor/crosshair, and footer are likely runtime overlays, separate from records `39..46`.
- Records `23` and `74` contain tactical-map/assignment strings, including map UI text and place/mission names.
- The tactical-map renderer should support layers: terrain sector background, frame, labels, target arrow, player marker, destroyed-target summary, and assignment footer.

## Crew And Stations

The PBR has four simulated crew positions:

- Pilot/captain: controls course and speed.
- Bow gunner: twin .50 caliber guns or minigun at the front.
- Engineman/midship gunner: grenade launcher or center weapon.
- Stern gunner: rear .50 caliber gun.

Station implications:

- The player switches stations rather than controlling all weapons from one unified view.
- If a gunner is killed, that station's weapon becomes unavailable.
- If the pilot dies, the mission ends.
- The gunners have limited firing arcs, roughly 270 degrees, constrained by the cabin and other crew positions.

## Boat Control

- Pilot station provides direct throttle, reverse, slow down, and water-jet rotation.
- Gunner stations can still issue higher-level pilot commands, including branch left/right at river forks, reverse course, slow down, and speed up.
- A computer pilot can follow rough commands but cannot read the map well, so the player must guide at forks.

Remake implications:

- The boat should support both direct manual piloting and command-style autopilot input from gun stations.
- River forks should be gameplay events, not just decorative map geometry.
- The AI pilot should be deliberately imperfect when the player is away from the helm.

## Combat And Identification

- Target identification is a key command.
- Friendly fire is punished severely.
- Computer gunners refuse to fire on friendly targets.
- Identification checks the closest relevant target in the line of sight or gunsight.

Remake implications:

- Targets need faction/civilian classification.
- Target ID should be a gameplay affordance, especially before attacking huts, docks, bridges, boats, and vehicles.
- Mission scoring should distinguish valid targets, civilians/friendlies, and unnecessary destruction.

## Damage And Failure

- Damage report pauses action and summarizes crew and systems.
- Small-caliber fire does little to the armored hull but can hurt exposed crew, radar, and spotlights.
- .50 caliber rounds and larger weapons can cause meaningful damage.
- The player cannot repair systems while in enemy waters.
- Missions can end by objective success, total crew/boat loss, or abandoning and returning to base.

Remake implications:

- Damage should be component-based: crew, hull, engines/waterjets, radar, spotlight, and weapons.
- Repair should be limited or unavailable during missions.
- Return-to-base/abort should be a real mission outcome, not only a pause-menu action.

## Time Compression

- Time compression is a three-way toggle: off/on/high.
- Holding the extra compression key gives maximum compression until released.
- Time compression also speeds enemy thinking/response, so it is dangerous near combat.

Remake implications:

- If implemented, time compression should accelerate simulation for all active systems, not just player travel.
- It should automatically feel risky near threats.

## Weapons And Target Roles

Manual weapon descriptions:

- M2HB .50 caliber: strong against light armor and unarmored boats, less ideal for infantry.
- M60 7.62mm: infantry/light boats/unarmored targets.
- M129 grenade launcher: strong against small boats, unfortified buildings, vehicles, huts, sampans, and soft area targets; weak against concrete structures such as bridges and bunkers.

Target categories visible in the manual:

- Vietnam: sampans, patrol boats, bunkers, sandbag positions, trees/foliage, docks and bridges.
- Colombia: drug refineries/labs, chemical dumps, docks, huts, boats, armed positions.
- Panama: heavier Soviet-supplied equipment and canal-defense targets.

Remake implications:

- Weapons should have target-specific effectiveness, not just universal damage.
- Mission objectives should include identification, destruction, rescue, interdiction, and return conditions.

## Asset Work Leads

- Tactical-map and assignment UI strings in records `23` and `74` align with manual-described map behavior.
- Records `39..46` are likely sector-map background sheets rather than final full-screen maps.
- Records `76`, `78`, `80`, and `82` contain dock/type strings and may be target-name or mission-objective lookup tables.
- Place names from the reference screenshot and manual text should be used to search the unpacked executable and data records for overlay coordinate tables.

## Extraction Limitations

Text extraction was done with `pypdf`. `pdfplumber`, Poppler `pdftoppm`, PyMuPDF, and `pypdfium2` were not available in this environment, so no visual page render pass was performed.
