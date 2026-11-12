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
4. Press **D** for the dark instrument and **D** again for the light one. **L** changes between the
   lit skin (how it starts) and the flat drawing. The look is kept between runs. See The look, below.

## Playing it

| Do this | To |
| --- | --- |
| Drag a knob up or down | turn it (hold Shift for fine; double-click for its starting value; the wheel works too) |
| Set GLIDE, then turn a knob | the sound travels there over the GLIDE time: the knob turns green and walks, with a white mark where it is going. It sets off on the next beat while the clock runs. Turn it again on the way and it carries on from where the sound is. OFF: at once |
| Click a lane's name | make it the active lane: the inserts and the sequencer strip show it. Its display is the one lit up |
| **C** on a lane, an insert or a return | open its CONFIG panel (see below). **C** again, **X** or Esc closes it |
| **M**, **S** | mute and solo: a lane, an insert (M passes it by, S plays only what it adds), a return |
| Click an insert's name | step that slot through DELAY, REVERB, CONVOLUTION, DISTORTION, COMPRESSOR, RADIO, EMPTY |
| Click a step | unlit: light and select it (velocity 75, a whole step of gate). Lit: select it. Lit and selected: put it out, and it forgets everything it held |
| Drag a step up or down | velocity |
| Drag a step left or right | gate. It stops at the step's edge. Past the edge it runs on through unlit steps, and a little past the next lit step it is legato into it (a tie, on the same note) |
| RATCHET, NOTE, OCTAVE | change the selected step (NOTE and OCTAVE on lanes that take notes) |
| < NUDGE, NUDGE > | move the whole pattern one step |
| Drag the LENGTH knob | 1 to 128 steps. Click a page above the steps to see its 16 |
| **M** in the sequencer strip | mute the active lane's own sequencer. Its steps stay, drawn grey, and go unplayed; the note it was holding is let go. Notes from MIDI in still sound on the lane. Press again and it comes back on its next lit step |
| RANDOM, EUCLID | write a pattern at once; while the key is lit, HITS, ROTATE, ACCENTS and SEED rewrite it |
| **REC** (top bar) | press: start a take. Its key turns red and its name becomes the time it has run. **Hold** the key to end it. See Recording, below |
| SNAPSHOT | keep everything on the main screen as a new snapshot. If the clock is running, the last four bars are kept with it as a clip and it is given a profile: nothing to name or tag (see The library, below) |
| Turn a snapshot knob | push the rig from where it is toward that knob's snapshots, over the GLIDE time |
| Recall a snapshot | sound knobs travel over the GLIDE time; step patterns change on the next bar line while the clock runs; the rest changes at once |
| Double-click a snapshot knob | recall its nearest snapshot as the new base scene |
| LIBRARY | the snapshot library: tap to recall, rename, hold DELETE. With **CUE** lit, a tap plays that snapshot's bars in the headphones instead, and **RECALL** takes it to the set |
| MACROS | the macro designer: pick a knob, tap snapshots to add them in sweep order |
| Drag TEMPO or KEY | 60 to 200 bpm; the key every pitched lane follows (parts are written in C) |
| Drop an audio file on a lane | load it, where the lane's model takes a sample (VOICE takes a phrase). Its CONFIG panel opens to show the waveform |
| Drop an audio file on an open CONFIG panel | it goes to whatever that panel is for: the lane, the insert, or the return. An impulse response dropped on SPACE's CONFIG (or on the RETURNS panel) makes SPACE a convolution reverb of that place |
| Click the clip lamps above MIX | clear them |
| Arrow keys | up and down: the active lane. Left and right: the step page |
| **I** | INFO: every control MIDI reaches shows its CC number, a panel its channel, and a control the mouse rests on says what it does. **I** again puts it away |
| **H** | PHONES: headphones on the one pair of outputs. While a cue plays, the set is in the left ear and the cue in the right (see The library, below) |
| **D** | the dark instrument, or the light one |
| **L** | the lit skin, or the flat drawing it replaced |

The screen never asks a question while it plays. PANIC silences every voice and tail.

