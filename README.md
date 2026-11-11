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
| Click an insert's name | step that slot through DELAY, REVERB, CONVOLUTION, DISTORTION, COMPRESSOR, RADIO, EMPTY |
| Click a step | unlit: light and select it. Lit: select it. Lit and selected: put it out |
| Drag a step up or down | velocity |
| Drag a step left or right | gate. Past the step's edge it runs on: a tie through unlit steps, legato into a lit one |
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
| Drop an audio file on a lane | load it, where the lane's model takes a sample (VOICE takes a phrase) |
| Arrow keys | up and down: the active lane. Left and right: the step page |

The screen never asks a question while it plays. PANIC silences every voice and tail.

## Checks

```
./build.sh test                                   # build, then the offline audio and timing checks
build/.../SF-000 --test --wav out.wav             # the same, keeping 16 bars of the full rig
build/.../SF-000 --audit                          # does every lane knob change the sound? (slow)
build/.../SF-000 --uitest                         # clicks and drags every control on every screen
build/.../SF-000 --shot main.png --scale 2        # draw a screen: --page library|macros, --lane N
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
