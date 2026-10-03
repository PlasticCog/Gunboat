#pragma once
// Quicksave and quickload, an enhancement: the whole game as it stands, kept in memory and in
// quicksave.sav in the settings folder, and put back on request. Two saves per mission; loading goes
// back to the latest save, as often as wanted; a new mission forgets it and gives two saves again,
// and so does the end of its mission. Not in the title's demo; VGA only (the other cards keep
// picture state outside mem[]). After a restart the game offers the file's save before the title
// (game_main: host_resume); resumed, its mission goes on, and after it the game goes on as after
// that mission.
//
// The game's state is mem[] (the port keeps all of it there) and, outside it, the emulated machine:
// the VGA palette and display start, the speaker and the AdLib chip (host_machine_save). Both are
// taken and put back at one safe point, the top of a pass of the mission loop (host_mission_pass,
// a PORT notification of mission_run), where the game is between two frames; the keys and buttons
// only ask for it. After a load the presentation layer forgets what it had (the captured frames,
// the debris, the AdLib effects' own copies).
#include <string>

namespace gb {

void quicksave_install();
// The requests (Ctrl+S / Ctrl+L, the Ctrl+H panel's buttons): done at the mission loop's next pass,
// or refused with a note (outside a mission, in the demo, none left, nothing saved).
void quicksave_request_save();
void quicksave_request_load();
// For the Ctrl+H panel.
bool quicksave_in_mission();
int quicksave_saves_left();
bool quicksave_have();
std::string quicksave_time();  // the mission clock of the save, "09:31:47"
// The presentation layer's hooks: a note to show, and its own reset after a load.
void quicksave_set_note(void (*note)(const char *text));
void quicksave_set_after_load(void (*after_load)());
// The resume question at start-up: true while it is asked (the save described in `text`); the answer.
bool quicksave_prompt(std::string &text);
void quicksave_answer(bool resume);

} // namespace gb
