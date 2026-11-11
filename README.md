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
| Double-click a snapshot knob | recall its nearest snapshot as the new base scene |
| LIBRARY | the snapshot library: tap to recall, rename, hold DELETE |
| MACROS | the macro designer: pick a knob, tap snapshots to add them in sweep order |
| Drag TEMPO or KEY | 60 to 200 bpm; the key every pitched lane follows (parts are written in C) |
| Drop an audio file on a lane | load it, where the lane's model takes a sample (VOICE takes a phrase). Its CONFIG panel opens to show the waveform |
| Click the clip lamps above MIX | clear them |
| Arrow keys | up and down: the active lane. Left and right: the step page |

The screen never asks a question while it plays. PANIC silences every voice and tail.

### CONFIG: where a sound is set up

The main screen is for playing. CONFIG is for building the sound: it slides in from the left over
two columns and moves the top three rows over; the sequencer stays where it is.

- **A lane's CONFIG** holds everything its twelve knobs do not: MODEL and LAYER MODEL, the layer
  crossover (SPLIT, SPLIT HZ), sample start, end and fades, and the rest of the module's
  parameters. At the top is the module's own display: the waveform with its in and out points, and
  on VOICE the phrase with its cue points. The display takes the mouse.
- **An insert's or a return's CONFIG** holds that effect's remaining parameters and its display.
- A menu shows its current choice under the knob. The parameter last touched shows its value in
  the panel's header. Double-click returns a knob to where it started. The wheel scrolls if there
  are more rows than fit.
- What CONFIG sets is stored in snapshots with everything else.

### Snapshots that come with it

CLEAN SLATE (always first in the library) puts every knob where the rig starts it, with no steps
and no inserts. INIT is the rig as it first comes up, patterns and all. The other five are the
pushes behind the ENERGY, BUILD, ACID, DIST and WARP knobs.

## Checks

```
./build.sh test                                   # build, then the offline audio and timing checks
build/.../SF-000 --test --wav out.wav             # the same, keeping 16 bars of the full rig
build/.../SF-000 --audit                          # does every lane knob change the sound? (slow)
build/.../SF-000 --uitest                         # clicks and drags every control on every screen
build/.../SF-000 --shot main.png --scale 2        # draw a screen: --page library|macros, --lane N, --cfg lane|insert|return
```

## What is here

| Path | What |
| --- | --- |
| `Source/Rig.h` | the rig: each lane's module, fixed model and settings, and which knob drives what |
| `Source/Engine.*` | clock, lanes, inserts, returns, lane EQ, master chain, delay compensation |
| `Source/Sequencer.h` | patterns, ties, ratchets, the generators, MIDI on the sample |
| `Source/Session.*` | base scene, snapshot library, snapshot knobs and their morph, the saved file |
| `Source/Screen.*` | the three screens |
| `Source/Main.cpp` | the app, the audio device, the command-line tools |
| `suite/` | SF Live Suite's sound code, copied (see `suite/PORTING.md`) |

State is kept in `~/Library/Application Support/SF-000/session.json` (snapshots, knobs, the base
scene) and `audio.xml` (the interface). Delete `session.json` to start again from the factory rig.

## Not built yet

- MIDI: no controller input, no E16 rings, no MIDI learn, no clock in or out.
- Recording of audio, MIDI and knob moves.
- The library's sound map and audio previews; meaning filters; snapshot knob pages beyond the first.
- PA and cue outputs with room EQ; the second display mode for a dark stage.
- Patterns changing on the bar line when a snapshot is recalled (they change at once).
- Crash recovery beyond the saved session; the 8-hour soak test.
