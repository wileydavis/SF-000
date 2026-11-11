# SF-000

A standalone Mac instrument for live hypnotic techno: eleven lanes, a step sequencer on each,
snapshots, and sixteen knobs that morph between them. It plays the sound code of
[SF Live Suite](https://github.com/wileydavis/SF-Live-Suite) inside one app, with no Ableton and
no plugins.

This is the first build. `DECISIONS.md` lists every choice made while building it that the design
did not already settle.

## Build and run

Needs the Xcode command line tools and CMake (the same as the suite).

```
./build.sh
open build/SF000_artefacts/Release/SF-000.app
```

The first build clones JUCE 8.0.4 into `./JUCE` and takes a few minutes.

First run:

1. Press **AUDIO** (white key, top bar) and pick the interface and its outputs. The choice is kept.
2. Press **RUN** (or the space bar). Every lane has a starting pattern.
3. Press **F** for full screen at 1728 x 1117; **F** or **Esc** leaves it.

## Playing it

| Do this | To |
| --- | --- |
| Drag a knob up or down | turn it (hold Shift for fine; double-click for its starting value; the wheel works too) |
| Set GLIDE, then turn a knob | the sound travels there over the GLIDE time: the knob turns green and walks, with a white mark where it is going. It sets off on the next beat while the clock runs. Turn it again on the way and it carries on from where the sound is. OFF: at once |
| Click a lane's name | make it the active lane: the inserts and the sequencer strip show it |
| **C** on a lane, an insert or a return | open its CONFIG panel (see below). **C** again, **X** or Esc closes it |
| **M**, **S** | mute and solo: a lane, an insert (M passes it by, S plays only what it adds), a return |
| Click an insert's name | step that slot through DELAY, REVERB, CONVOLUTION, DISTORTION, COMPRESSOR, RADIO, EMPTY |
| Click a step | unlit: light and select it (velocity 75, a whole step of gate). Lit: select it. Lit and selected: put it out, and it forgets everything it held |
| Drag a step up or down | velocity |
| Drag a step left or right | gate. It stops at the step's edge. Past the edge it runs on through unlit steps, and a little past the next lit step it is legato into it (a tie, on the same note) |
| RATCHET, NOTE, OCTAVE | change the selected step (NOTE and OCTAVE on lanes that take notes) |
| < NUDGE, NUDGE > | move the whole pattern one step |
| Drag the LENGTH knob | 1 to 128 steps. Click a page above the steps to see its 16 |
| RANDOM, EUCLID | write a pattern at once; while the key is lit, HITS, ROTATE, ACCENTS and SEED rewrite it |
| SNAPSHOT | keep everything on the main screen as a new snapshot |
| Turn a snapshot knob | push the rig from where it is toward that knob's snapshots, over the GLIDE time |
| Recall a snapshot | sound knobs travel over the GLIDE time; step patterns change on the next bar line while the clock runs; the rest changes at once |
| Double-click a snapshot knob | recall its nearest snapshot as the new base scene |
| LIBRARY | the snapshot library: tap to recall, rename, hold DELETE |
| MACROS | the macro designer: pick a knob, tap snapshots to add them in sweep order |
| Drag TEMPO or KEY | 60 to 200 bpm; the key every pitched lane follows (parts are written in C) |
| Drop an audio file on a lane | load it, where the lane's model takes a sample (VOICE takes a phrase). Its CONFIG panel opens to show the waveform |
| Drop an audio file on an open CONFIG panel | it goes to whatever that panel is for: the lane, the insert, or the return. An impulse response dropped on SPACE's CONFIG (or on the RETURNS panel) makes SPACE a convolution reverb of that place |
| Click the clip lamps above MIX | clear them |
| Arrow keys | up and down: the active lane. Left and right: the step page |

The screen never asks a question while it plays. PANIC silences every voice and tail.

### CONFIG: where a sound is set up

The main screen is for playing. CONFIG is for building the sound: it slides in from the left over
two columns, and the top three rows move so that the panel being set up (the lane, the insert, the
returns) stands right beside it. The six columns go round: what leaves one edge of the screen comes
back at the other. The sequencer stays where it is.

- **A lane's CONFIG** holds everything its twelve knobs do not: MODEL and LAYER MODEL, the layer
  crossover (SPLIT, SPLIT HZ), sample start, end and fades, and the rest of the module's
  parameters. At the top is the module's own display: the waveform with its in and out points, and
  on VOICE the phrase with its cue points. The display takes the mouse.
- **An insert's or a return's CONFIG** holds that effect's remaining parameters and its display.
- **SPACE (return B) is one of two reverbs.** MODEL, first in its CONFIG, chooses REVERB (the
  algorithmic one it starts as) or CONVOLUTION. The convolution holds eight places; PLACE (or a
  click on the numbered boxes in its display) chooses one, and a dropped file fills the place it
  is on. An empty place is the built-in room. Each reverb keeps its own four SPACE knobs. Which
  reverb SPACE is, and the files it was given, belong to the rig: they are kept in the session and
  a snapshot recall leaves them alone.
- **An insert belongs to snapshots, whole.** A CONVOLUTION insert takes files the same way (drop
  them on its CONFIG), and a snapshot keeps the effect, its knobs, its CONFIG and those files. A
  recall puts it back with its room already loaded.
- A menu shows its current choice under the knob. The parameter last touched shows its value in
  the panel's header. Double-click returns a knob to where it started. The wheel scrolls if there
  are more rows than fit.
- What CONFIG sets is stored in snapshots with everything else, except which reverb SPACE is.

### Snapshots that come with it

CLEAN SLATE (always first in the library) puts every knob where the rig starts it, with no steps
and no inserts. INIT is the rig as it first comes up, patterns and all. The other five are the
pushes behind the ENERGY, BUILD, ACID, DIST and WARP knobs.

### MIDI and the E16

Open AUDIO (now AUDIO AND MIDI) to see what is connected. Nothing needs choosing.

- **Notes in.** Every MIDI input is listened to. A lane's number is its channel (1 KICK to 11 VOICE),
  so the OXI One plays the lanes as it played the Live tracks. Notes and the lane's own steps sound
  together.
- **Clock out.** While it runs, START, 24 ticks a beat and STOP go to every output that is not the
  E16. The OXI One's settings and its project for SF-000 are in `controllers/oxi-one/`. CLOCK LEAD is how long the follower takes to answer: play a plain
  sixteenth pattern from it into a lane, read NOTES IN LAND x MS LATE in the panel, and put that
  number on CLOCK LEAD. With the clock on, RUN waits two blocks and LEAD (a few ms) before the first
  sound. SF-000 does not follow an outside clock yet.
- **The E16.** Load `controllers/e16/SF-000.oxie16` (see `controllers/e16/README.md`). Pages 1-11
  are the lanes: the twelve knobs where the screen has them, then SEND A, SEND B, LOW and HIGH. Press
  knob 16 and it is MID (its ring turns pink); press again for HIGH. Page 12 is the sixteen snapshot
  knobs; a press recalls that knob's nearest snapshot. Turning to a lane's page makes it the active
  lane on the screen. The rings follow the rig, and a travelling knob's ring walks green.

## Checks

```
./build.sh test                                   # build, then the offline audio and timing checks
build/.../SF-000 --test --wav out.wav             # the same, keeping 16 bars of the full rig
./build.sh perf                                   # the engine's share of real time and what the screen costs, on this machine
build/.../SF-000 --audit                          # does every lane knob change the sound? (slow)
build/.../SF-000 --uitest                         # clicks and drags every control on every screen
build/.../SF-000 --miditest                       # notes in, the E16's knobs and rings, the clock out, with made-up messages
build/.../SF-000 --e16 controllers/e16/SF-000.oxie16   # write the E16 scene (snapshot knob names from your session)
python3 tests/e16_scene_test.py                   # the scene's shape and addresses, and its script on a mock E16 (pip install lupa)
build/.../SF-000 --shot main.png --scale 2        # draw a screen: --page library|macros, --lane N, --cfg lane|insert|return|space, --space conv
```

### Load

Activity Monitor's figure is a share of one core (100 % is one of the Mac's cores busy all the time).
It is the engine plus the screen. AUDIO AND MIDI shows the engine's share on its own (ENGINE: n %),
and `./build.sh perf` measures both. The screen redraws only what moved: a travelling knob, the
meters when a bar has moved, the playing step.