### The look: a lit skin, after the 3D model

The screen is drawn as the instrument in the 3D model: the grey case, a cream plate standing in
each pocket, dark caps in rings of sixteen lights, keys that stand up, displays set into the
plates. Nothing has moved: every control is where it was and takes the mouse as before. This is a
first pass at it.

- **The sizes and heights are the model's.** Its module is 49.72 mm wide and the screen's is 272
  units, so 5.46 units to the millimetre, and the model's knobs, rings, keys and display fall on
  the screen's own places. From it: a plate stands 1.27 mm proud of the case; a big cap is 4.07 mm
  across and 5.08 mm tall in a ring 2.29 to 3.37 mm out, on a base 7.15 mm across (a small one
  3.03, 3.56, 1.60 to 2.36 and 5.0); a ring's lights stand 0.25 mm; a key 1.27 mm; the name plate
  1.27 mm with letters 0.25 mm over it. (The case's corners are a little tighter than the model's
  6 mm, to clear the name plate.)
- **One light, upper left and a little behind, high up.** What stands up casts a short shade down
  and to the right and is dim at its foot. The eye is a little in front of straight above, so a
  cap shows its front and the case shows its front edge at the bottom of the screen.
- **A knob's ring of lights says where it stands**, from six o'clock round, as on the E16. A
  whole segment is lit whole; the one the value stands in is as bright as the value is far into
  it. The colours are the ones the flat drawing had: white for a sound knob, red for decay or
  gate, density and filter, blue for level, sends and EQ, teal for a snapshot knob, green while it
  travels (with a white mark where it is going). A knob with a middle has a mark printed above it.
- **Displays.** A panel's name is a dark display with light letters. The active lane's is lit:
  pale, with dark letters. Readouts, the step pages and the meters are pale displays.
- **D** on the keyboard is the dark instrument: a graphite case, dark plates and keys, light
  print. The lights, the lit keys and the yellow keys are as they were. A pale display turns dark
  with light marks (the meters, the step pages, the readouts, a step key's bars), so the screen
  gives off far less light. Two things stay bright because they are what to find at a glance: the
  active lane's display, and in the library the base scene's name (there it is the lit name, where
  on the light instrument it is the black one). D again is the light instrument.
- **L** on the keyboard gives the flat drawing back, exactly as it was, and L again the skin. The
  dark instrument is the skin's: from the flat drawing, D goes to it.
- The look you leave it in is the look it opens in (`look.json`, beside the session).

The pictures are made when the window first shows and again when it changes size (it waits until
the size has held for a moment). `./build.sh perf` gives what both drawings cost on your Mac.

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

### The library: what a snapshot sounded like

A snapshot taken while the clock runs keeps its sound: the last four whole bars of the output, cut
on the bar lines, as a small Ogg file (about 80 KB) in `clips/` beside the session. From those bars
and from the rig itself it is given a profile, and its card in the library draws it:

| ON THE CARD | WHAT |
|---|---|
| the coloured block | as tall as its **energy** (the level before the master evens it out, how many hits a beat, how bright), as hot as it is **bright**: deep red for dark, through amber and yellow, to white |
| the sixteen bars | the spectrum's shape, 30 Hz to 16 kHz |
| the two rows of sixteen | where the hits fall in the bar: the top end above, the low end below |
| the eleven bars | how much of each lane there was |

**CUE** (in the library's strip) turns taps into listening: a tapped snapshot's four bars play in
the headphones, on outputs 3 + 4, looped. While the clock runs they play in time: on the rig's bars
and at the rig's tempo (so the pitch moves with the tempo's ratio to the clip's own). The set
itself is not touched. Tap the card again to stop, another to hear that one, **RECALL** to take the
one in the headphones to the set. Outputs 3 + 4 have to be ticked in AUDIO once.

**PHONES** (beside CUE, or **H** on any screen) is for playing on headphones with only one pair of
outputs. While a cue plays, outputs 1 + 2 carry the set in the left ear (both its sides, summed)
and the cue in the right; when the cue stops, the set is back in both, over 10 ms. With no cue
playing nothing is different. The AUDIO key at the top turns green and reads PHONES while it is
on. A recording, the meters and a snapshot's kept bars are the set itself whichever way it is.
It is off every time SF-000 opens, so it cannot be left on for a PA.

Also kept, and not drawn yet: weight (the low end's share), density, tone against texture,
movement from bar to bar, and how wet (the returns' share). `SF-000 --profiles` prints them for the
snapshots the rig comes with and some plain cases. A snapshot taken with the clock stopped, or
from before this build, has no sound kept: its card shows how busy each lane's pattern is, as before.

The bars kept are ones the rig held the snapshot's state through: nothing changed in them and
nothing was still travelling. Press SNAPSHOT on a state you have been listening to and its sound
is there at once. Press it the moment you arrive (or while a knob glides) and the snapshot is taken
at once, but its card fills in a few bars later, once four bars of that state have played. If you
move on first, it keeps the bars that were held; if there were none, the bars it was taken over.

### A recall, when more than knobs differ

Sound knobs travel to a recalled snapshot's places over the GLIDE time. What cannot travel is
crossed over instead, over the same time and starting on the same beat:

- **An effect put in, taken out or changed for another** (or the same effect with another CONFIG,
  other files, or a menu or switch of its own moved): the one that was there carries on exactly
  as it was and fades out, its tail with it, while the new one arrives beside it.
- **An instrument's model, anything else CONFIG sets, or a menu among its knobs**: the instrument
  playing carries on as it is and fades out; a copy of it, changed, fades in. Both are played the
  same notes, and both follow the knobs that travel.
- **An effect that stays** keeps running, and its knobs travel like any other.

With GLIDE OFF the crossing takes a fiftieth of a second: enough not to click. While something
crosses, that lane runs both, so a long glide between very different snapshots costs more of the
processor until it is over.

### ONLINE: the library on the web

With **ONLINE** lit (the library screen's strip), every snapshot you take goes to your snapshot
library on the web in the background: the snapshot whole, its four bars, and the definitions it
names. Nothing waits on it. With ONLINE out, or no network, a snapshot simply has not gone yet; it
goes when ONLINE is lit and the library answers. The lamp: green when the library holds every
snapshot taken, yellow while sending, amber when it waits (no answer, or not set up), red when
the library turned SF-000 away. The line beside it says which, and how many wait.

**SET UP** takes the library's address and the two strings of its service token (Client ID,
Client Secret). CHECK asks the library whether they let SF-000 in; KEEP stores them in
`online.json` beside the session, readable by you only. Neither the address nor the strings are in
the code.

- A snapshot renamed here is renamed there. One deleted here stays there.
- One removed in the web library is not sent again.
- The snapshots the rig came with (INIT and the rest, CLEAN SLATE) stay at home.
- Each time it connects it asks the library what it holds, so the two never drift.

### Recording

REC writes a take to `Music/SF-000/`, in a folder named for the moment it began. It starts at once,
whether or not the clock is running, and goes on until its key is held. Quitting ends and keeps it.

| FILE | WHAT |
|---|---|
| `00 SET.wav` | the set: the output, after the limiter. 24-bit |
| `01 KICK.wav` to `11 VOICE.wav` | each lane as it went into the mix: after its inserts, EQ, MUTE and SOLO. 32-bit float, so none can clip |
| `12 DUB.wav`, `13 SPACE.wav` | the two returns as they went into the mix. 32-bit float |
| `take.txt` | when, how long, what was kept, and anything lost |
| `session-at-start.json` | the rig as it stood when the take began |

- The lanes and returns added together are what the master chain was given. Each begins with the
  master chain's look-ahead in silence (181 samples at 48 kHz), so it lines up with the set.
- A lane or return that was silent all take is not kept. `take.txt` names them.
- A take is about 19 GB an hour at 48 kHz. The bar says how many hours the disk has room for as it
  starts, and the take ends by itself, kept, with a minute of room left.
- The audio thread never waits for the disk. If the disk falls behind by more than four seconds,
  whole buffers are left out of every file alike (the files stay in step), the key turns amber, and
  `take.txt` says how much was lost.
- The files' lengths are written into them every ten seconds, so a take cut short by a crash or a
  pulled plug still opens, up to there. Past 4 GB a file is written as RF64.
- MIDI and knob moves are not recorded yet.

### Snapshots that come with it

CLEAN SLATE (always first in the library) puts every knob where the rig starts it, with no steps
and no inserts. INIT is the rig as it first comes up, patterns and all. The other five are the
pushes behind the ENERGY, BUILD, ACID, DIST and WARP knobs.

### MIDI and the E16

Open AUDIO (now AUDIO AND MIDI) to see what is connected. Nothing needs choosing.

- **Notes in.** Every MIDI input is listened to. A lane's number is its channel (1 KICK to 11 VOICE),
  so the OXI One plays the lanes as it played the Live tracks. Notes and the lane's own steps sound
  together; **M** in the sequencer strip mutes a lane's own steps, so only the One plays it.
- **LATENCY** in AUDIO AND MIDI is how long SF-000 takes from a note arriving to its sound, to a
  tenth of a millisecond, and what it is made of: the wait for a buffer, the way out through the
  interface, and 4 ms inside (the limiter's look-ahead). About 10 ms at 128 samples, less at 64. It
  is the figure to delay outside gear by to hear it with SF-000. It is worked out, not measured
  (the interface's part is what the system reports). Only the buffer size changes it: no insert and
  no recall does.
- **The clock is sent, followed, or neither**: CLOCK in AUDIO AND MIDI. The choice is kept.
- **SEND.** While it runs, START, 24 ticks a beat and STOP go to every output that is not the E16.
  CLOCK LEAD is how long the follower takes to answer: play a plain sixteenth pattern from it into a
  lane, read NOTES IN LAND x MS LATE in the panel, and put that number on CLOCK LEAD. With the clock
  on, RUN waits two blocks and LEAD (a few ms) before the first sound.
- **FOLLOW.** The OXI One (or any sender) is the clock: its START, STOP and CONTINUE are the
  transport and its tempo is the rig's. The TEMPO readout says CLOCK IN and shows the sender's tempo.
  SF-000 sends no clock while it follows. RUN still works by hand: stopped and started here while the
  sender plays on, SF-000 comes back in at the sender's place in the bar. With no clock coming in,
  SF-000 runs on its own at the tempo it last had; if the clock goes without a STOP (a cable pulled),
  it plays on alone at that tempo.
- **Following: where SF-000's own steps are heard.** A note from outside is heard about 10 ms after
  it arrives (LATENCY, above). Gear the sender plays directly has no such delay. **WITH NOTES IN** (the way it starts) gives no
  head start: SF-000's own steps are heard with the lanes the sender plays, and everything SF-000
  plays is together. Gear the sender plays directly is heard that much before it. **ON THE BEAT**
  is for playing with such gear: SF-000's own steps are played that much early, so they are heard
  with it; lanes the sender plays by MIDI cannot be played early and are heard that much later, and
  after START bar 1's first step cannot be early either: it is played at once. TRIM moves the steps
  either way by ear. The settings for the OXI One, and how to line all three up, are in
  `controllers/oxi-one/`.
- **The E16's second scene, the library** (`controllers/e16/SF-000-LIBRARY.oxie16`, SF-LIB). Page 1
  is the **MAP**: the grid of knobs is the library laid out by sound, energy up the rows (bottom
  row calm, top row peak) and brightness along them (dark at the left, bright at the right). Each
  knob holds the snapshots in its square. **Turn** it to step through them (four clicks to the
  next), each heard in the headphones in time with the set; it stays on the one it has come to
  until it is turned again; **press** it and that one is recalled to the set, over the GLIDE
  time. Page 2 is **FIND**: RECENT (the newest first), FITS NOW (nearest in sound to what is
  playing), MORE and LESS (a step up or down in energy from what is playing, nearest step first),
  and knob 16 is GLIDE. A ring shows where its knob is among what it holds; an empty square's
  ring sits at nought.
- **A sequencer can play all of it.** Every address below is heard on any port, so the ONE's
  modulation lanes can turn knobs, set GLIDE and recall snapshots. **I** on the keyboard shows
  each control's number on the screen.

| WHAT | CHANNEL | CC |
|---|---|---|
| a lane's twelve knobs, in the screen's order | the lane's number | 12 to 23 |
| its SEND A, SEND B, LOW, HIGH, MID | the lane's number | 24, 25, 26, 27, 28 |
| its three inserts' eight places (whatever effect is in the slot) | the lane's number | 32 to 39, 40 to 47, 48 to 55 |
| its MUTE, SOLO, and the mute of its own sequencer (64 and over: on) | the lane's number | 56, 57, 58 |
| the sixteen snapshot knobs: a turn | 16 | 12 to 27 |
| GLIDE | 16 | 30 |
| a snapshot knob's press: recall its nearest (64 and over) | 16 | 44 to 59 |
| the DUB return's four knobs, the SPACE return's four | 16 | 60 to 63, 64 to 67 |
| TAPE, COMP, LIMIT | 16 | 68, 69, 70 |
| **RECALL** from snapshot knob 1 to 16 | 16 | 80 to 95 |

  - **RECALL** is for arranging. A snapshot knob's snapshots (the MACRO designer fills it) are a
    numbered set: the CC's value says which one, 1 the first. It is recalled as from the library:
    sound knobs travel over the GLIDE time, patterns change on the bar. A sequencer says a value
    again on every step, so only a new value acts; 0 is none, and makes the next value new. A
    number past the last it holds does nothing.
  - **GLIDE** by value: 0 OFF, 12 a quarter bar, 23 half, 35 one bar, 46 two, 58 four, 69 eight,
    81 sixteen, 92 thirty-two, 104 sixty-four, 115 a hundred and twenty-eight, 127 two hundred and
    fifty-six. Sent in the same step as a RECALL, it is taken first.
  - A knob turned by CC travels over the GLIDE time like one turned by hand: for a sequencer's
    own curve to be heard as drawn, GLIDE is OFF. A bipolar knob's middle is 64. A menu's values
    are spread evenly (INFO lists them). Insert, return, master and mute addresses are taken up
    forty times a second; a lane's own knobs and a RECALL at once.
  - Not on MIDI: tempo, key, the generator's four knobs, a step's keys, an insert's or a return's
    C, M and S.
- **The E16.** Load `controllers/e16/SF-000.oxie16` (see `controllers/e16/README.md`). Pages 1-11
  are the lanes: the twelve knobs where the screen has them, then SEND A, SEND B, LOW and HIGH. Press
  knob 16 and it is MID (its ring turns pink); press again for HIGH. Press knob 1 and it is GLIDE
  (its ring turns green), the same GLIDE on every lane's page; press again for the lane's first
  knob. Page 12 is the sixteen snapshot knobs; a press recalls that knob's nearest snapshot.
  Turning to a lane's page makes it the active lane on the screen. The rings follow the rig, and a
  travelling knob's ring walks green. A page that comes up is told its values again, so a knob's
  first turn starts where the rig is.

## Checks

```
./build.sh test                                   # build, then the offline audio and timing checks
build/.../SF-000 --test --wav out.wav             # the same, keeping 16 bars of the full rig
./build.sh perf                                   # the engine's share of real time and what the screen costs, on this machine
build/.../SF-000 --audit                          # does every lane knob change the sound? (slow)
build/.../SF-000 --uitest                         # clicks and drags every control on every screen
build/.../SF-000 --miditest                       # notes in, the E16's knobs and rings, the clock out and the clock followed, with made-up messages
build/.../SF-000 --onlinetest http://127.0.0.1:8787   # ONLINE against a snapshot library running on this machine (its `npm run dev`)
build/.../SF-000 --shot setup.png --setup follow  # draw the AUDIO AND MIDI panel: --setup send|follow|off, --beat
build/.../SF-000 --e16 controllers/e16/SF-000.oxie16   # write the E16 scene (snapshot knob names from your session)
python3 tests/e16_scene_test.py                   # the scene's shape and addresses, and its script on a mock E16 (pip install lupa)
build/.../SF-000 --shot main.png --scale 2        # draw a screen: --page library|macros, --lane N, --cfg lane|insert|return|space, --space conv, --seqmute, --rec, --dark (the dark instrument), --flat (the flat drawing), --info [--point x,y] (INFO, and the mouse resting there), --phones
SF000_SKIN_TIMES=1 build/.../SF-000 --shot x.png  # how long each of the lit skin's pictures took to make
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
| `Source/Defs.cpp` | every lane, effect and return written out as data, with a print of each: what a snapshot was made with |
| `FORMAT.md` | the session file: what is in it, and the rules it grows by |
| `tests/fixtures/` | a session file as an older build wrote it, for the check that old files still open |
| `Source/Screen.*` | the three screens |
| `Source/Help.h` | what each control does, in a line or two: INFO's boxes |
| `Source/Skin.*` | the lit skin: surfaces as heights and finishes (the 3D model's), lit into pictures the screen is put together from |
| `Source/Midi.*` | notes in, the E16 (addresses, rings, the scene file), the clock's sender, the clock followed |
| `controllers/e16/` | the E16 scene and its script |
| `controllers/oxi-one/` | the OXI One MkII project, and a checker for your own |
| `Source/Profile.*` | what a snapshot sounded like, worked out from the bars kept with it |
| `Source/Recorder.*` | takes: the set, the lanes and the returns to disk, off the audio thread |
| `Source/Online.*` | ONLINE: snapshots sent to the library on the web in the background, and what has gone |
| `Source/Main.cpp` | the app, the audio device, the command-line tools |
| `suite/` | SF Live Suite's sound code, copied (see `suite/PORTING.md`) |

State is kept in `~/Library/Application Support/SF-000/session.json` (snapshots, knobs, the base
scene, each snapshot's profile; its layout and the rules that keep old files readable are in
`FORMAT.md`), `library-id.txt` (this library's own id), `online.json` (ONLINE's address and strings) and `online-sent.json` (what the web library has said it holds), `clips/` (each snapshot's four bars), `look.json` (the look: lit or flat, light or dark), `audio.xml` (the interface) and `midi.json` (the clock sent or followed, CLOCK LEAD, ON THE BEAT, TRIM). Each time the app
opens it copies the session as it found it into `backups/` beside it and keeps the last twenty: to go
back, quit and copy one over `session.json`. Delete `session.json` to start again from the factory rig.

## Not built yet

- The skin is a first pass, drawn from measurements of the 3D model, not rendered from the model
  itself: no texture maps, one finish per material, and the inside of the displays (the meters,
  the step keys' bars, CONFIG's own display) is the flat drawing behind glass. The model's choices
  it does not follow yet are listed in `DECISIONS.md` (items 172 to 174).

- MIDI: no MIDI learn for other controllers, no notes out. Following a clock takes its ticks,
  START, STOP and CONTINUE; a song position sent on its own is not read.
- The E16: returns, inserts and the master are not on it; 7-bit knobs (128 places).
- Recording of MIDI and knob moves, and rendering a set again from them (audio is recorded: REC).
- The library: the sound map drawn on the screen, more finders (traits by name), suggestions timed
  to where the set is, a learned model for likeness. Meaning filters; snapshot knob pages beyond the first.
- A snapshot knob moves sound knobs only: it does not cross between effects or models (a recall does).
  A model changed by hand in CONFIG changes at once. Nothing on the screen shows a crossing yet.
- Lanes as modules that load into a slot, each with a descriptor of what its knobs do to the sound.
  The session file is ready for it: `FORMAT.md`.
- ONLINE the other way: loading a library file, buckets and snapshot knobs made in the web library.
- PA and cue outputs with room EQ. A display mode made for a dark stage: the dark instrument (D)
  is a start on it, with nothing dimmed or enlarged for distance.
- Menus, switches and inserts changing on a beat or bar line at a recall (they change at once; step patterns wait for the bar).
- Crash recovery beyond the saved session; the 8-hour soak test.