## What is here

| Path | What |
| --- | --- |
| `Source/Rig.h` | the rig: each lane's module, fixed model and settings, and which knob drives what |
| `Source/Engine.*` | clock, lanes, inserts, returns, lane EQ, master chain, delay compensation |
| `Source/Sequencer.h` | patterns, ties, ratchets, the generators, MIDI on the sample |
| `Source/Session.*` | base scene, snapshot library, snapshot knobs and their morph, the saved file |
| `Source/Screen.*` | the three screens |
| `Source/Midi.*` | notes in, the E16 (addresses, rings, the scene file), the clock's sender |
| `controllers/e16/` | the E16 scene and its script |
| `controllers/oxi-one/` | the OXI One MkII project, and a checker for your own |
| `Source/Main.cpp` | the app, the audio device, the command-line tools |
| `suite/` | SF Live Suite's sound code, copied (see `suite/PORTING.md`) |

State is kept in `~/Library/Application Support/SF-000/session.json` (snapshots, knobs, the base
scene), `audio.xml` (the interface) and `midi.json` (clock on or off, CLOCK LEAD). Each time the app
opens it copies the session as it found it into `backups/` beside it and keeps the last twenty: to go
back, quit and copy one over `session.json`. Delete `session.json` to start again from the factory rig.

## Not built yet

- MIDI: no MIDI learn for other controllers, no following an outside clock, no notes out.
- The E16: returns, inserts and the master are not on it; 7-bit knobs (128 places).
- Recording of audio, MIDI and knob moves.
- The library's sound map and audio previews; meaning filters; snapshot knob pages beyond the first.
- PA and cue outputs with room EQ; the second display mode for a dark stage.
- Menus, switches and inserts changing on a beat or bar line at a recall (they change at once; step patterns wait for the bar).
- Crash recovery beyond the saved session; the 8-hour soak test.
