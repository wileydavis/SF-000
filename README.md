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

1. Press **SETUP** (white key, top bar) and pick the interface and its outputs. The choice is kept.
2. Press **RUN** (or the space bar). Every lane has a starting pattern.
3. Press **F** for full screen at 1728 x 1117; **F** or **Esc** leaves it (so does FULL SCREEN in SETUP).
4. It opens as the 3D stage: the instrument as a solid thing, lit. **3** changes between that and
   the lit skin; **V** goes round its three views; **K** unlocks the view so a drag turns it. See
   The 3D stage, below.
5. Press **D** for the dark instrument and **D** again for the light one. **L** changes between the
   lit skin (how it starts) and the flat drawing. The look is kept between runs. See The look, below.

## Playing it

| Do this | To |
| --- | --- |
| Drag a knob up or down | turn it (hold Shift for fine; double-click for its starting value; the wheel works too). A stack knob is the other way round: fine as it is, a click of its 127 at a time, and Shift is the fast way |
| Set GLIDE, then turn a knob | the sound travels there over the GLIDE time: the knob turns green and walks, with a white mark where it is going. It sets off on the next beat while the clock runs. Turn it again on the way and it carries on from where the sound is. OFF: at once |
| Click a lane's name, or its plate anywhere off the controls | make it the active lane: the inserts and the sequencer strip show it. Its display is the one lit up |
| Click an insert's plate, the returns', the master's, the snapshot knobs', the sequencer strip's, off the controls | bring that module to the front: its name lights up, and the E16 turns to its page (see MIDI and the E16) |
| **C** on a lane, an insert or a return | open its CONFIG panel (see below). **C** again, **X** or Esc closes it. It also shuts when another lane comes to the front (by any hand), or another module by a key or a controller: CONFIG is one module's, and the E16 is then on the page that was asked for |
| **M**, **S** | mute and solo: a lane, an insert (M passes it by, S plays only what it adds), a return |
| Click an insert's name | step that slot through DELAY, REVERB, CONVOLUTION, DISTORTION, COMPRESSOR, RADIO, EMPTY |
| Click a step | unlit: light and select it (velocity 75, a whole step of gate). Lit: select it. Lit and selected: put it out, and it forgets everything it held |
| Drag a step up or down | velocity |
| Drag a step left or right | gate. It stops at the step's edge. Past the edge it runs on through unlit steps, and a little past the next lit step it is legato into it (a tie, on the same note) |
| RATCHET, NOTE, OCTAVE | change the selected step (NOTE and OCTAVE on lanes that take notes). A step says the note that sounds: with KEY set, a new step is the key's own note, and NOTE goes up from there |
| < NUDGE, NUDGE > | move the whole pattern one step |
| Drag the LENGTH knob | 1 to 128 steps. Click a page above the steps to see its 16 |
| **M** in the sequencer strip | mute the active lane's own sequencer. Its steps stay, drawn grey, and go unplayed; the note it was holding is let go. Notes from MIDI in still sound on the lane. Press again and it comes back on its next lit step |
| RANDOM | a new pattern at every press: SEED goes to a new one of its 128 places, so the four knobs still say what plays. While the key is lit, HITS, ROTATE, ACCENTS and SEED rewrite the pattern. Change the rhythm by hand (a step lit or put out, a velocity, a gate, a ratchet, a nudge) and the key goes out: the pattern is yours, and the knobs leave it alone. A note or an octave is not rhythm, and the key stays lit |
| EUCLID | a pattern with its hits spread evenly, at once; while the key is lit, HITS, ROTATE and ACCENTS rewrite it. Press it again, or change the rhythm by hand, and it lets go |
| **REC** (top bar) | press: start a take. Its key turns red and its name becomes the time it has run. **Hold** the key to end it. See Recording, below |
| **SNAPSHOT** (top bar, beside REC and RUN) | keep everything on the main screen as a new snapshot. If the clock is running, the last four bars are kept with it as a clip and it is given a profile: nothing to name or tag (see The library, below) |
| Turn knob 1 to 4 of the snapshot module (a **macro knob**) | push the knobs it is tied to, from wherever they stand, over the GLIDE time |
| Turn knob 5 to 12, 14 or 15 (a **stack knob**) | **CUE** out: morph the set from wherever it is toward the next of the knob's snapshots, the way the knob is going. **CUE** lit: go through its snapshots in the headphones. A press takes the set to where the knob is: a snapshot on its place, between two that far between them (see The snapshot module, below) |
| Recall a snapshot | sound knobs travel over the GLIDE time; step patterns change on a bar line while the clock runs: the next one, or, with a glide of a bar or more, the first at or after the glide's end; the rest changes at once |
| **NOTES** (on the snapshot module, in the library; **N**) | whether a recall brings a snapshot's notes. Lit: each lane's sequencer is given what the lane was playing, and plays it. Out: a recall is the sound alone and every sequencer stays as it is (see What a snapshot holds of the playing, below) |
| Double-click a stack knob (its press on the E16) | the set goes to its nearest snapshot, over the GLIDE time |
| **CUE** (on the snapshot module; knob 4 pressed) | how the stack knobs are played: lit, they choose in the headphones; out, they morph the set |
| **NEW** (top bar, beside the flag) | held until it is full of light: a new track. The rig as it starts: no steps, no effects in the racks, every knob where the rig starts it, every MUTE and SOLO out, the macro knobs at rest. TEMPO, the master, the snapshot module's knobs and the library stay. A tap does nothing |
| **LOAD** (on the snapshot module) | the modules kept on the instrument: choose one, and LOAD puts it in the chassis, whole (see Modules, below). The web library's module is **WEB LOAD**, in the library |
| LIBRARY | the snapshot library: press a snapshot to hear it in the headphones (its card then shows a key for every part of the rig), **RECALL** to take it to the set with the parts lit, rename, hold DELETE. The head of the list says whose are listed and in what order (see Finding a snapshot, below) |
| MACROS | the designer for the sixteen knobs: pick one of 1 to 4 and press **LEARN** to tie it to knobs; pick a stack knob (5 to 12, 14, 15) and ADD snapshots to its stack (see The snapshot module, and A macro knob, below) |
| ARRANGE (**A**) | the arrangement: snapshots in a line that morph one into the next, played from the clock and printed to files (see The arrangement, below) |
| Drag TEMPO or KEY | 60 to 200 bpm; the key every lane follows (parts are written in C: the pitched lanes move with it, and the kick, the percussion and the drum bus's boom tune to its nearest note). A lane's KEY FOLLOW, in its CONFIG, lets it be. The steps of the sequencer say the notes as the key has moved them, so a part reads as it sounds; on a lane that does not follow, a new step is written at the key's note |
| Drop an audio file on a lane | load it, where the lane's model takes a sample (VOICE takes a phrase). Its CONFIG panel opens to show the waveform. SF-000 keeps its own copy (see Where everything is kept) |
| Drop an audio file on an open CONFIG panel | it goes to whatever that panel is for: the lane, the insert, or the return. An impulse response dropped on SPACE's CONFIG (or on the RETURNS panel) makes SPACE a convolution reverb of that place |
| Click the clip lamps above MIX | clear them |
| Arrow keys, on the main page | up and down: the active lane. Left and right: the step page |
| **Tab**, on the main page | the next module to the front: a lane, then those of its three inserts that have an effect in them, then the next lane and its inserts, and after the last lane the snapshot knobs, the returns, the master and the sequencer strip. Shift-Tab: the one before |
| Arrow keys, **Return**, **=**, **Backspace**, **[** **]**, **Tab**, in LIBRARY, MACROS and ARRANGE | the rack without the mouse: a mark (four yellow corners) that the arrows move over pockets and chips, and keys for what is done where it stands (see The rack on the keyboard, below) |
| **C** (the keyboard's) | CONFIG for what is in front, and shut again (the returns: DUB's, then SPACE's, then shut) |
| **N** | NOTES: whether a recall brings a snapshot's notes |
| **I** | INFO: every control MIDI reaches shows its CC number, a panel its channel, and a control the mouse rests on says what it does. **I** again puts it away |
| **H** | PHONES: headphones on the one pair of outputs. While a cue plays, the set is in the left ear and the cue in the right (see The library, below) |
| **3** | the 3D stage, or the lit skin it is drawn from (see The 3D stage, below) |
| **V** | 3D: the next view. From above, the player's, from the corner |
| **K** | 3D: unlock the view (drag off the controls to turn it, shift-drag or right-drag to slide it, scroll or pinch to come nearer), and lock it where it is put |
| **M** | 3D: whether the eye goes to what the controller moves |
| **D** | the dark instrument, or the light one |
| **L** | the lit skin, or the flat drawing it replaced (neither is where new work goes: from the thirty-seventh build on a page is made for the 3D stage, and the lit skin is what stands in if 3D cannot start) |

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
- **A key goes down as its kind does.** One that latches (MUTE, SOLO, a lane's C, RUN, REC while
  a take runs, a toggle in CONFIG, CUE, NOTES, PHONES, ONLINE, the scope's keys, RANDOM or EUCLID
  while armed, LEARN while a knob is being taught) sits low for as long as it is on, 0.66 mm
  above its plate where an idle key stands 1.27, and comes up when it is pressed again. One that
  does a thing once (SNAPSHOT, PANIC, NUDGE, LIBRARY, a step key) is in only while the hand is on
  it. Under the hand any key is all the way in, 0.26 mm. So what is on can be read from the keys'
  heights as well as from their lights.
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

### The 3D stage

**3** on the keyboard. The instrument is drawn as a solid thing: the case, a plate in each pocket,
caps in rings of lights, keys, glass displays, lit from behind and to the left with a soft shadow
under everything that stands up, a little warm light under the case, and a glow round every light
that is on. It is how the app opens. **3** again is the lit skin.

It is not a second interface. The screen lays out and handles everything exactly as before; in 3D
its drawing goes to a different place. So every control works: the lanes' knobs and C, M and S
keys, the sequencer and its step keys, CONFIG, the library, the macro designer, INFO. A knob in
the hand turns by how far the hand moves, as on the flat screen, however near the eye is; a step
key's gate and anything else that follows the hand is wherever the pointer is on the instrument.

- **Three views**, each with the whole instrument fitted to the window. **V** goes round them:
  from above (the flat screen's own view, with depth), the player's (56 degrees up, how it
  starts), from the corner (low, for the look of it: a cap hides the legend behind it from there).
- **The view is locked** until **K** unlocks it. Unlocked, a drag that starts off the controls
  turns the instrument, shift-drag or right-drag slides it, the wheel or a pinch brings it nearer
  (over a knob the wheel still turns the knob). **K** again locks it where you put it. The view,
  the lock and M are kept between runs.
- **The eye follows the controller** (**M** turns this off and on). A knob turned on the E16: the
  eye flies in to that knob's module, and goes home a couple of seconds after the hand stops.
- **A module chosen from the controller or the keyboard holds the eye.** The wheel's second
  press, a page turned to on the E16, Tab: the eye flies in to that module and stays there
  until another is chosen (it goes to that one) or CONFIG is opened or shut by OPEN or by C (it
  holds CONFIG and the module beside it for as long as CONFIG is open, the module alone after).
  While the wheel's pointer is up the eye is home, so that every module can be seen to choose
  from; put down with nothing chosen, it goes back to what it held. **Esc** lets it go home, and
  so do V, M and a drag of the view. A click of the mouse does not fetch it (the hand is
  already there), but a module clicked to the front while the eye is held is what it holds.
- A snapshot recalled or pushed, when three or more knobs are moving together: the eye flies out
  to hold all of them. What never moves it: notes, the clock, the tempo under an outside clock,
  keys (mute, solo, THROW, FREEZE), and any knob moved by a message from another port than the
  E16's, which is a sequencer playing it. Those knobs' lights move; the view stays.
- **A moved knob has a tag beside it**: its name, the reading the engine gives (hertz, decibels,
  a sixteenth), where it is and its MIDI address, and a bar. In the hand on the screen, or under
  a hand on the E16; not for a sequencer's moves or a snapshot's. INFO's box floats by the
  pointer the same way.
- **A knob's lights follow the engine itself**, read on the drawing thread every frame, so they
  move with the sound whatever else the app is doing.
- **The keys travel.** In, a key goes its whole way in 45 ms; out, on its spring, in 70. A key
  latched from somewhere other than the pointer (the E16's MUTE, a snapshot's recall, the space
  bar on RUN) goes to the bottom first and then to where it sits, as the switch itself must to
  catch or let go. A key whose action was done from the keyboard is seen to go in and come back.
  Only the keys are sent to the card again while one travels, with its shade.
- **Typing a name** (a snapshot's, a snapshot knob's) happens in a small box over the stage.
- **D** is the dark instrument here too.

The dark instrument's faceplates have a worn black anodized finish: a fine grain, a slight
unevenness, scratches and dust, wear toward the metal along the edges. The scratches are a
photograph's (`assets/wear.png`, made by `tools/wear.py`): every plate takes its own piece of
it, turned its own way, so no two modules are worn alike. Its case is powder coat: an
orange-peel, a speckle, a few scuffs. The pale instrument is plain.

The glow under the case is set in SETUP: three knobs. COLOUR (round the wheel), BRIGHTNESS (none
to four times what it comes with) and PULSE: how much it beats with the kick, from none (steady,
as it comes) to intense (nearly out between hits, three times as bright on one). It follows the
kick lane's own meter, so a muted kick does not pulse. Double-click a knob for what it came with.

If OpenGL cannot start on a machine, the lit skin stays, the top bar says why for a few seconds,
and the reason is written to `3d.log` beside the session. The same if it starts and its first
picture comes out blank: the stage reads that picture back, and gives way to the lit skin
rather than show nothing. 3D is tried again at the next launch, or with **3**. A stage that
is made and never draws is given six seconds in front of you. And if the app dies while the
stage is coming up, the next launch leaves 3D off and says so (**3** tries it again): a stage
that takes the app down cannot do it at every launch.

How it is made: the lit skin's drawing calls are a list of solid parts (a plate here, a knob
there, a key held down), and in 3D the skin writes that list down instead of drawing pictures.
Everything flat, which is the print on the plates and what the displays show, is drawn by the
same code into one picture (the sheet) that is laid over the tops of the parts; a display's part
of it is light. OpenGL 3.2 draws both on a thread of its own: a shadow map from the one light,
the picture at four samples a pixel in a wide range of brightness, the bright parts spread for
the glow, and the whole eased into the display's range. Only what changes is done again: a moved
knob prints a few square centimetres of the sheet; nothing moving, a frame shows the last
picture again.

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
- **The controls stand in boxes by what they belong to**: SOUND (MODEL first), LAYER, SAMPLE,
  ECHO, POCKET, RIG, and on an instrument MOTION 1 and MOTION 2. A model's own controls (FM 4-OP's
  operators, WAVETABLE's table) and a sample's are there only while that model plays, as the
  lane's model or its layer's: what is on the panel does something. More rows than fit: scroll.
- **MOTION** moves one of the instrument's knobs by itself (an effect's and a return's CONFIG has
  the same two, moving that effect's knobs, with INPUT, the level coming in, in place of NOTES).
  Each lane has two: WAVE (sine,
  triangle, square, ramps, random, drift, steps, an envelope, the kick's hits, the lane's own
  notes), SPEED (a sixteenth to 64 bars, locked to the bars), TARGET (the lane's own knobs first,
  as on its panel, then the rest of the instrument's) and AMOUNT (either way, up to the whole
  range). It adds to where the knob stands and to what a snapshot knob does. It is kept in
  snapshots; one taken before MOTION could be set means it off.
  **What a MOTION moves is kept by the parameter** (eighty-fifth build). It was kept as a place
  on the list of what the instrument's MOTION can move, and that list grows when an instrument
  gains a knob: from the seventy-ninth build to the eighty-fourth, a MOTION in a snapshot taken
  before them moved the wrong thing wherever its target was far enough down the list (a bass's
  COLOR became POCKET SHAPE, a chord's FILTER its LEVEL). Those snapshots are read right
  again, by when each was taken; nothing in them is rewritten.
- **PLAIN**, beside LOAD in the CONFIG of a lane that is playing a sample, sets the lane to
  play it **as it was recorded** (see A sample as it was recorded, below). The line beside it
  says the file's loudest point (PEAK) and the note that plays it at its own pitch (ROOT).
- **LOAD**, on any CONFIG that takes a file (a sample, a phrase, an impulse response), lists
  folders and sound files in the panel itself: a click goes into a folder, UP comes out, a click
  on a file loads it as a drop would. No window of the system's is opened, so it works full
  screen, while playing. It opens where the last file came from.
- **INFO** (I) answers for every CONFIG control too. While INFO is on, the keyboard's keys are
  listed at the foot of the screen; the list steps to the other side when the pointer comes to it.
- **SPACE (return B) is one of two reverbs.** MODEL, first in its CONFIG, chooses REVERB (the
  algorithmic one it starts as) or CONVOLUTION. The convolution holds eight places; PLACE (or a
  click on the numbered boxes in its display) chooses one, and a dropped file fills the place it
  is on. An empty place is the built-in room. Each reverb keeps its own four SPACE knobs. Which
  reverb SPACE is, and the files it was given, belong to the rig: they are kept in the session and
  a snapshot recall leaves them alone.
- **An insert belongs to snapshots, whole.** A CONVOLUTION insert takes files the same way (drop
  them on its CONFIG), and a snapshot keeps the effect, its knobs, its CONFIG and those files. A
  recall puts it back with its room already loaded.
- **BANDS: a convolution with three places at once.** BANDS, in a convolution's CONFIG (an
  insert's, or SPACE's), gives the low, the middle and the top of what plays a place each. Lit, the
  panel has four more boxes: where the bands meet (**LOW/MID**, 60 Hz to 1 kHz; **MID/HIGH**,
  1 to 12 kHz), and for each band its **PLACE** (one of the eight, or PLACE: whatever the PLACE
  knob is on), its **LEVEL** (-24 to +12 dB), its **DECAY** (a share of the DECAY knob's) and its
  **SIZE** (on top of the SIZE knob's). The boxes in the display say which bands play from each
  place (L, M, H). A tight room under the bass with a long hall over the top; a pipe in the
  middle of a cave; the top band twice the size and the bottom cut to nothing. Any file is a
  place, a room or not: a hit, a chord, a second of noise.
  - Three bands of one place, at the same settings, are that place again (checked: to a
    hundredth of a dB). Two different places meet at a crossover with no dip there.
  - It costs nothing more to play: the three are made into one response when a knob moves (off
    the audio thread, as every change to a place is), and that one is what runs.
  - SIZE, DECAY, DARK, START, LENGTH and REVERSE work on all three; with BANDS out the bands'
    knobs do nothing and the convolution is what it was.
- A menu shows its current choice under the knob. The parameter last touched shows its value in
  the panel's header. Double-click returns a knob to where it started. The wheel scrolls if there
  are more rows than fit.
- What CONFIG sets is stored in snapshots with everything else, except which reverb SPACE is.
- **The master's CONFIG is its drum bus's** (the C beside DRUM BUS; with the master in front, the
  C key or OPEN on the E16). See The drum bus, below.

### The drum bus

DRUM BUS, on the master, is one effect the drums play through together on their way to the mix:
what a drum-bus processor does to a group, built in. Four knobs on the master's face, the rest in
its CONFIG.

| KNOB | WHAT |
|---|---|
| **DRIVE** | glue and heat on one knob. The first third brings in a gentle compressor (3:1, slow enough to let the hit through); all the way it is saturation. The drums get denser and their peaks stay where they were |
| **CRUNCH** | a second distortion on the highs alone (above 1.6 kHz): bite on the hats and the clap, the kick's body left clean |
| **PUNCH** | the transients, above the sub. Either way the hit is brought out. Right: what follows it is lifted too (fuller). Left: it is pulled down (tighter, drier). The sub is left as it is |
| **BOOM** | a resonance under the drums, rung by their lows: weight and sustain at one note |

In its CONFIG: **DAMP** (a low-pass on what DRIVE and CRUNCH have made, down to 700 Hz), **BOOM
TUNE** (30 to 90 Hz; near the kick's own pitch it rings most), **BOOM DECAY** (80 ms to a second
and a half), **MIX**, **TYPE** (how DRIVE clips: SOFT, MEDIUM, HARD), **COMP** (off: DRIVE is
saturation alone), **BOOM TO KEY** (on: with a KEY set, the boom is the nearest note of the key to
BOOM TUNE), **LEVEL**, MOTION, and **LANES**: which of the eleven lanes play through the bus. To
begin with, the five drums.

- **At rest it is not there.** With the four knobs at nought (and DAMP at nought, LEVEL at 0 dB)
  the lanes reach the mix exactly as they did before there was a bus, sample for sample. The first
  knob to move brings it in over a hundredth of a second.
- It works against the bus's own level (a slow reading of its peaks: up at once, down over half
  a minute, held through silence), so the knobs do the same whatever the lanes' levels are.
- What it hands on is held under two and a half times that level, however far PUNCH and BOOM are up.
- A lane's sends leave before the bus. RUMBLE is still made from the kick as it leaves its lane.
- **A snapshot holds it**: the four knobs travel on a recall and a snapshot knob (or LEARN) can
  reach them; its CONFIG comes with them. BUS, in the row that says what RECALL takes, leaves it
  out. A snapshot from before there was a bus leaves it as it is.
- On the E16 the four are the second row of the MASTER page; a sequencer reaches them on channel
  16, CC 71 to 74.

### POCKETS: who has which part of the spectrum

POCKETS, on the master, is the mix made by the instrument: every lane has a **pocket**, a
stretch of the spectrum that is its own, and where two sounds would cover one another the one
with the weaker claim to that stretch gives way to the other. Only there, only while the other
is sounding, and by no more than it takes to sit clear below it. Move a pocket and the sound moves
to another place in the mix, so where the pockets are and how they overlap is part of the sound,
not a job for afterwards.

(POCKET DEPTH and POCKET SHAPE, on a lane, are something else and were there before: the kick's
pocket in **time**, a duck round each kick. POCKETS is each lane's place in **frequency**.)

**The pocket is the kick wrapped in the lane** (seventy-ninth build), drawn and not compressed.
A lane's POCKET takes what the kick says of each hit (when it lands, how hard, how long it lasts)
and cuts the lane's level along the kick's own decay turned over: the lane is out through the
kick's punch and comes up to meet its end. **POCKET DEPTH** is how deep the cut goes (PUMP on
RUMBLE). **POCKET SHAPE** is its length: the kick's curve scaled in time, all of the kick's
length at full and a quarter of it at nought, so a long kick can have a short pocket that is
still the kick's shape. The kick's own DECAY sets what "all of it" is. (Before this build SHAPE
went from a notch at the kick's pitch to a pump of the whole lane, on a decay of its own: every
snapshot's ducking sounds different from here on, by the DEPTH and SHAPE it already had.)

**AFTER FX** (eighty-fourth build), a switch in the POCKET box of a lane's CONFIG and a key in
LOW END: the lane's pocket is made after its insert effects and its EQ, where it is otherwise
made in the instrument, before them. It is the same pocket. On, a reverb's or a delay's tail on
the lane is held out of the kick with the lane; off, the tail rings on through it. A snapshot
keeps it. **DUCK FOR**, in the CONFIG of a REVERB or a CONVOLUTION (an insert, or SPACE), says
what its DUCK makes room for: KICK AND SOURCE (as it was), KICK, or SOURCE.

**P** on the keyboard, or the POCKETS key on the master, opens its page: a row for each lane and
the two returns, each with a map of the spectrum from 31 Hz to 16 kHz. On a map the yellow shape
is the lane's pocket, as tall as it is firmly held; the pale bars are what the lane is playing,
band by band; the red at a bar's top is what is being taken from it at that moment. Esc, P or MAIN
goes back.

Four knobs for the whole rig, on the page's strip:

| KNOB | WHAT |
|---|---|
| **AMOUNT** | how much of it is done, from none to all that CLEAR asks for. It starts at nought, where POCKETS is not there at all |
| **CLEAR** | how far below the one with the claim the others are put, band by band: 0 to 18 dB (3 to begin with). Low: they may be as loud as it. High: it stands well clear |
| **SPEED** | how soon a sound comes back once the other has gone: over 600 ms at nought, 25 at full. Slow: it breathes. Fast: it snaps back between hits |
| **RETURNS** | the most the two returns give, up to 24 dB. They have no pocket: echo and reverb give way to every lane wherever the lane is sounding, and fill what is left |

Four for each lane, beside its map:

| KNOB | WHAT |
|---|---|
| **HOME** | where the pocket is, 31 Hz to 16 kHz. Drag the pocket itself along the map |
| **WIDTH** | how wide it is, half an octave to eight. Drag the pocket up and down. Narrow: a claim to one place, and the rest of the sound gives way. Wide: it keeps more of itself, and more has to move for it |
| **HOLD** | how firmly it holds its pocket: where pockets meet, the lane with more HOLD wins. Outside its pocket a lane has a quarter of its HOLD, so one with a lot of it still comes before the rest there. At nought it gives way to everyone |
| **YIELD** | the most the lane gives in any band, up to 24 dB. At nought nothing is ever taken from it: the kick starts there |

- **Where the pockets start.** Each where its lane's own sound has most of itself: the kick at
  55 Hz and never cut, the bass over it at 80 Hz, the rumble under both, the hats at the top, and
  in the middle (where everything meets) the voice before the lead, the lead before the chord, the
  chord before the percussion and the pad. With AMOUNT at full on the rig as it starts, nine
  lanes give somewhere, the most in one band is some 10 dB, no lane loses more than 3 dB over
  all, and the kick's stem is the same to the last bit. Half way is a good place to begin.
- **AUTO** places the pockets from what is playing: each lane heard while the page was up gets
  the narrowest pocket (an octave to four) that holds three fifths of its sound, where it holds
  the most. HOLD and YIELD stay as they are. **RESET** puts every pocket back where the rig
  starts it. Both travel over the GLIDE time. A double-click on a pocket puts that one back.
- **With AMOUNT at nought it is not there**: nothing is looked at, nothing is in the path, and the
  set is the same to the last bit. With its page up it looks (so the maps move, and AUTO has
  something to go by) and still touches nothing.
- **No delay.** Each lane and each return is heard through 28 third-octave bands and carved by
  28 bells that only ever cut. A bell that is not cutting is not run. A low band takes tens of
  milliseconds to ring up, and a kick is past its hardest part by then: once a kick has been
  heard, the next is known by its spectrum from its first millisecond, so the bass is out of its
  way 8 ms in and not 40.
- **What is not heard covers nothing.** A muted lane, or one put out by another's SOLO, asks
  nothing of the others: bring the kick out and the bass has the bottom to itself. Each row has
  the lane's M and S, to hear a lane by itself as POCKETS leaves it.
- A lane is carved as it leaves its strip: its sends, its stem in a take and its meter are of
  what was left. The returns are carved as they come back.
- **The knobs are knobs**: forty-four for the lanes and four for the rig, each in a snapshot,
  each travelling over the GLIDE time on a recall or under a hand (a HOME sent somewhere sweeps
  there), each within reach of a snapshot knob and of LEARN. A snapshot from before there were
  pockets leaves them as they are. In the row that says what RECALL takes, a lane's pocket goes
  with its lane and the rig's four with BUS.
- On the E16 the rig's four are the third row of the MASTER page. **OPEN** on the master is its
  drum bus's CONFIG; OPEN again is POCKETS, and the CONFIG pages are then its knobs, a lane to a
  knob: HOME (and AMOUNT on the twelfth), WIDTH (and CLEAR), YIELD (and SPEED), and a fourth,
  HOLD (and RETURNS), which PAGE + from the third puts on the third's knobs. The display says
  which. OPEN once more is the way back. A sequencer reaches a lane's four on the lane's channel,
  CC 59 to 62, and the rig's four on channel 16, CC 75 to 78.
- Its cost on the machine this was written on (a slow one): 2 % of a core to look at all thirteen
  parts, 5 % with two hundred bells cutting at once, which is every lane giving way all over.

### A sample as it was recorded

A lane does not play a sample back: it plays it through everything the lane is. As SF-000 opens,
the BASS lane with a sample plays a fraction of each note (GATE), through a low-pass (CUTOFF),
with the sample's own lows crossed out and a sine put in their place (SPLIT, in CONFIG's LAYER
box, even with the LAYER knob at nought), at the pitch of the step and not the sample's own,
and well under the file's level. Each of those is a setting.

**PLAIN** (in the lane's CONFIG, beside LOAD, while its MODEL is SAMPLE and a file is loaded)
sets all of them at once:

| LANES | WHAT PLAIN SETS |
|---|---|
| BASS, LEAD, CHORD, PAD | CUTOFF 100, RES 0, ENV AMT 0, DRIVE 0, FILTER centre, GATE 100, LAYER 0 and SPLIT OFF, GLIDE 0, ATTACK 0, POCKET DEPTH 0, ACID, WARP and DRIFT 0, DENSITY 0, CHORD NONE, PLAY THROUGH, KNOBS on with DECAY 100, DETUNE 0, SPACE 0 and COLOR centre, the lane's LOW, MID and HIGH centre. **TUNE**, so that the lane's first lit step plays the sample at its own pitch (where that is within two octaves; else TUNE 0, and it says the root) |
| KICK, CLOSED HAT, OPEN HAT, CLAP | TUNE 0, DECAY 100 (the whole sample), CLICK 0, TONE centre, DRIVE 0, FILTER centre, LAYER 0 and SPLIT OFF, DENSITY 0, KEY TRACK off, the EQ centre. They play a sample at its own pitch whatever the note |
| VOICE | TUNE 0, GATE 100, ECHO 0, TONE centre, DRIVE 0, FILTER centre, POCKET DEPTH 0, DENSITY 0, SLICE MODE WHOLE (any note is the whole phrase, at its own pitch), the EQ centre |

It says what it did in CONFIG and in the bar. **It leaves LEVEL, GAIN, NORMALISE, the sends and
the lane's insert effects** (it names the inserts that still act: LEAD opens with DELAY and RADIO). RUMBLE has no
PLAIN: it shapes whatever it is given. PERC plays no sample.

**A sample's own controls**, in CONFIG's SAMPLE box with START, END and the fades:

- **GAIN**: the sample's own gain, 24 dB either way, before DRIVE and the filter (so up, DRIVE
  bites sooner). On every lane that plays a sample.
- **NORMALISE**: on, the sample's loudest point (of either channel) is brought to full scale, so a quiet file plays
  as loud as a loud one; GAIN is from there. PEAK, beside the file's name, is that loudest
  point: one that reads -10 dB comes up 10 dB. On every lane that plays a sample.
- **PLAY** (BASS, LEAD, CHORD, PAD): **HELD**, the sample sounds while its note is held and
  fades when it is let go, GATE being its length; **THROUGH**, a note plays it to its END
  however short the note, as a drum does, the next note starts it again, and GATE is how much
  of the sample plays. KICK, the hats, CLAP and VOICE play a sample through as they are.
- **KNOBS** (BASS, LEAD, CHORD, PAD), on as a sample opens: **every knob of the lane acts on
  the sample.** DECAY is how fast it dies away (the top fifth of the knob brings the sustain
  up, to all of it at full); ENV AMT opens the filter at the start of each note, from CUTOFF;
  COLOR tilts it darker under its centre and brighter over it; DETUNE adds a second reading a
  few cents off and SPACE the ensemble (neither on BASS, which stays one voice in the middle).
  Off, those knobs leave a sample alone, as they did before: a snapshot taken before this
  switch existed recalls with it off, so it sounds as it did. PLAIN leaves it on and puts each
  knob where it does nothing.
- On the hats and CLAP, and on VOICE with its MODEL at SAMPLE, **TONE** tilts a sample darker
  under its centre and brighter over it. (It did nothing to a sample before.)

GAIN, NORMALISE and PLAY open where they change nothing, and a snapshot holds all four. Nothing here makes a lane
louder than its LEVEL: after PLAIN at the LEVEL each lane opens with, a sample is about 15 dB
under its file on BASS, the hats, the clap and VOICE, and 3 dB under on KICK, before the master
(which adds about 7 dB as it opens).

**WAVETABLE**, a MODEL of BASS, LEAD, CHORD and PAD (a wavetable in earnest from the
eighty-fifth build). A table is many frames, each one cycle of a wave, and **POSITION** is the
place along it: the oscillator reads between the two frames there, at any pitch, clean.

- **POSITION and TWIST are the lane's first two knobs** while the model is WAVETABLE, on every
  lane that has it. The rest of the panel is as the lane has it (on LEAD the fourth knob is
  POS ENV); what the two knobs took the places of is in CONFIG, under USUAL KNOBS.
- **TABLE**, in CONFIG's WAVETABLE box. **CLASSIC**: the four waves it always had (a sine, a
  saw, a square, a vowel). Then eleven tables of 64 frames: **BASIC** (sine, triangle, saw,
  square, pulse), **HARMONIC** (a saw opening from its fundamental to all of it, with a peak
  where it is cut: a filter sweep in the oscillator), **PWM**, **SYNC** (the hard-sync sweep),
  **FORMANT** (OO, OH, AH, EH, EE), **FM**, **FOLD**, **DIGITAL**, **ORGAN** (the drawbars
  coming out), **BASS** (a sub driven square), **COMB**. **FILE**: the file on the lane is a
  wavetable, 2048 samples a frame, up to 256 frames, as Serum, Vital and Phase Plant write
  them (a short file that is not a whole number of frames is one cycle). **SAMPLE**: frames
  cut along a recording on the lane.
- **TWIST** bends the cycle as it is read, the way **TWIST AS** says: **BEND** (the wave
  squeezed towards the end of its cycle), **SYNC** (read faster than the note and restarted
  by it, up to eight times), **FOLD** (driven into a folder), **PULSE** (the whole wave
  squeezed into the front of the cycle). At nought, the table as it is.
- **POS ENV**: the note's filter envelope carries the place along the table as the note
  starts and lets it back as it falls (left of the middle: the other way). DECAY sets how
  long. A MOTION aimed at POSITION sweeps it on a wave.
- **WANDER**: each note drifts slowly about its place. It opens at nought, where POSITION is
  exactly where the knob is. (It always drifted, by 15 %, until this build: a snapshot from
  then has WANDER at 30 and sounds as it did.)
- **FROM SAMPLE**: TABLE SAMPLE's frames. 8, each brought to one loudness, as it always was;
  64; or 64 WITH LEVEL, as loud at each place as the recording was there.
- **A file loaded or dropped on the lane while its MODEL is WAVETABLE becomes its table**
  (eighty-sixth build): TABLE goes to FILE for a wavetable file, else to SAMPLE with 64
  frames. CLASSIC and the eleven take no notice of a file. With 64 frames, **POSITION travels
  the part of the recording between its START and END** (the marks on the sample's display).
- **The display** shows the table, its frames one behind another, with the place lit.
- A snapshot from before this build that has a lane on WAVETABLE is read so that it sounds as
  it did: your SNAP 087, 088, 092 and 101 render sample for sample the same with their
  MOTIONs off.

**GRANULAR** (below) plays grains of a sample.

**Slices, loops and starts** (seventy-ninth build), in the same SAMPLE box. A file's own onsets
cut it into slices, every one found, or eight equal parts where there are fewer than two
between START and END.

- **NOTES** (BASS, LEAD, CHORD, PAD): **PITCH**, a note plays the sample at its pitch;
  **SLICES**, a note chooses a slice: C3 the first, each key above it the next, round again past
  the last, each at the pitch it was recorded at (TUNE shifts it). One recording is a kit, or a
  set of phrases.
- **LOOP** and **CROSSFADE** (with PLAY HELD): what plays, START..END or the note's slice, goes
  round for as long as the note is held, its end faded into its start. No crossfade: a hard
  join, for a loop cut on its beat. Half: no seam, for a texture.
- **VEL START**: a soft note starts further in (at full, the softest half way through).
  **RANDOM START**: each note starts somewhere else.
- **SLICE** and **CYCLE** (KICK, the hats, CLAP, PERC, RUMBLE): SLICE is which slice a hit plays
  (all the way down: the whole, as before); CYCLE ROUND plays the next one each hit, RANDOM any.
- **Slice markers** (every lane that plays a file; eighty-third build): on the sample's display,
  drag a cut's line to move it, double-click to put a marker in, double-click a marker to take
  it out. The first time, the cuts that are there (the file's own onsets, or VOICE's SLICE MODE)
  become the markers and **MARKERS** in CONFIG's SAMPLE box goes on: the slices are then START
  and each marker between START and END. Up to 32, each a place in the whole file. MARKERS off
  gives the file's own cuts back and keeps the markers. A snapshot holds them. START and END are
  taken by a press within ten pixels of them (a press anywhere used to move the nearer).
- **LOOP** (the hats, CLAP, PERC; eighty-first build): a file on a drum lane is a hit, started
  again by every step and cut by DECAY. With LOOP on it is a loop: START..END is taken to be a
  number of bars (AUTO: the power of two it is nearest to at the tempo; or 1 to 32) and goes
  round on the rig's bars while the clock runs, at the rig's tempo. Its pitch moves with the
  tempo's ratio to its own, as a record's does; nothing is stretched. Where it is in the file is
  where the clock is in those bars, so it cannot be put out of time. DECAY right up plays it
  through (choosing LOOP puts DECAY there); lower, the lane's steps open it, each for as long as
  DECAY says: the loop chopped by the pattern. TUNE moves it by whole octaves, twice or half the
  speed. Stopped, it is silent. CONFIG's line under the display says what it is taken for and
  the tempo it was made at ("LOOP 8 BARS AT 131.1").
- **PERC has SAMPLE** (eighty-first build), a seventh model after its objects, with everything a
  hat's sample has. On it the lane's sound knobs are TUNE, FILTER RES, ECHO SPACE, ECHO SWING,
  WIDTH (the middle: as recorded), ACCENT, TONE and DRIVE, each where a hat has its like.
- **Stereo** (eighty-first build): every lane that plays a file plays its left and right: the
  note lanes' SAMPLE and GRANULAR (through the filter, both sides), KICK, the hats, CLAP and
  PERC on SAMPLE, RUMBLE's SAMPLE (under its WIDTH), VOICE's SAMPLE and STUTTER, and KICK
  FORGE's impact. VOICE's RADIO, VOCODER, ROBOT and CHOIR work on one signal. The master folds
  everything under 150 Hz to the middle.
- **RESAMPLE**, a key beside LOAD: the last four bars the rig played (its whole output, from a
  bar line) become this lane's sample, written into the library's samples as RESAMPLE and the
  date and time. What the rig just played is then something to slice, loop or make grains of.
  The lane's model is left as it is.
- BASS, LEAD, CHORD, PAD and PERC read **five minutes** of a file (a field recording); the
  other drums one minute. A file is read whole into memory as it is loaded: five minutes is about a quarter of
  a gigabyte and a moment's pause.

All of these open where they change nothing, a snapshot holds them, and a snapshot from before
them recalls with them at rest.

**GRANULAR**, a MODEL of BASS, LEAD, CHORD and PAD, plays grains of the file from between START
and END (of its own source with no file). While it is a lane's model the lane's first six knobs
are its own, named so on the panel, and what those places moved before is in CONFIG:

| knob | |
|---|---|
| POSITION | where in the source the grains come from |
| SCAN | how fast that place travels while a note is held: stopped in the middle (a held note is one frozen moment), forwards to the right, backwards to the left |
| SIZE | a grain's length, 5 ms to half a second |
| GRAINS | how many overlap: gaps between them, to eight at once. On a CLOCK: how likely each tick is to fire |
| SPRAY | how far from the place each grain may start |
| PITCH | how far each grain's pitch may stray |

and in CONFIG's GRANULAR box: **SPREAD** (PITCH by a few cents, by OCTAVES, or by FIFTHS AND
OCTAVES), **SHAPE** (a grain struck, rounded or held flat), **REVERSE** (how many play
backwards), **CLOCK** (FREE, or a grain a tick of the song's grid, 1/4 to 1/32 and triplets),
**FOLLOW** (NOTES: the note sets a grain's pitch; FIXED: the source at its own pitch whatever
the note), **SYNC** (the source's START..END spans 1 beat to 8 bars of the song and the place
is where the song is in them: a recorded loop holds the tempo with its pitch unchanged). Each
starts where GRANULAR was before these, so a snapshot that played it sounds as it did (a PAD
from before has its COLOR and DETUNE turned into the SIZE, SPRAY and PITCH they were).

**The lane EQ** (eightieth build), at the foot of every lane's CONFIG: eight bands, on a graph.

- A band is OFF, a BELL, a LOW or HIGH SHELF, a LOW or HIGH CUT (two on one place for a steeper
  one) or a NOTCH, at a frequency, with a gain and a width.
- **The yellow line** is what the EQ does. **The white shape** is what this lane is playing.
  **The coloured shapes** are what every other lane is playing: where they lie on top of each
  other is where they fight. **The keys down the left**, a lane each in its shape's colour, show
  or hide that lane's; **OTHERS** shows or hides them all (with OTHERS off, a lane's key shows
  that lane alone).
- **Drag a band's point**: left and right its frequency, up and down its gain (the width of a
  cut or a notch). **The wheel** is its width. **Double-click** a point to put it back, or the
  graph to make a free band a bell there. The numbered keys choose a band; the keys beside them
  say what kind it is.
- **Bands 1, 4 and 7 are the panel's LOW, MID and HIGH knobs.** Turn the knob and the point
  moves; drag the point and the knob follows. As they start they are the three-knob EQ exactly.
- **COSTS 3.2 DB**, or ADDS, beside the band's name (eighty-fourth build): the lane's level
  after the EQ against before it, as it plays. Amber past 3 dB.
- **LOW END**, beside OTHERS (eighty-fourth build): the graph gives its place to **one bar, and
  what is under 200 Hz of the kick, the bass, the rumble and this lane** (white) as each reaches
  the mix. Where two shapes stand on the same part of the bar they share the subs there. **This
  lane's pocket hangs from the top in yellow**, on the same scale: drag on the bar, **down for
  a deeper pocket, right for a longer one** (its DEPTH and SHAPE). The keys down the left show
  or hide a lane. **AFTER FX**, under it, makes the pocket after the lane's insert effects and
  EQ, so that a reverb's or a delay's tail is held out of the kick too; off, it is made in the
  instrument and the effects ring on through it. LOW END again: the EQ.
- A snapshot keeps all of it, a stack knob sweeps it (frequency, gain and width travel; a band's
  kind changes half way), and the E16 has it on the last pages of CONFIG, a row a band.

**Everything in CONFIG keeps its place.** The boxes that are always there come first, in one
order on every lane, with MODEL the first control of all; MOTION follows them; the boxes that
come and go with a model (its own controls, SAMPLE's, and USUAL KNOBS: the knobs a model's own
took the places of) come after. Choosing a model moves nothing that was there before it.

**A model with knobs of its own** is the rule GRANULAR and KICK FORGE share: the lane's first
eight knobs are the model's while it is on, a snapshot holds them, and a stack knob or a recall
that changes the model changes what the knobs are with it. The E16's own little screens keep
the lane's usual names on those knobs (its scene is sent once); the knobs do what this screen
says.

**KICK FORGE**, the kick's eighth model (seventy-ninth build): one kick of three parts tied to
each other, and one knob through four kicks.

| knob | |
|---|---|
| CHARACTER | mellow and dubby, then punchy and tight, then boomy, then distorted: one curve with no step on it, at one loudness |
| IMPACT | the front of the hit, up to nine hundredths of a second dying away: a piece of the lane's file (LOAD a field recording), or noise with none. The one part that is not the same hit after hit |
| BODY | a sine that starts at the same point of its wave every hit and falls onto the kick's note (the key's root with KEY FOLLOW); its lows clean, its mids driven |
| GROUND | the body's own tail, held under 150 Hz, rung at the kick's note and held down while the body sounds: what is heard between the kicks |
| POSITION | where in the file the impact is from, each place brought to one strength, so another place is another thing struck. With no file: the colour of the noise, a low knock to a bright tick |
| TONE | darker and plainer, or brighter and richer |

with TUNE and DRIVE where they were and DECAY, DENSITY, FILTER and LEVEL as on every model.
PUNCH (how far the pitch falls), PITCH TIME (how fast) and CLICK are in CONFIG and move it from
where CHARACTER has them. **Chosen by hand it puts LAYER at nought and SPLIT off** (the rig's
kick is a 909 over an 808 with a crossover that takes the 909's own lows out: left on, FORGE
would have no low end of its own); both are in CONFIG. While it is on, the kick's module is a
dark red plate on the dark instrument. None of it has been heard: see Not verified in DECISIONS.

**Every knob is heard, on every model.** `--audit` (see Checks) turns each of a lane's twelve
knobs down and up and measures what the lane's sound does, for every model the lane has (85
lane-and-model pairs, and the seventy-ninth build's four more, each audited by itself: KICK
FORGE, and GRANULAR on BASS, LEAD and CHORD). Every knob moves the sound on every one, with one
exception that is the
music's and not the knob's: GLIDE on BASS while its steps are all one note (it slides between
two). Where a model had nothing for a knob to do, the knob was given something that suits it:
a filter for the models that had none, the ensemble for SPACE, a second voice a few cents off
for DETUNE, a darker-or-brighter tilt for COLOR.

Measured, not assumed: `--samplecheck` (see Checks) plays a file through a lane and writes what
came out; after PLAIN it is level with the file across the spectrum to a tenth of a decibel.

### TRANSITION: a riser into a change, a hit on it

In the bar of the main and the macro pages, beside NEW. Two one-shots timed to a bar
line: a **LEAD-IN** that ends on it and a **HIT** that starts on it. You say when; the machine
keeps the time, so the lead-in ends and the hit lands on the sample the line falls on.

- **FIRE** (the key, or **T**): the lead-in from now to the next line, the hit on it. Lines are
  every 4, 8 or 16 bars (BARS); pressed under a beat from one, it is the one after. While it is
  on its way the key is lit and says the bars left. With the clock stopped the hit plays at once.
- **ARM** (the plate under FIRE, or **G**): lit, the next recall, mute or solo does not happen
  at once. It sets the transition off and happens **on the line the transition lands on**, with
  whatever else you ask for meanwhile (the plate counts what waits). So: ARM, un-mute the hats;
  the riser runs, and the hats come in on the line with the hit. ARM then goes out. Put out by
  hand while things wait, they happen at once. A mute or a solo that waits is made by the audio
  thread, on time whatever the screen is doing, and one that arrives by MIDI (the E16's keys)
  waits as one pressed here does. A recall that waits is made by the screen's thread, most of
  a beat before its line.
- **What it plays**, each plate stepped by a press: BARS (4, 8, 16); its level; **IN** (OFF, the
  built-in RISER, or a file); **HIT** (OFF, the built-in IMPACT, or a file); DUB and SPACE (how
  much of it also goes into the two returns: a stab with DUB up is thrown into the delay).
- **A file**: drop a sound file on the IN or the HIT plate. A lead-in's file starts when it must
  to end on the line (with less time than it is long, part way in, faded up); a hit's plays
  from its start on the line.

**In the arrangement** each part's card has an **IN** and a **HIT** plate: lit, that part is
come into with a lead-in that ends on its bar line, or a hit that lands with it, by itself as
the arrangement plays. Amber is the sound the bar's plates say; drop a sound file on a plate and
it is that part's own (green). A press steps a plate through its own file, the bar's sound and
off. Each part also has its own **N** key, last of its row: lit (as a part starts) the part
brings its snapshot's notes, whatever the library's NOTES says.

It goes into the mix before the master, so the master's limiter holds it, and has a stem and a
recorded track of its own. What it plays is kept with the session, not in a snapshot; a file
given to it is copied into the library, as a lane's sample is. Not on the E16 or a pad yet: the keys T and G, or the mouse.

### A pitch wheel

A pitch wheel on a lane's MIDI channel bends that lane (eighty-third build): it adds to the
lane's TUNE, two semitones either way at its ends. BASS, LEAD, CHORD, PAD, RUMBLE and VOICE
follow it while a note sounds; a drum takes it as each hit starts. The E16's port is not
listened to for it.

### GAIN: the whole mix, into the master

A knob on the MASTER panel, under TAPE (eighty-second build). It turns the whole mix up or down
by as much as 24 dB on its way into the master chain; every lane and return keeps its place
against the others, and the middle is no change. **LIMIT only lifts a mix that is within 6 dB of
its figure** (further under, it takes it for a breakdown and holds), so a set that reads far
under LIMIT on the LUFS readout is brought up here until it is within reach, and LIMIT does the
rest. What comes out is a little under LIMIT's figure, more the harder it is pushed (the rig as
it opens: -9.6 LUFS at -9, -8.3 at -7.5, -7.1 at -6). Kept with the set, as TAPE, COMP and LIMIT
are, and not in a snapshot. Not on the E16.

### What the master says, CLIP, and TRIM held

Eighty-fourth build. **The meter's display has the figures over its bars**: the set's **LUFS**,
and what the limiter (**LIMIT**) and its clipper (**CLIP**) are each taking off, in dB. They are
the limiter's own figures.

- **CLIP**, a knob under COMP: the limiter's clipper. It rounds the tops of the peaks off before
  the limiter has to pull the whole mix down for them. Up: more of the peaks taken by it, a
  louder and harder set. Down: the limiter does it all. At nought it is out; it opens at 30 %,
  where it always was. Kept with the set, not in a snapshot.
- **TRIM**, a plate under LIMIT: it says the limiter's own trim, which follows the loudest of
  the last minute towards LIMIT's figure. **Pressed, the trim is learnt and held** (HELD,
  amber): it stays where the last minute's loudest put it, so a breakdown is not lifted and a
  drop is not pulled down. **From then LIMIT's knob moves nothing and what is played louder
  comes out louder**, up to the ceiling. Pressed again, it follows the set. Kept with the set.
- **Each lane's title has its loudness beside its name**: short-term LUFS as the lane reaches
  the mix, "--" when it is silent. (INFO shows the lane's channel there.)
- POCKETS' key is under TRIM, with AMOUNT's number on it.

### FINISH: the master's last stage before its limiter

A plate on the MASTER panel, beside the title. A press steps it: **OFF, 25, 50, 75, 100**; lit
while it is in. It does to the whole mix what a mix-bus finisher does, in three bands (under
150 Hz, between, over 2.5 kHz):

- **the middle band is held most**, gently and from far down (1.28 to 1), slow to take hold so
  that a hit gets through, a quarter of a second to let go;
- **the lows are all but left alone** (a twelfth of what is over), let go over seconds, so the
  kick is ridden and not pumped to; **the highs are held only when they are loud**, and fast;
- **each band is given a little of its own third harmonic** (twice as much in the lows), in
  each band apart, so the bass leaves no mark on the highs;
- **the middle of the spectrum comes up 1 dB and the top is eased 1.6**; **the sides come up
  3 dB where they are bright** and are otherwise held as the middle is.

What each part does at 100 was fitted to measurements of a finisher plugin Wiley uses on his
prints (`DECISIONS.md`, 560 and 561, has the two side by side). It **finds its own level**: the
loudest three seconds of the last minute are put where the bands were measured, whatever level
the set reaches it at, so it does the same to a quiet set as to a loud one. And it **gives back
what it takes** (1.75 dB at 100), so a press of the plate changes the sound and not the level:
through the whole master, -9.5 LUFS with it off and -9.4 at 100. **It does not make the set
louder: LIMIT does that.** It adds no delay. At OFF the samples pass untouched.

Kept with the set, not in a snapshot; a print has it (in the set's file: the lanes' files are
from before the master). Not on the E16.

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

#### The rack: a stack is a pocket with a pile of chips in it

The library is the chips, and they are kept once: **the bin** holds every one. A **stack** is a
pocket in the rack along the top of the page, with a pile in it: the chips it refers to, in
an order. A chip can be in as many stacks as you like, and it is still one chip, in the bin.
Taking it out of a stack never takes it out of the bin; deleting the stack leaves its chips.
On the 3D stage a pocket is a tray with a lip round it, and the pile in it is a chip for
every chip the stack holds, one standing on another (ten at most are drawn).

Down the left of the rack are its four shelves: **STACKS**, **KNOBS** (knobs kept by
themselves), **MODULES** and **ARRANGEMENTS**, each with how many it holds. The first pocket
of every shelf is the bin, **ALL CHIPS**. The wheel over the rack goes along a long shelf.

**Press a pocket and it is open**: what it holds is laid out on the bench under the rack, and
its rim is green. Press the bin's pocket to put it away. **What is open has its own keys,
as plates after its name** at the head of the bench; one that cannot be pressed now is an
empty frame. The strip at the foot of the page has RENAME and, for the chip in hand, DELETE,
RECALL and ADD TO, then PHONES and NOTES.

**RENAME is one key, in the same place on the library's, the designer's and the
arrangement's strip, and it names the thing you pressed last**: a chip (in the bin, in a
stack's row, in a knob's pile, on a part of the arrangement), a stack, a knob kept, a module
or an arrangement (by its pocket), a knob of the module (by its place in the designer). Press
the thing, press RENAME, type, Return. With nothing pressed on a page yet it names the page's
own: the chosen knob in the designer, the arrangement on its page. The box opens over the
name where you pressed it.

**A key that must be held fills with light while your hand is on it, and does what it does
when it is full** (about two thirds of a second): DELETE on a chip, DELETE after an open
thing's name, CLEAR on a knob and on the arrangement, NEW on the top bar. It goes dark as you
press it and lights from its left. Let go before it is full and nothing is done; the bar says
to hold it. None of them says HOLD: the light does.

What a knob kept, a module's knob or an arrangement holds may be more than the bench has room
for: it shows the lines that fit above the strip, says which (1 TO 4 OF 30), and the wheel
over them goes through the rest.

**The same rack is the right-hand third of MACROS and of the arrangement's page**: the same
shelves (in a row there), the same pockets (four across; the wheel over them goes along),
the same keys after an open thing's name. What is chosen on one page is chosen on all three.
On those two pages the bench under the pockets holds the chips to take from: the bin's, or
the open stack's in its order. A stack's row is put in order here, in the library.

| WHAT YOU WANT | HOW |
|---|---|
| a new stack | press the empty pocket, **NEW STACK**. It is open at once, with nothing in it |
| a chip into the open stack | carry it up from the bin to the place in the row where it should go; or press it and then **ADD** (it goes last) |
| a chip into a stack that is not open | carry it from the bin onto that stack's pocket; or press it, press **ADD TO** (it stays lit), then each pocket it should go into. The empty pocket makes a new stack with it in |
| another order | carry a chip of the row to another place; or press it and then **EARLIER** / **LATER** |
| a chip out of the stack | press it in the row and then **TAKE OUT**; or carry it off the row. It stays in the bin and in every other stack |
| a chip of this stack into another too | carry it from the row onto the other stack's pocket: its keys go with it |
| which parts of the rig it takes there | the keys on its card in the row (1 to 11, R, B, K), **N**: whether a knob coming to it brings its notes, and **M**: whether a knob turned toward it blends each lane's model and sample to its own |
| the stack on a knob | **ON A KNOB**: a copy of it on the first stack knob that holds nothing. The stack stays in its pocket, and what is changed on the knob afterwards does not change it |
| the stack again under another name | **COPY**: the copy is the one open |
| its name | press its pocket, then **RENAME** on the strip; type and Return |
| the stack gone | **DELETE**, held until it is full of light. Its chips stay in the bin. (With ONLINE lit it goes from the web library's rack for the instrument too) |

While a stack is open the bin is still under its row, every chip of it, and the chips that are
in the open stack have a green line along their near edge. With a chip in hand, the pockets of
the stacks that hold it are ringed, and the strip says which stacks those are. A chip deleted
from the bin (DELETE on the strip, held) is out of every stack that held it, and the bar
says how many. A stack that is the web library's is changed there: the keys on its row do not
press, and the bar says so if you try to add to it or take from it.

The other shelves open the same way:

| POCKET | ON THE BENCH | KEYS |
|---|---|---|
| a **knob kept** (a stack knob: a pile; a macro knob: a cap) | a stack knob's pile, out in a row and changed as a stack's is (below); or a macro knob's ties, each the knob it moves and how far, changed there too (below) | the row's **ADD, TAKE OUT, EARLIER, LATER** (a macro knob: **TAKE OUT**); **ON A KNOB** (the first free place of its kind), **DELETE** |
| a **module** (green: the one in the chassis) | its knobs. Press a stack knob and its pile is out in a row under them, changed as a stack's is; press a macro knob and its ties are out under them, changed as a kept macro knob's are | **NEW**, **COPY**, **INTO THE CHASSIS**, **DELETE** (never the one in the chassis); beside the pressed knob's name the row's **ADD, TAKE OUT, EARLIER, LATER** (a macro knob: **TAKE OUT**) |
| an **arrangement** (green: the one on the page) | its parts in order, each with its chip, its bars and the parts it takes | **NEW**, **COPY**, **ONTO THE PAGE**, **DELETE** (never the last one) |

**Every chip you can see plays when you press it**: in the bin, in a stack's row, in a kept
knob's pile, on a module's knob, in an arrangement's parts. Where it is shown with the parts
it takes there, those are the parts you hear, with its notes if it would bring them. Pressed
again, it stops. Two quick presses take it to the set, as RECALL does.

**A pile is changed the same way wherever it is kept**, in the library: a stack's, a stack
knob's kept by itself, a stack knob's of any module. It is out in a row with the bin under
it; a chip is carried up from the bin to its place (or pressed, then ADD), carried along the
row (or EARLIER / LATER), carried off it (or TAKE OUT), and its keys say which parts it takes
there. What changes is the thing that is open: the knob kept (not a knob it was once put on),
the module where it is kept (INTO THE CHASSIS then brings the change with it), or, for the
module in the chassis, the knob itself as it plays.

**A macro knob's ties are changed where it is opened**, in the library: a macro knob kept
by itself, or the macro knob you press in an open module. Each tie is a plate: the knob of
the rig it moves, and a figure, how far the macro knob's full turn takes that knob, in
hundredths of its travel (DEPTH, in MACROS, scales them all).

| WHAT YOU WANT | HOW |
|---|---|
| a tie that takes its knob further, less far, or the other way | drag its figure up or down (three points of the hand to a hundredth). It goes from +100 through +1 to -1 and on to -100: a tie is never nothing |
| a tie gone | press it (it turns green), then **TAKE OUT**. That knob of the rig is no longer moved by the macro knob |
| a new tie | **LEARN**, in MACROS, on the module in the chassis: a tie is made by turning the knob it should move (see A macro knob: LEARN) |

What changes is the thing that is open, as with a pile: the knob kept (not a knob it was put
on), a module where it is kept, or, for the module in the chassis, the knob itself as it
plays. With that knob turned up you hear a figure change as you drag it. A knob that is being
taught is left alone until KEEP or DROP.

**On MACROS and beside the arrangement the rack is for taking from.** An open pile shows its
chips there, to hear and to add from, sorted by the head of the list if you like (RECENT,
FITS NOW, MORE, LESS, FIND); a macro knob's ties are shown and not pressed. A pile is put in
order, and a tie changed, here in the library. (Your choice, Nov 16: the finders stay in
force inside a stack on those two pages.) The kept shelves have no find row.

#### The rack on the keyboard

In LIBRARY, MACROS and ARRANGE the rack works without the mouse. The first arrow key brings
out **a mark, four yellow corners,** round the pocket that is open (the bin, with nothing open); after that the
arrows move it over everything that is pressed there: the pockets, the chips on the bench, a
pile's row, the knobs of an open module, a macro knob's ties; in MACROS the places of the
module in the chassis and the chosen knob's pile too, and beside the arrangement its parts.
It goes to the thing in line with it that way; at the end of a line of chips, right goes on to
the next line. A list longer than its place goes by under the mark (a long shelf of pockets,
a pile's third line, the bin, the chosen knob's pile), then the mark goes on to what lies
beyond it.

| KEY | WHAT |
|---|---|
| arrows | move the mark |
| **Return** | press what the mark is on: a pocket opens, a chip plays in the headphones (again: it stops), a knob of a module shows what it holds, a place is the chosen knob |
| **Shift-Return** | in the library, on a chip: RECALL. It goes to the set, with the parts it takes where it is shown |
| **=** | ADD the chip the mark is on: in the library into the pile that is out, last; in MACROS onto the chosen stack knob's pile; beside the arrangement a part after the chosen one |
| **Backspace** | TAKE OUT what the mark is on: a chip of a pile (it stays in the bin), a tie, a part |
| **[** **]** | EARLIER, LATER: a chip in its pile, a part in the arrangement. The mark goes with it. On a tie: a hundredth less, more (with Shift: ten) |
| **Tab** | the next shelf (Shift-Tab: the one before); the mark is on its first pocket |

Where a thing is chosen by being pressed (a chip of a pile, a tie, a part) the mark coming to
it chooses it, without playing it: it is lit as the mouse would light it, and the keys on the
page (TAKE OUT, EARLIER, LATER) are for it too. **A press of the mouse puts the mark away**,
and so does leaving the page. With the mark away, =, Backspace, [ and ] are for what the
mouse last chose, so a chip pressed with the mouse can be walked along its pile with ]. The
keys a chip's parts are put out with (1 to 11, R, B, K, N), RENAME, and the held keys
(DELETE, CLEAR) are the mouse's. INFO (**I**) lists these keys with the rest.

Nothing of the rack is on the E16: the knobs you play are, and the rack is where a set is put
together beforehand.

#### Finding a snapshot

Wherever chips are listed (the library's bin, and the rack's bench in MACROS and beside the
arrangement) the list has the same head, and what is chosen there is chosen on all three:

| AT THE HEAD | WHAT |
|---|---|
| **THE BIN 37** / a stack's name | whose are listed, and how many. It is not a key: the rack's pockets say which stack is out (the bin's pocket: every chip again) |
| **RECENT** | the newest first |
| **FITS NOW** | nearest in sound to what is playing, first (only snapshots with a sound kept) |
| **MORE**, **LESS** | a step up or down in energy from what is playing, the nearest step first: the twelve nearest it in colour, as the E16's MORE and LESS knobs hold |
| **FIND** | part of a name, typed, then Return: only names that hold it. Pressed while lit: all again |

The lit one is black. FITS NOW, MORE and LESS listen to the set when they are pressed, and the
list stays put until the plate is pressed again. They work together: a stack's snapshots,
nearest what is playing first, whose names hold DUB.

#### A chip pressed plays, and carries its keys

One rule for every page that shows snapshots (the library, MACROS, the arrangement):

- **Press a snapshot and it plays in the headphones**: its four bars, on outputs 3 + 4, looped.
  While the clock runs they play in time: on the rig's bars and at the rig's tempo (so the pitch
  moves with the tempo's ratio to the clip's own). The set itself is not touched. Press it again
  to stop, another to hear that one. Outputs 3 + 4 have to be ticked in SETUP once, or PHONES lit.
- **It is then the one in hand**: its card is ringed, and where the others draw their sound it
  has **a key for every part of the rig** (lanes 1 to 11, R the returns, B the drum bus, K the
  key). Put a part out and it is not heard, and does not go with the snapshot in what is done
  next.
- **What is done next is the page's own key**: **RECALL** in the library (two quick presses on
  the card do the same), **ADD** or carrying in MACROS and beside the arrangement.
- **If nothing will be heard, the bar at the top says why**: the snapshot has no sound kept
  (taken with the clock stopped, or one of the rig's own), or the headphones have nowhere to
  play. **PHONES** is on the strip of the library and of MACROS, and **H** anywhere.

In the library a press used to recall the snapshot at once unless CUE was lit. There is no CUE
key there now: a press never changes the set, and RECALL is what does.

#### A cue is the rig itself

What plays in the headphones for a snapshot is **the rig as a recall of it would leave it now**,
played by a second rig that runs beside the first. (It used to be the four bars recorded when
the snapshot was taken: that day's notes and tempo, all of the rig whatever keys were out, the
master as it stood. That is still there, as THE CLIPS: see below.)

- **What it is made from.** When a cue is asked for, the second rig is made what the first is
  at that moment: every knob, every lane's instrument, effects and files, the macro knobs' and
  MOTION's pushes, the master, FINISH, the tempo; and each lane's sequencer is given what that
  lane is playing, the notes coming in from outside as steps. Then the recall is made on it
  as it would be made on the set: the parts lit on the card (a part put out stays as the rig
  has it), and the snapshot's notes if that recall would bring them (NOTES in the library; a
  stack knob's N for its snapshot; a part's N in the arrangement). It arrives at once, with no
  GLIDE: the cue is where the recall ends up.
- **In time.** While the clock runs the second rig is on the same sample of the same bar as
  the first. With the clock stopped it runs on its own, so a cue is still heard.
- **It takes a moment to be ready**, and nothing plays until it is (no clip is played first).
  SET UP says how long the last one took. One asked for while another is being made ready
  takes its place.
- **Every snapshot can be heard**, also one taken with the clock stopped and the rig's own,
  which have no clip.
- **With every key of the card out the cue is the rig as it stands** (that recall would change
  nothing). With a clip it was silence.
- **It is the rig as it stood when the cue was asked for.** A knob turned on the set after that
  is not in the cue: press the snapshot again and it is made again from the rig as it is then.
- **Not in it yet:** a place between two snapshots of a stack knob. With CUE lit a turn still
  goes from snapshot to snapshot.

The second rig is this program started a second time by the instrument, as a process of its own
with no window (`SF-000 --sf000cue:…`). It has its own memory, its own bus between its modules
(its kick does not duck the set, its lanes are not among POCKETS' sources, its KEY is not the
set's) and its own core. The set's audio thread writes where its clock is into a block of
shared memory and reads the second rig's sound out of it, and never waits: if the second rig is
late, the headphones are silent for that buffer and SET UP counts it; if it is gone, the
instrument says so, plays the clip instead, and starts another (three tries). It writes
nothing to the disk and goes when the instrument does.

**SET UP has the choice: CUE, THE RIG or THE CLIPS**, kept for this computer (`cue.json`), and
under it what the second rig is doing: whether it is up, how long the last cue took to be
ready, what it takes of its core while it plays, and any gaps. THE CLIPS puts the second rig
away, and a cue is the snapshot's kept bars as before. (`SF000_CUE_CLIPS=1` in the environment
starts it that way.) The bars are still kept with every snapshot either way: the cards and the
finders are drawn from them.

**PHONES** (beside CUE, or **H** on any screen) is for playing on headphones with only one pair of
outputs. While a cue plays, outputs 1 + 2 carry the set in the left ear (both its sides, summed)
and the cue in the right; when the cue stops, the set is back in both, over 10 ms. With no cue
playing nothing is different. A small PHONES plate under SETUP's name, at the top of every page,
is green while it is on; pressed, it turns PHONES on or off. A recording, the meters and a
snapshot's kept bars are the set itself whichever way it is. It is off every time SF-000 opens,
so it cannot be left on for a PA.

Also kept, and not drawn yet: weight (the low end's share), density, tone against texture,
movement from bar to bar, and how wet (the returns' share). `SF-000 --profiles` prints them for the
snapshots the rig comes with and some plain cases. A snapshot taken with the clock stopped, or
from before this build, has no sound kept: its card shows how busy each lane's pattern is, as before.

The bars kept are ones the rig held the snapshot's state through: nothing changed in them and
nothing was still travelling. Press SNAPSHOT on a state you have been listening to and its sound
is there at once. Press it the moment you arrive (or while a knob glides) and the snapshot is taken
at once, but its card fills in a few bars later, once four bars of that state have played. If you
move on first, it keeps the bars that were held; if there were none, the bars it was taken over.

**A recall of part of a snapshot.** The keys on the card of the snapshot in hand say what RECALL
takes: lanes 1 to 11, R (the returns), B (the drum bus) and K (the key). Click one to put it out:
that part then stays as it is, steps, inserts and CONFIG included, and RECALL takes the rest.
After the recall all are lit again. A snapshot knob and a sequencer's RECALL take everything, as
always.

**What the scope leaves lit is what is in the headphones.** A snapshot keeps its lanes beside its
four bars: each lane, the two returns and what the drum bus did, as they went into the mix
(`clips/<id>.stems`, about a megabyte). Put a key of the row out and the cue becomes the lanes
still lit and no others, in time as before: the hat alone, the bass and its echo, whatever the
recall would bring. All lit again, it is the clip as it was heard. With PHONES lit that is the
right ear; the left is the set exactly as it goes to the PA, whatever the scope. Two things to know. A lane's
echo and reverb are in RET, not in the lane (one tail for all the lanes that fed it). And lanes
heard this way have not been through the master, so they are a little quieter and plainer than
in the whole clip. A snapshot taken before this build has no lanes kept: its cue stays whole.

### What a snapshot holds of the playing

A snapshot is meant to come back sounding as it did, whoever was playing the lanes when it was
taken. So besides the sound it keeps, for every lane:

- **its own steps**, as always;
- **the notes that were coming in from outside** on the lane's channel (the OXI One, a keyboard),
  written down as steps. SF-000 listens while the clock runs and keeps the last bars: a part that
  comes round in one, two, four or eight bars is kept as that many; one that never comes round,
  as its last bars (up to eight). A note up to a quarter of a step early or anywhere later in a
  step is that step's; two to four hits inside one step are a ratchet; a note's length is the
  step's gate, its velocity the step's. The notes are also kept exactly as they came, for a
  later build to do more with;
- **whether the lane's own sequencer was muted** (the M in the sequencer strip), so its steps
  are known to have been silent;
- **the sample it held** (a lane's audio file: a phrase on VOICE, a sample on a lane whose model
  plays one).

**NOTES** says what a recall does with the playing. It is a key on the snapshot module (beside
its name), in the library, and **N**. Every recall goes by it: a tap in the library, a snapshot
knob's press, a sequencer's RECALL.

| NOTES | A RECALL |
|---|---|
| **lit** (how it starts) | puts into each lane's sequencer what the lane was playing: its own steps; or the notes from outside as steps; or both together where both were sounding (where a note came from outside, that note). The sequencer's M is set to match: off where there is something to play, on where the lane's steps were muted and nothing was playing it. With nothing playing into SF-000, the snapshot sounds as it did |
| **out** | changes the sound and leaves every sequencer exactly as it is: its steps, its generator, its M. For when the OXI One is playing the lanes: no part is doubled |

- With NOTES lit and the OXI One still playing a lane, that lane has its notes twice. Put NOTES
  out, or mute the track on the ONE.
- A snapshot taken before any of this was kept (before the twenty-ninth build) holds its lanes'
  own steps only: recalled with NOTES lit it gives those and leaves the M keys alone, as recalls
  always did.
- What steps cannot hold is lost in the writing down: timing finer than a sixteenth (swing, a
  note played early or late), more than one note on a step (the lowest is kept), a part longer
  than eight bars, notes played while the clock was stopped.
- The sample is the library's own copy of it (see Where everything is kept, below), so it comes
  back whatever has become of the file it was loaded from. A snapshot that held no file on a
  lane leaves that lane's file alone. SPACE's impulse responses are still the rig's, as before.

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

**The notes come in when the glide is over** (eighty-second build). A recall that travels for
a bar or more leaves the notes of before playing through the travel; the snapshot's are taken
up on the first bar line at or after its end (exactly on it for a recall made on a bar line, as
an armed one and the arrangement's are). What belongs to the notes goes over on that line with
them: a lane's sequencer muted or not, and the key every lane follows. With GLIDE OFF, or a
glide shorter than a bar, the notes come on the next bar line as they always did, and those two
switches on the beat the knobs set off on (at once with GLIDE OFF). The step keys show the
snapshot's notes from the moment of the recall.

**In the arrangement** a part's MORPH is a glide, so a part's notes begin when its morph is
over. A part whose morph is as long as the part is one travel on the notes of the part before.

### The arrangement: snapshots in a line

**ARRANGE** on the main screen (under the snapshot knobs), or **A**. An arrangement is parts in a
line. Each part is a snapshot, how many **BARS** it lasts, and its **MORPH**: how long the way
into it takes, from its first bar. CUT: it is simply there. A morph as long as the part: the
whole of it is one travel from the snapshot before.

- **Making it.** The right-hand third of the page is the rack (see The library). Press a chip
  on its bench to hear it; its **ADD** makes it a part, after the one that is chosen, taking
  the parts of the rig lit on its card (a part comes in as the one before it is: the same
  bars, the same morph). **Carried onto the arrangement**, a chip is a part at its end; and
  **a stack's pile carried from its pocket onto the arrangement is a part for each of its
  chips**, in the stack's order, each taking the parts it takes in the stack. (The stack
  stays in its pocket. A chip's N in the stack is not carried: a part's own N says whether
  it brings its notes.) **A part has the same keys on its
  own card**: what it takes of its snapshot when the arrangement comes to it (the others stay as
  they are then; PRINT takes it the same way). A part pressed is the chosen one, and plays in
  the headphones with those parts. Drag
  a part's BARS (1 to 256) and its MORPH (the GLIDE list's own lengths; never longer than the
  part). A double-click puts either back (eight bars; cut). EARLIER and LATER move the chosen
  part, TAKE OUT takes it out, CLEAR (held until it is full of light) empties the line. Up to 64 parts.
- **The line** along the top is the whole of it to scale: each part as long as its bars, with
  the bar it begins on above it, and its morph as a slope. The part playing is green.
- **PLAY.** From stopped: the first part is recalled at once and the clock starts from the top.
  With the clock running: it comes in on the next bar line. **FROM HERE** plays from the chosen
  part, **with what every part before it left**: a part with a key out sounds as it does when
  the arrangement reaches it, not as the rig happened to stand. (Until the sixty-sixth build it
  recalled that part alone.)
  Each part after is recalled inside the beat before its bar line, so that everything in it sets
  off on the line: the knobs travel over the morph, the notes come in when the morph is over (on the bar, with a cut), and what cannot
  travel is crossed over, exactly as in any recall. A cut lands on its line too (it is a travel
  of a few hundredths of a beat). At the end the clock stops. RUN stopped by hand puts the
  arrangement down; a recall made by hand while it plays is yours, and the next part still comes.
- **What plays over it.** The snapshot knobs, where they stand, and MOTION: an arrangement is
  the scenes, and those are what you play on top. So is anything you turn while it plays.
- **EDIT**: the chosen part in the main view, without touching its snapshot. The arrangement
  stops, the rig is made what the arrangement has at that part (what every part before it left,
  then it, arrived), and it plays. The bar says which part, with **KEEP** and **DROP**.
  - **KEEP** (or Return): the rig as it stands is **a new snapshot in the library**, named after
    the part's (ACID OPEN, then ACID OPEN 2), and takes its place in the part. It holds the whole
    rig as you heard it, what the earlier parts set included, so it is right by itself: in the
    library, on a stack knob. **The snapshot the part had is not touched**, and other parts that
    use it still do.
  - **The part's keys** stay as they were, and those of whatever part of the rig you changed are
    lit (N, if you changed a lane's steps), so the arrangement plays what you heard and what you
    did not touch still follows the parts before. The page says which it lit. A part after this
    one with a key out takes that part of the rig from here on, as it did.
  - **Edited again**, the snapshot the last edit made is replaced by the new one, which takes its
    name: the library does not fill with every try. Unless something else holds it by then
    (another part, a knob, a stack): then it stays and the new one is numbered on.
  - **DROP** keeps nothing; the rig stays as you left it. Playing or printing the arrangement,
    or moving its parts, drops an edit too.
- **Arrangements are kept, and nothing has to be saved.** The page always holds one of the
  arrangements kept (its name is beside ARRANGEMENT on the strip; a session starts with
  ARRANGEMENT 1): what you change on the page changes that one. **ARRANGEMENTS** is a shelf
  of the rack: each one kept is a pocket (a rail, its parts as long as their bars; green: the
  one on the page). Press one and its parts are on the bench, in order; its keys are after
  its name:
  - **NEW**: an empty one, kept, and put on the page.
  - **COPY**: the one on the page kept again under another name. The page holds the copy from
    then on: change it and the first is as it was. (A track and its live version.)
  - **ONTO THE PAGE**: the open one onto the page, whole. The one that was there is kept as
    it stood. If the arrangement is playing, it stops.
  - **RENAME**, on the strip: the arrangement on the page, or the one whose pocket you
    pressed last. Type, then Return.
  - **DELETE**, held: the open one goes; its chips stay in the library. If it is the one on
    the page, the one before it takes the page. The last one cannot go: CLEAR, held, empties
    it.
  They are in the session file. A snapshot deleted from the library leaves their parts.
- **TO STACK** keeps the arrangement as a stack in the rack (TRACK, TRACK 2):
  its parts' snapshots in order, each with the keys its part has. On a stack knob the knob's
  turn is the track from first part to last. A part with a key out leans on what came before
  it, on a knob as here; the page says how many do.
- **PRINT** renders it to files, for a release: faster than it plays, and the same every time.
  A folder `PRINT <date>` where takes go (SETUP's folder for them). **What it holds is for a
  mastering engineer as much as for you**: every file is a stereo WAV at the device's rate, one
  length, bar 1 on its first sample, with what rings on after the last bar kept until it has
  died away; nothing is normalised, dithered or cut flat.
  - `00 SET.wav`: the track as the master finishes it, limiter and all (24-bit). What was meant.
  - `MIX BEFORE THE LIMITER.wav`: the same with the master's last stage left off (32-bit float,
    at the level it reached the limiter). What a mastering engineer starts from.
  - `MIX BEFORE THE MASTER.wav`: the mix as the master is given it, none of the master on it.
  - `STEMS/`: **add up to `MIX BEFORE THE MASTER.wav`, to the sample** (measured: what is left
    is 141 dB under it, which is the rounding of the adding). Each lane as it goes into the
    mix, the two returns, the transitions; and **the lanes that play through the drum bus as
    one file, `14 DRUM BUS.wav`, as the bus leaves them**, because what the bus does it does to
    them together. 32-bit float: a stem may go over full scale and is whole where it does.
  - `INTO THE DRUM BUS/`: those lanes one by one, as they go into the bus, for when the drums
    are wanted apart. Not to be added to the stems. (A take keeps a file of what the bus *did*
    instead; nobody downstream can use that.) **If the bus did nothing from first sample to
    last, there is neither**: each of its lanes is a stem of its own.
  - `PRINT.txt`: its length, its loudness, the parts with the bar and time each begins, and
    every file with what it is, its highest sample, and for the three mixes its loudness. It
    says so if a mix goes over full scale. Hand it over with the files.
  What is silent throughout is not kept. None of the master is on a stem: not its low end, its
  tape, its compressor, FINISH or its limiter.
  - It is made on **a rig of its own**, built there and then from the session as it stands, with
    every module starting from its beginning. So the rig you are playing is not touched, and
    printing the same arrangement twice gives the same file to the byte, however busy the
    computer is meanwhile (the print goes by its own time, sample for sample, not the wall
    clock's: before the thirty-seventh build a print held up by the computer could lose the duck
    under a kick). The sound stops while it is made (one rig sounds at a time) and comes back
    when it is done.
  - Each part brings its own notes, whatever NOTES says: nothing outside is playing. So what the
    ONE was playing has to be in the snapshots (it is, if they were taken while it played: see
    What a snapshot holds of the playing). No clock is sent or followed, and notes played in
    meanwhile are not part of it.
  - The page says how far it has got. PRINT again, or Esc, stops it and leaves no files.
- The arrangement is kept in the session file. A snapshot deleted takes its parts with it.

### ONLINE: the library on the web

With **ONLINE** lit (the library screen's strip), every snapshot you take goes to your snapshot
library on the web in the background: the snapshot whole, its four bars, and the definitions it
names. Nothing waits on it. With ONLINE out, or no network, a snapshot simply has not gone yet; it
goes when ONLINE is lit and the library answers. The lamp: green when the library holds every
snapshot taken, yellow while sending, amber when it waits (no answer, or not set up), red when
the library turned SF-000 away. The line beside it says which, and how many wait.

**SET UP** takes the library's address and the two strings of its service token (Client ID,
Client Secret). CHECK asks the library whether they let SF-000 in; KEEP stores them in
`online.json` with this computer's settings (not in the library folder, which may be on a drive
that travels), readable by you only. Neither the address nor the strings are in the code.

- **Its stems go too**: each lane, the two returns and the drum bus on their own (about a
  megabyte a snapshot). The web library plays a chip from them when keys are put out on it.
- **So does everything the library is to be studied from**, after the snapshots and never
  ahead of them: the log of what you do with chips, the session without its chips (your knobs,
  stacks, modules and arrangements), every sound file the rig has been given, and each
  snapshot's sound without loss (see [What is kept to be studied](#what-is-kept-to-be-studied)).
  The log and the session are small and go as they change. The sound files and the lossless
  sound are megabytes each: they go one at a time **and only while the rig is stopped**, so
  nothing large is read or sent under a set being played. While any of it has still to go the
  line says so (`ARCHIVE: 12 STILL TO GO`, or `TO GO WHEN THE RIG IS STOPPED`); the lamp is the
  snapshots' own and stays green. Nothing in the web library is changed by any of this: it is
  only added to.
- **So do the knobs of the snapshot module**, as they are made or changed here: a macro knob
  you have taught, a stack you have changed, a name. Each goes once it has stood unchanged for
  a moment, into its own place on the library's module if that place is free, else into the
  library off the module. A knob emptied here (CLEAR) comes off the library's module and stays
  in the library. A knob is only sent once the library has every snapshot it holds.
- **So does the rack, and it comes back the other way too.** The stacks in the rack (its
  STACKS shelf, on any page that has it) and the stacks the web library has lit for the instrument
  (TO SF-000 there) are kept the same, by themselves, while ONLINE is lit:
  - A stack kept here (the rack's empty pocket, or KEEP ITS PILE AS A STACK on a knob's card; once it holds a chip) is in the library's rack within a moment: lit for the instrument,
    off its module, each snapshot with the parts it takes.
  - A stack made or lit there is here within a quarter of a minute, in the library's order. A
    snapshot it names that this library has not got is brought first, whole, with its four
    bars; until it is here the stack is left alone on both sides.
  - Changed there, it changes here. Changed here (in its row in the library, or KEEP again under its name), it changes
    there. Changed on both since they last agreed: the instrument's is the one that stands.
  - Unlit or removed there, it leaves the rack here (its snapshots stay). Changed here since,
    it stays and goes back.
  - Its number here takes it out there too: a stack made there is only unlit for the
    instrument (the library keeps it); one made here is removed there (marked removed, not
    erased).
  - A song is the library's to arrange: it comes and follows, and is never sent.
  - A stack is only sent once the library has every snapshot in it. One holding the rig's own
    snapshots (which stay at home) stays here.
  The row of a stack in the rack says which it is: KEPT HERE (not there yet), HERE AND ON THE
  WEB, or FROM THE WEB.
- A snapshot renamed here is renamed there. One deleted here stays there.
- One removed in the web library is not sent again.
- The snapshots the rig came with (INIT and the rest, CLEAN SLATE) stay at home.
- Each time it connects it asks the library what it holds, so the two never drift.

The library itself is a bench: every snapshot a chip with a key for each lane, chips in stacks,
stacks that fill themselves, a column where a stack knob is built, and a **module**: its model
of this one's sixteen knobs (its own README, in the repo `snapshot-library`).

**LOAD** (on the snapshot module, and beside SET UP) makes the sixteen knobs what the web
library's module holds now. It sees to the rack first, as ONLINE does, whether ONLINE is lit or
not. So there are two ways to fill a knob: from the rack here (a stack's ADD: no network, and
with ONLINE lit the rack is the library's too), or LOAD, for a whole module arranged in the
browser.

- **The knobs.** Places 1 to 4 there are macro knobs and 5 to 12, 14 and 15 stack knobs, as here (13 and 16 hold none).
  - **LOAD makes room.** A stack knob here that the library cannot be given (it holds the rig's own snapshots, which
    never go there, or ones ONLINE has not sent yet), on a place the module has another knob for, moves to the first
    stack knob that is free here and there, as it is. LOAD says so first: WHAT KNOB 5 HELD (TOUR) IS ON KNOB 6 NOW.
  Each snapshot of a stack comes with the parts of the rig chosen for it there (its keys).
  - A place's module goes onto its knob **whatever the knob held**. What it held is not lost:
    it went to the library when it was made (above), or goes now, and is in the library's
    rack, off the module, to be put back for another show.
  - A module **taken off the module** there comes off its knob here. The library still has it.
  - A knob **made here** is on the library's module already, or goes onto it now.
  - The same module on both sides: changed here since the two last agreed, what is here goes
    up and stays; changed there and not here, it comes down.
  - A knob is only ever replaced or emptied once the library has all of it. If one of its
    snapshots has not reached the library, or the library does not answer, the knob is left
    exactly as it is and the line says which. (The five snapshots the rig comes with never go
    to the library, so a knob holding them stays as it is.)
  - A place holding the other kind (from before the two kinds) is left, and the line says so.
- **The snapshots this library lacks**, whole, with their four bars and the definitions they
  name, under the name each has there. One this library has, renamed there, is renamed here.
- **The rack** (above): the stacks lit for the instrument there (TO SF-000) and those kept
  here, made the same. The plate at the head of the snapshots (ALL, or a stack's name) steps
  through them, each in its own order, then back to every snapshot.
- **A song**, as the arrangement, *if there is none*.

LOAD deletes no snapshot. What it brought is not sent back. With no answer from the library it
says so and changes nothing. What it did is said on the top bar for a few seconds, and stays on
the library screen. If a snapshot it brings names a sample that is not on this computer, it
says how many do: the web library does not keep samples yet, so that lane will be silent until
the file is here.

### Modules: a set of knobs for each set

The snapshot module's knobs are a **module**: its four macro knobs, its ten stack knobs, and
MOTION as it is set on them, under a name. The instrument keeps as many as you make and carries
one at a time. No network is asked.

- **MODULES** is a shelf of the rack, on any page that has it. **LOAD** on the snapshot
  module's own panel goes straight there: the designer, the rack on MODULES, the one in the
  chassis open. Each module kept is a pocket (a cartridge: four caps over ten pockets); the
  one in the chassis is green. Press one and its fourteen knobs are on the bench; press a knob
  there to see its pile, or what it is tied to (in the library both are changed there).
- **INTO THE CHASSIS**, after the open one's name, puts it in the chassis, whole. The one
  that was there is kept as it stood. Nothing is heard to change: what the macro knobs were
  adding stays in the sound, and the new module's knobs start at rest.
- **COPY** keeps the one in the chassis again under another name; the chassis holds the copy
  from then on. **NEW** is a module with nothing on its knobs. **RENAME**, on the strip,
  names the one whose pocket you pressed last. **DELETE**, held, takes the open one out,
  never the one in the chassis.
- **A module's stack knobs are changed in the library too**, whichever module it is: press
  its pocket, press the knob, and its pile is out in a row (see The rack). For a module that
  is not in the chassis nothing that is playing changes.
- The module in the chassis is always one of the kept ones: what you change while playing
  changes it. A session from before modules has one, MODULE 1.

### Stacks: chips under a name

A stack is a pocket in the rack with a pile in it: references to chips, in an order. It is
made and changed with no knob, on the instrument alone; with ONLINE lit the web library's
rack is kept the same (see ONLINE). The rack, under The library, has all of it: making one,
putting chips in, their order, taking them out. From the designer and the arrangement's
page, where the rack is a third of the page:

- **A chip carried onto a stack's pocket** goes into that stack (the empty pocket: a new
  stack with it in). So does a chip of a knob's own pile, with its keys.
- **A stack's pocket pressed** has its chips on the bench, in its order, to hear and to take
  from. **ON A KNOB** puts a copy of the stack on the first stack knob that holds nothing; in
  the designer that knob is then the chosen one.
- **A stack's pile carried** onto a stack knob's place (or onto the chosen knob's column)
  puts all of it there, after what the knob holds, each chip with its parts. Carried onto
  the arrangement, it is a part for each of its chips. Either way it is a copy: the stack
  stays in its pocket, and what is changed on the knob or in the arrangement does not
  change it.
- **KEEP ITS PILE AS A STACK**, on the chosen stack knob's card in the designer, keeps the
  knob's pile as a stack under the knob's name (kept again under that name: the same one,
  as it is now).
- An empty stack is the instrument's alone until it holds something.

### What is kept to be studied

The snapshot library is there to be studied: what sounds alike, what leads where, what you reach
for and when. The studying is done away from the instrument, by a program of its own on another
machine; the instrument's part is to keep and send everything that program will need, and to
do nothing else about it.

- **Each snapshot's sound without loss.** Beside its clip and its stems (Ogg Vorbis, which is
  what the instrument and the web library play), the same four bars are kept as 24-bit FLAC:
  the output, and each lane, return and the drum bus on its own. `clips/<id>.lossless`, about
  ten megabytes a snapshot. It is written last, after the clip and the stems, by the thread
  that writes those; the instrument never reads it. Snapshots taken before this build have none.
- **A log of what is done with chips.** A line as each thing is done: a snapshot taken (and the
  chip the set was on), a chip heard in the headphones, a chip recalled (which parts, with its
  notes or not, from which knob), a stack knob turned onto a chip or pressed between two, a
  chip put into or taken out of a knob, a stack or the arrangement, an arrangement played and
  each part it came to, RUN and STOP. `log/` in the library folder, a file a month, only ever
  added to. The first line of each run names the build. What ONLINE and WEB LOAD do is not your
  doing and is not in it.
- **Which build made each snapshot** was there already: every snapshot says what it was made
  with (`made`: the build, and a print of each lane, effect and the chassis as they were).

With ONLINE lit all of it goes to the web library (above), which is where the studying program
fetches it from and puts back what it finds. Nothing here waits on any of that. The program is
**SF-000 Analysis**, in `analysis/` (its own README): double-click `analysis/start.command` when
you are not playing, and open the page it writes to listen to what it found.
FORMAT.md has the files and the lines exactly.

### Knobs kept by themselves

A knob of the module can be kept by itself, to put on a place of any module. No network is
asked.

- **KNOBS** is a shelf of the rack. A stack knob kept is a pocket with its pile and a knob
  in its corner; a macro knob kept is a cap on its base, with a pip for each knob it is tied
  to. Press one and what it holds is on the bench: its chips with the parts each takes, or
  what it is tied to and how far it takes each. In the library either is changed there: the
  kept knob is what changes, not a knob it was put on.
- **KEEP KNOB n**, on the chosen knob's card in the designer, keeps the knob under its name,
  as it is: a macro knob with every knob it is tied to and how far it takes each, a stack
  knob with its chips and the parts each takes. Kept again under the same name, it is the
  same one as it is now. The rack turns to KNOBS so that its pocket is seen.
- **Carried from its pocket onto a place** in the designer, it is put there. The place must
  be of its kind (a macro knob on 1 to 4, a stack knob on 5 to 12, 14 or 15) and hold
  nothing: CLEAR it first (held, on the designer's strip). **ON A KNOB**, after the open one's name, puts it on the first
  such place. The knob starts at rest and nothing is heard to change.
- A kept knob is a copy, and so is the one put: what you change on the place changes the
  module's knob, not the kept one. A kept stack knob's own pile is changed in the library:
  press its pocket, and it is out in a row (see The rack). **DELETE**, held, takes the open one out of those kept;
  modules that have it keep theirs.

### The snapshot module: four macro knobs, ten stack knobs

The knobs are two kinds, by place. **1 to 4 are macro knobs**: each drives other knobs
(next section). **5 to 12, 14 and 15 are stack knobs**. **13 and 16 hold no knob**: on the E16 those
two corners are the lane wheel and OPEN on every page, this one included, and a stack knob
would need its press for itself. A stack knob: each holds snapshots in an order, one at each of
even steps along its turn (three: at the bottom, half way and the top; one alone: at the top).
**CUE**, on the module's panel, says how the stack knobs are played, and can be changed while
playing. Knob 4 pressed is CUE too, on the screen and on the E16.

**CUE out: a turn morphs the set itself.**

- From wherever the set is, it goes toward the next snapshot in the direction the knob is
  turning, by as much of the way as the knob has gone of its way to that snapshot's place. So
  the first click of a turn moves the sound a little: it never jumps, whatever was recalled or
  moved since the knob was last touched.
- At a snapshot's place the set is that snapshot. Turn on and it heads for the next one;
  turn back and it heads for the one behind. Stop anywhere between two for a place between
  them.
- It moves the sound knobs **and the effects in the racks** (below). A snapshot's
  lane's model and its samples come when the knob is **pressed on that snapshot's place**
  (double-click; its press on the E16), as a recall does.
- **Its notes come with N.** Each snapshot of a stack has an **N** key in the designer, after
  K, as a part of the arrangement has. Lit: when the knob **comes to that snapshot** (or goes
  through it on the way to the next), every lane the snapshot takes is given what that lane
  was playing in it, on the next bar line. Out, as it is on every stack made before this: the
  notes stay as they are and the sound alone follows the knob. So the notes are those of the
  last snapshot with N lit that the knob came to: leave it and they stay until you arrive at
  another. A press on its place brings them too (as it always has with the library's NOTES
  lit). **TO STACK** gives each snapshot its part's N, so an arrangement put on a knob brings
  its notes as it did when it played. N is the instrument's own: the web library is not told
  of it, and keeps none.
- **A snapshot put back is what sounded when it was taken**, whatever the macro knobs are
  adding: recalled, come to on a stack knob, or pressed to between two, each knob is where the
  snapshot's clip heard it. The macro knobs stay where they are; let down from there, one takes
  away what it was adding. (What a snapshot does not hold: MUTE and SOLO, TEMPO, the master's
  TAPE, COMP and LIMIT.)
- Standing on one of its snapshots, a stack knob shows that snapshot's name. Lit teal.
- **While it is turned it says where it is between two.** On the 3D stage the knob's tag has
  the snapshot it has left and the one it is going toward (the way it was last turned, with
  +N if that one brings its notes), how far it has come from the one to the other as a figure
  of 100, and a bar between two posts that fills as it nears. **Standing on one** (to within a
  click of the E16's knob) it says ON and the bar is full and green: the rig is that snapshot.
  On the flat screen the line under the knob says the same in short: ACID OPEN 60.
- **AT: the place to come back to.** Under the bar the tag has the knob's own place, **AT 51
  /127**: one figure for each click of the E16's knob, the same whether the knob came up to it
  or down (the figure of 100 above it is not: it counts from the snapshot left). Beside it is
  the whole turn as a line, a post where each snapshot stands and a pointer where the knob
  is. Write the figure down, or remember it; turned back to 51, the rig is where it was. The
  figure is where the hand has left the knob; the bar is the sound.
- **A stack knob turns finely.** Its snapshots are close together on the turn (five of them:
  32 clicks from one to the next), so a place between two is something to find. With the
  mouse it moves a click at a time, 800 pixels for the whole turn, and **Shift is the fast
  way**; the wheel is a click a notch. On the E16 the ten stack knobs turn **without
  acceleration**: a detent is a click however fast the hand (the macro knobs, and every other
  knob, go by the scene's acceleration as before). That is in the scene files: load
  `SF-000.oxie16` and `SF-000-FX.oxie16` onto the E16 again to have it.

**CUE lit: a turn chooses, a press goes.**

- A turn goes through the stack in the headphones, each snapshot in time with the set, and
  the set is not touched. The knob's name plate says which. Lit amber. Each is heard as its
  press would leave the rig now: the parts its keys take, its notes if its N is lit (see A cue
  is the rig itself).
- (Where the headphones are plugged in is PHONES, in the library screen.)

**A press takes the set to where the knob is**, over the GLIDE time, with CUE lit or out, and
the knob stays where it was left.

- On a snapshot's place (to within a click of the E16's knob): that snapshot, whole, for the
  parts of the rig it takes.
- Between two: every sound knob goes as far between where the two snapshots have it as the
  stack knob stands between their places. Half way between a closed filter and an open one is
  half open. The effects in the racks go with it (below). Notes and samples are a snapshot's
  own and are left as they are.
- With CUE lit the headphones play the nearer of the two; a mix of two is not something there
  is a clip of.
- Putting CUE out moves no knob: each stays where it was left, and its next turn goes on from
  there.

**Effects between two snapshots.** An effect is part of a sound, so a place between two
snapshots has their racks between them too, pressed or turned.

- The same effect in a slot of both: each of its knobs goes between the two.
- An effect in one, nothing in the other: the effect, its MIX as far up as the place is toward
  the snapshot that has it. Half way from dry to a reverb is half the reverb.
- Two different effects in the same slot: through dry. The first one's MIX comes down to
  nothing by half way, the other takes the slot there and comes up over the second half.
- A menu or a switch of an effect (a delay's TIME, FREEZE) and its CONFIG are the nearer
  snapshot's, changed once at half way.
- A lane a snapshot does not take keeps its rack.

**Each snapshot of a stack takes only the parts of the rig chosen for it**: any of the lanes,
the returns, the drum bus, the key. A part that was put out stays as it is when the knob comes
to that snapshot, is pressed onto it, or plays it in the headphones. They are chosen in the web
library, on the chips; in the designer such a snapshot's number is drawn on amber.

- A stack holds up to 128 snapshots (the places of a controller's knob).
- **MACROS** is where a stack knob is built, on the instrument alone. Along the top is **the
  module in the chassis**: its four macro knobs and its ten stack knobs in a row (13 and 16
  hold none), each stack knob a pocket with its pile in it and its knob in the corner, which
  turns where it stands. Press a place to choose it. Under the module is the chosen knob's
  own bench: a stack knob's pile as a **column** of cards, in the knob's order, and beside
  it the knob itself, large, with what it moves. The right-hand third is **the rack**.
  - Press a chip in the rack to hear it in the headphones (again to stop; **PHONES** is at
    the right end of the strip). It is then the one **in hand**: its card is ringed and shows a key for every part of the rig. Put out
    what should not go with it (heard at once).
  - **Carry it** into the column (press, move, let go between two cards: a yellow bar shows
    where), or onto any stack knob's place in the module (a yellow ring). The **ADD** key on
    its card, or **ADD** at the head of the column, puts it at the end with one press.
  - Each card of the column has **a key for every part of the rig**: the eleven lanes, **R**
    the returns, **B** the drum bus, **K** the key. Black: this chip takes that part. An
    empty frame: the part is put out, and stays as it is when the knob comes to this chip.
    Then **N**, its notes: black, the knob coming to this chip brings them (above).
    The last key is **M**, its models and samples. A turn of a stack knob carries the sound
    knobs and the effects in the racks from chip to chip; what it cannot carry is a lane's own
    instrument where the two chips differ in it: another **model**, another **sample**, the
    rest of its CONFIG, a menu or a switch. **M out** (as every chip starts): the lanes keep
    the instruments they have, all the way, and at the chip you hear its knobs on the sound
    you came with. **M black**: as the knob turns toward this chip, each lane it takes whose
    instrument is another here is **held between the two**: the one that was playing carries
    on, the chip's arrives beside it, both are played the same notes, and the knob mixes
    them, as far over as it has come. It is what a recall with GLIDE does, with the knob for
    the clock. At the chip the lane is the chip's alone and the other is let go; turned back
    before it gets there, the lane returns to the one it had. Two instruments run on such a
    lane only while the knob stands between the two. A press on the chip brings its models
    and samples whether M is lit or not, as a recall does. A snapshot taken while a lane is
    held between two holds the one that was arriving.
  - **Carry a card** to another place in the column to move it; onto another stack knob's
    place to put a copy there; onto a stack's pocket to put it in that stack too; off the
    column to take it out. Its number takes it out too.
  - **ADD, TAKE OUT, EARLIER, LATER**, at the head of the column, do the same with no
    carrying, as in a stack's row in the library: ADD is for the chip in hand, the other
    three for the card of the column last pressed.
  - Tap the card to hear it in the headphones with the parts it takes; tap again to stop. A
    key pressed while it plays is heard at once.
  - The wheel over the column moves it when it holds more than eight.
  - **A stack's pile carried from its pocket** onto a place puts that stack on the knob, a
    copy (see Stacks). **A kept knob carried from its pocket** onto a place puts it there
    (see Knobs kept by themselves).
  The web library builds them too (LOAD brings those), and with ONLINE lit a stack knob made
  here goes there by itself. So does the rack, and the library's comes here: see ONLINE.
- A stack knob has no DEPTH and is tied to nothing. MOTION swings it along its own stack
  (below).

**A session from before the two kinds** is brought over the first time this build opens it.
A knob that is where its kind now lives stays. A knob holding snapshots on 1 to 4 moves to the
first free stack knob, and a knob tied to knobs on a stack knob's place to the first free of 1 to 4,
in order; one that had both becomes two; MOTION follows its knob. **A knob that swept through
snapshots is a stack knob now**, which is not the same: it used to add how far its snapshots
were from its first on top of the set, by its DEPTH; now it takes the set to them. What the
sweeping knobs were adding when SF-000 was last closed is put into the set, so it opens
sounding as it was left. The four a session came with are the exception: a knob on 1 to 4 that
still holds INIT and one of the rig's own pushes (ENERGY, BUILD, ACID, DIST) becomes a macro
knob where it is, tied to what its push moves, and does what it did. The file as it was is
kept beside the session as `session-before-two-kinds.json`.

### A macro knob: LEARN

Knobs 1 to 4: one knob that moves the knobs you pick, on the lanes you pick, each as far as you
show it.

1. **MACROS**, pick one of knobs 1 to 4, press **LEARN**. The main screen comes up with
   `LEARNING` and the knob's number in the top bar, and that knob goes to its full turn.
2. **Set the rig as that knob's full turn should leave it**: turn any lane's sound knobs, sends
   and EQ, and the returns' knobs, on the screen or on the E16. Each one moved lights yellow, and
   the bar counts them. One turned and turned back is not counted.
3. **KEEP** (the yellow key in the top bar, or Return). Nothing changes in the sound: the knob is
   at its full turn. Turn it back and those knobs go back to where they were, each by its own
   share. **DROP** puts them back and ties nothing.

- It pushes **from wherever those knobs stand**: an offset on top of the set, so the four add
  up, and they stay on top of whatever a recall or a stack knob does.
- Up or down is whichever way you turned each knob: one knob can open the bass's filter while it
  shortens the hats.
- **LEARN again adds** to what the knob has. A tied knob turned back to where the knob's zero
  has it is let go. CLEAR unties everything.
- The macro designer lists what a knob is tied to and how far its full turn takes each, in
  hundredths of that knob's travel (`BASS: CUTOFF +50`). DEPTH scales the lot, to twice.
- **One tie is changed, or taken out, in the library**: MODULES, the module's pocket, the
  macro knob. Drag a tie's figure; press a tie and TAKE OUT (see The rack, above).
- **Pressed** (double-click; its knob's press on the E16), knobs 1 to 3 land on the nearer end
  of their turn: past half way what the knob has pushed stays and the knob goes home, ready to
  push again; short of it the knobs go back. **Knob 4's press is CUE**, so it has no such press.
- A macro knob made here goes to the web library by itself while ONLINE is lit (see ONLINE).
- While a knob is being taught it stands at its full turn and cannot be turned. A recall made
  meanwhile starts the measuring again from the recalled scene.
- It reaches what can morph: a lane's sound knobs, its sends and EQ, the returns'
  knobs, the drum bus's four. Not an insert's knobs, TAPE, COMP, LIMIT, tempo or the sequencer:
  moved while learning, those are ordinary changes and stay.

### MOTION: a wave on a snapshot knob

A snapshot knob can be turned for you. The macro designer has three **MOTION**s along its foot,
each four knobs:

- **WAVE**: the shape. SINE, TRIANGLE, SQUARE, RANDOM, DRIFT (a smooth wander that never repeats)
  and STEPS (eight steps, over and over) swing the knob both ways about where it stands. RAMP UP,
  RAMP DOWN and ENVELOPE (up at once, falling over the rest) push it one way and let it back.
  KICK pushes it on every hit of the kick lane. OFF: nothing.
- **SPEED**: how long one turn of the wave is: a sixteenth to 64 bars, with dotted, triplet and
  odd lengths (3/16, 5 BAR) that cross the bar. For KICK: how long the push takes to fall back.
- **KNOB**: which of the sixteen snapshot knobs it turns.
- **AMOUNT**: how much of that knob's turn, and which way. In the middle: none. +100 is the
  whole turn.

Everything the knob moves goes with it, as if a hand were turning it: through its snapshots in
their order, and every knob it is tied to by its own share. The knob stands where you leave it
and the wave moves it from there, as far as its travel goes (a SINE on a knob standing at nought
has only its upper half to go to; stand the knob half way for the whole swing).

- **In time with the clock.** Every wave is locked to the song position, so it falls on the bar
  the same way each time, and RANDOM, DRIFT and STEPS are the same every run. Stopped, the waves
  carry on from where they were.
- **It is worked out with the sound**, on the audio thread, every block: nothing waits on the
  screen, and a window being dragged does not make it stutter.
- **What the knobs show is what sounds.** The snapshot knob's ring swings, and so does the ring
  of every knob it moves. Where they *stand* is not touched: a snapshot taken while a wave runs
  holds where the knobs stand, a knob under the hand is set where the hand puts it and the wave
  carries on about that, and the E16's knobs are told where they stand.
- Two MOTIONs on one knob add. A MOTION with a wave and no AMOUNT, or on a knob that holds
  nothing, does nothing at all (the sound is to the sample what it was).
- **MOTION is not part of a snapshot.** Like the snapshot knobs themselves it is what you play
  over the scenes: a recall leaves it as it is. It is kept in the session file.
- A knob's card in the designer says which MOTIONs are on it, and each MOTION says what it is set
  to in words (`SINE  1 BAR  1 ENERGY  +40`).
- **Without the mouse.** On the E16, hold knob 15 on the snapshot knobs' page for half a second:
  the macro designer comes up and the E16 turns to MOTION's twelve knobs (a row for each: WAVE,
  SPD, KNOB, AMNT). OPEN is the way back. (A short press on knob 15 is still that stack knob's
  own.) They also have addresses of their own for a sequencer: channel 16, CC 102 to 113.
- **On a stack knob** the wave swings the knob along its own stack: the set follows the path
  through its snapshots, on top of where the set is. With CUE lit the swing is about where the
  knob stood when CUE was lit, so it keeps running while you choose. A stack of one has no
  path and does not swing.

### Snapshots that come with it

CLEAN SLATE (always first in the library) puts every knob where the rig starts it, with no steps
and no inserts. INIT is the rig as it first comes up, patterns and all. The other five are what the four
macro knobs of a new session push toward (ENERGY, BUILD, ACID, DIST) and, all five in a row,
the stack on knob 5 (TOUR).

### MIDI and the E16

Open SETUP to see what is connected. Nothing needs choosing.

- **Notes in.** Every MIDI input is listened to. A lane's number is its channel (1 KICK to 11 VOICE),
  so the OXI One plays the lanes as it played the Live tracks. Notes and the lane's own steps sound
  together; **M** in the sequencer strip mutes a lane's own steps, so only the One plays it.
- **LATENCY** in SETUP is how long SF-000 takes from a note arriving to its sound, to a
  tenth of a millisecond, and what it is made of: the wait for a buffer, the way out through the
  interface, and 4 ms inside (the limiter's look-ahead). About 10 ms at 128 samples, less at 64. It
  is the figure to delay outside gear by to hear it with SF-000. It is worked out, not measured
  (the interface's part is what the system reports). Only the buffer size changes it: no insert and
  no recall does.
- **The clock is sent, followed, or neither**: CLOCK in SETUP. The choice is kept.
- **SEND.** While it runs, START, 24 ticks a beat and STOP go to every output that is not the E16.
  CLOCK LEAD is how long the follower takes to answer: play a plain sixteenth pattern from it into a
  lane, read NOTES IN LAND x MS LATE in the panel, and put that number on CLOCK LEAD. With the clock
  on, RUN waits two blocks and LEAD (a few ms) before the first sound.
  Switched on while it is already running (or a sequencer plugged in then), the clock takes up
  where the rig is, with no START: the follower joins at the next RUN.
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
- **The library on the E16** (the second scene's first two pages; the library opened on the screen turns the E16 to them). Page 1
  is the **MAP**: the grid of knobs is the library laid out by sound, energy up the rows (bottom
  row calm, top row peak) and brightness along them (dark at the left, bright at the right). Each
  knob holds the snapshots in its square. **Turn** it to step through them (four clicks to the
  next), each heard in the headphones in time with the set; it stays on the one it has come to
  until it is turned again; **press** it and that one is recalled to the set, over the GLIDE
  time. Page 2 is **FIND**: RECENT (the newest first), FITS NOW (nearest in sound to what is
  playing), MORE and LESS (a step up or down in energy from what is playing, nearest step first),
  and knob 14 is GLIDE. A ring shows where its knob is among what it holds; an empty square's
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
| its pocket's HOME, WIDTH, HOLD, YIELD (POCKETS) | the lane's number | 59, 60, 61, 62 |
| the snapshot module's knobs (1 to 4 macro; 5 to 12, 14, 15 stack; 13 and 16 are no knob and are not listened to from a sequencer): a turn | 16 | 12 to 27 |
| GLIDE | 16 | 30 |
| a snapshot knob's press (64 and over). 1 to 3: the macro knob lands. 4: CUE. A stack knob: the set goes to where the knob is | 16 | 44 to 59 |
| MUTE as a key, each press on or off (the E16's knob 4): a lane's | the lane's number | 63 |
| the same for DUB's MUTE and SPACE's | 16 | 39, 40 |
| the same for the active lane's three inserts, and for its own sequencer | 13 | 2, 3, 4; 5 |
| the DUB return's four knobs, the SPACE return's four | 16 | 60 to 63, 64 to 67 |
| TAPE, COMP, LIMIT | 16 | 68, 69, 70 |
| the drum bus's DRIVE, CRUNCH, PUNCH, BOOM | 16 | 71, 72, 73, 74 |
| POCKETS' AMOUNT, CLEAR, SPEED, RETURNS | 16 | 75, 76, 77, 78 |
| MOTION 1, 2, 3 on the snapshot knobs: each WAVE, SPEED, KNOB, AMOUNT | 16 | 102 to 105, 106 to 109, 110 to 113 |
| **RECALL** from snapshot knob 1 to 16 | 16 | 80 to 95 |

  - **RECALL** is for arranging. A snapshot knob's snapshots (the MACRO designer fills it) are a
    numbered set: the CC's value says which one, 1 the first. It is recalled as from the library:
    sound knobs travel over the GLIDE time, patterns change on the bar. A sequencer says a value
    again on every step, so only a new value acts; 0 is none, and makes the next value new. A
    number past the last it holds does nothing.
  - **MOTION**'s WAVE, SPEED and KNOB are lists: the number's share of 0 to 127 says which (WAVE:
    0 OFF, 13 SINE, 25 TRIANGLE, 38 SQUARE, 51 RAMP UP, 64 RAMP DOWN, 76 RANDOM, 89 DRIFT, 102
    STEPS, 114 ENVELOPE, 127 KICK; KNOB: 0 the first, 127 the sixteenth). AMOUNT has none at 64.
  - **GLIDE** by value: 0 OFF, 12 a quarter bar, 23 half, 35 one bar, 46 two, 58 four, 69 eight,
    81 sixteen, 92 thirty-two, 104 sixty-four, 115 a hundred and twenty-eight, 127 two hundred and
    fifty-six. Sent in the same step as a RECALL, it is taken first.
  - A knob turned by CC travels over the GLIDE time like one turned by hand: for a sequencer's
    own curve to be heard as drawn, GLIDE is OFF. A bipolar knob's middle is 64. A menu's values
    are spread evenly (INFO lists them). Insert, return, master and mute addresses are taken up
    forty times a second; a lane's own knobs and a RECALL at once.
  - The active lane's, on channel 13 (the second E16 scene's pages; a sequencer can send them too):
    its three inserts' eight places (CC 8 to 31; pressed, 40 to 63: a key among them), the
    sequencer strip's LENGTH (70: the value and one is the steps) and HITS, ROTATE, ACCENTS, SEED
    (71 to 74), and CONFIG's controls while CONFIG is open, in the order its panel has them (76 to
    95, then 102 to 117: INFO shows each one's number).
  - Not on MIDI: tempo, key, a step's keys, an insert's or a return's C, M and S.
- **The E16.** Two scenes: load `controllers/e16/SF-000.oxie16` and `SF-000-FX.oxie16` (see
  `controllers/e16/README.md`, which has the three settings to make on the E16 and in SETUP).
  In the first, pages 1-11 are the lanes: the twelve knobs where the screen has them, then SEND A,
  SEND B, LOW and HIGH. Page 12 is the snapshot module: 1 to 4 the macro
  knobs (white), 5 to 12, 14 and 15 the stack knobs (violet), 13 the wheel and 16 OPEN (pink). The rings follow the rig, and a travelling knob's ring
  walks green. A page that comes up is told its values again, so a knob's first turn starts
  where the rig is.
- **The four corner knobs mean the same on every page, each behind its press:**

  | Knob | Pressed | |
  |---|---|---|
  | 1 | **GLIDE**: the knob is the GLIDE time (its ring green) until it is pressed again | every page but the library's two (FIND has a GLIDE knob of its own, knob 14) |
  | 4 | **the module's one switch**: MUTE on what makes sound, CUE on the snapshot knobs | a lane's page: that lane's MUTE. INS 1 to 3: that insert's MUTE. RETURNS: DUB's MUTE (and knob 8 SPACE's). SEQ: the lane's own sequencer muted. SNAP: CUE. MASTER and CONFIG: none |
  | 13 | **lane select**: the pointer's wheel (below) | every page |
  | 16 | **CONFIGURE**: OPEN (below); on CONFIG's pages SHUT | every page |

  On a lane's page knob 15 pressed is MID (its ring turns pink; press again for LOW). The
  snapshot page has the same four corners at a plain press: no knob of the module is on 13 or
  16. Its knob 15 held for half a second is MOTION. On the library's MAP, where every press
  recalls, 13 and 16 are held for half a second.
- **The snapshot page names its knobs as they are now**: SF-000 tells the E16's display each
  knob's name (four characters), says CUE in the page's title while it is lit, and for a few
  seconds after a stack knob is turned or pressed the title names the snapshot it is on
  (`7 ON DARK ROOM`, `7 CUE DARK ROOM`). No scene needs loading for a name to change.
- **Every knob on the screen has a page**, so nothing needs the mouse. The second scene (SF-FX)
  holds what the first has no room for: the active lane's three inserts (INS 1 to 3: the eight
  places where the screen has them; a key among them, THROW or FREEZE, is its knob's press), the
  RETURNS, the MASTER (TAPE, COMP, LIMIT; on its second row the drum bus's four, on its third
  POCKETS' four), the sequencer strip (SEQ: LENGTH, the step under the hand as six knobs, STEP
  to go through the pattern and that step's VEL, GATE, RATCH, NOTE and OCT, the generator's four,
  and the nudges: see `controllers/e16/README.md`), CONFIG (CFG 1 to
  3: its controls in the order its panel has them, twelve to a page (the panel is laid out as the
  E16 is: four across, each twelve the three top rows of a page, with SEL, PAGE -, PAGE + and SHUT
  under them, and it goes to the page the E16 is on; a CONFIG longer than three pages, as a
  lane's is with its EQ's bands last, goes on with PAGE +, to eight pages in all, the pages past
  the third standing on CFG 3's knobs); with POCKETS up, every
  lane's pocket), and the library's two pages.
  Its page 12 is the snapshot knobs again, as the first scene's page 12: they are on the same page
  whichever scene the E16 is in, and choosing them never loads a scene.
  SF-000 names an insert's and CONFIG's knobs on the E16's display as they change.
- **The E16 is on the page of whatever is in front.** Bring a module to the front on the screen
  (a click on its plate, Tab) or open CONFIG or the library, and the E16 turns to that page, in
  whichever scene it is. Turn the E16 to a page yourself and that module comes to the front on
  the screen (a lane's page makes it the active lane, as before); SF-000 never answers that by
  turning the E16 again. An E16 plugged in, or there when SF-000 starts, is turned to the page of
  what is in front. (The E16's own PC Mode must be Page and its PC Receive Channel 12. If it ends
  on another page than the one in front, SETUP's E16 line says what was asked and what it said.)
- **Knob 13 pressed is the pointer's wheel, on every page: press, turn, press.** The first press
  (on the library's map, where a press is the knob's own, hold it for half a second)
  takes the pointer up: a yellow frame round the module in front, and the 3D eye home so the
  whole rig is in view. Its turns move the frame, three clicks to the next: a lane, then those
  of its inserts that have an effect in them (the three insert panels show the lane the pointer
  is on), then the next lane and its inserts, and after the last lane the snapshot knobs, the
  returns, the master and the sequencer strip. The E16's display names what it is on (`> 3.1
  DELAY`). Nothing comes to the front and the E16 stays on its page while you turn, however long
  the pointer rests. **The second press chooses**: that module comes to the front, the E16 turns
  to its page, the eye flies in and stays, and knob 13 is the page's own again. Turn any other
  knob instead and the pointer is put down with nothing chosen; eight seconds untouched does the
  same. On a lane's page knob 13 is SEND A: the wheel never moves SEND A.
- **Knob 16 pressed is OPEN** (held, on those same two pages): CONFIG for what is in front, and
  on CONFIG's pages SHUT. On the master, the press that shuts its CONFIG opens POCKETS, and the
  next is the way back. On the snapshot knobs it opens the library; in the library it is the
  way back. On CONFIG's pages knobs 14 and 15 pressed are the page before and the next.

## Checks

```
./build.sh test                                   # build, then the offline audio and timing checks
SF000_TEST_NEW=1 build/.../SF-000 --test          # only one of the newest blocks, for when that is what is being worked on: 1 POCKETS and the convolution's BANDS, b BANDS alone, m MOTION on the snapshot knobs, a the arrangement, q the lane EQ, c the pocket as the kick's own curve, g GRANULAR's own knobs and a snapshot from before them, p a sample's slices, loop, starts and RESAMPLE, w stereo samples, LOOP and PERC's SAMPLE, n GAIN and a recall's notes at the end of its glide, x slice markers by hand, e the pitch wheel, f KICK FORGE (it prints the kick's loudness at eleven places on CHARACTER; SF000_FORGE_DEBUG=1 also writes each as /tmp/forge-cN.wav)
build/.../SF-000 --test --wav out.wav             # the same, keeping 16 bars of the full rig
./build.sh perf                                   # the engine's share of real time and what the screen costs, on this machine
build/.../SF-000 --audit                          # does every lane knob change the sound? (slow)
build/.../SF-000 --audit 6 --model SAMPLE --sample f.wav   # the same for one lane (1 to 11) playing a model, with a file loaded (--table: WAVETABLE from the file; --plain: after PLAIN; --level: only how loud it plays)
build/.../SF-000 --audit 6 --model ALL --sample f.wav      # every model that lane has, a line each (a model with sound knobs of its own is named by them)
build/.../SF-000 --printbus                       # what a print leaves: prints a small arrangement three ways (the drum bus at work, at rest, a lane leaving it part way) and says whether the files are one length, the stems add up to the mix, and where the bus's lanes are (--printbus DIR keeps the prints there)
python3 tools/master_probe.py probe.wav           # a test signal for measuring what a mix-bus processor does: sweeps and noise at stepped levels, tone steps, sides alone (bounce it through the processor)
build/.../SF-000 --finishcheck in.wav out.wav     # a file through FINISH alone, as 32-bit float (--amount 0 to 1, --input dB, --auto 1: finding its own level, --says: where it puts it; --limit -1 [--gain dB]: the master's limiter after it)
build/.../SF-000 --finishrig dir                  # the rig as it opens rendered with FINISH off and at 100, as two files to set side by side (--seconds N, --amount A, --target LUFS: the LIMIT knob, --bare: the limiter out of the way)
build/.../SF-000 --samplecheck f.wav --lane 6 --out dir   # what a lane makes of a sample: plays it and writes the lane and the outputs as WAVs, with every setting of the lane (--plain: after PLAIN; --note N; --set id=value)
build/.../SF-000 --printsession session.json dir   # a session file opened as the instrument opens it (a copy: the file is not written; its samples go in dir/session/samples) and its arrangement printed into dir (--parts A B: only those; --finish 0 to 4; --rate HZ)
build/.../SF-000 --printsession session.json dir --load   # no print: what each part asks of the processor on this machine, by lane (instrument + effects), the returns and the master (--time: bar by bar, through the morphs)
build/.../SF-000 --livesession session.json dir --from 7 --to 10   # the arrangement played as it is played live (the device's callback on its own thread, the timer bringing the parts; --single: both from one thread), written to dir/live.wav with how long each buffer took (live.csv); --buffer N
build/.../SF-000 --graincheck                     # GRANULAR above its sample's pitch: the sample's tones and nothing else (one of the engine test's checks, alone)
build/.../SF-000 --cuetest                        # a cue that is the rig: a second rig started as the instrument starts it, its cue against what the set then plays, in step, killed and started again (real time: it needs two cores to itself)
build/.../SF-000 --cueprobe session.json dir      # how long a cue's hand-over takes on this machine, on that session: the state written and read, and the second rig made it
build/.../SF-000 --uitest                         # clicks and drags every control on every screen
build/.../SF-000 --stagetest                      # the 3D stage, played through the eye: every control reached where it is drawn, the keys, the view, following, the tags, and falling back when it cannot start
build/.../SF-000 --miditest                       # notes in, the E16's knobs and rings, the clock out and the clock followed, with made-up messages
build/.../SF-000 --onlinetest http://127.0.0.1:8787   # ONLINE against a snapshot library running on this machine (its `npm run dev`)
build/.../SF-000 --shot setup.png --setup follow  # draw the SETUP panel: --setup send|follow|off, --beat
build/.../SF-000 --e16 controllers/e16/SF-000.oxie16   # write the E16's two scenes: that one (snapshot knob names from your session) and SF-000-FX.oxie16 beside it
python3 tests/e16_scene_test.py                   # the scene's shape and addresses, and its script on a mock E16 (pip install lupa)
build/.../SF-000 --shot main.png --scale 2        # draw a screen: --page library|macros|pockets (pockets: --amount 0 to 100), --lane N, --cfg lane|insert|return|space|master|bands (bands: a convolution with BANDS lit), --space conv, --seqmute, --rec, --dark (the dark instrument), --flat (the flat drawing), --info [--point x,y] (INFO, and the mouse resting there), --phones
python3 tools/icon.py assets/icon.png             # the app's icon again: the SF-000 tag (add "one" for the tag on a single line)
python3 tools/wear.py photo.png assets/wear.png   # the dark instrument's scratches again, from a photograph of scratched black metal (pip install numpy scipy pillow)
build/.../SF-000 --shot x.png --page macros --motion  # the macro designer with two MOTIONs at work
build/.../SF-000 --shot x.png --page macros --knob 7 --libtab 1 --open 1   # the designer with that knob chosen and its rack on that shelf with that pocket open (as the library's --libtab and --open)
build/.../SF-000 --shot x.png --page macros --knob 5 --hand 4  # the designer with that snapshot of the list in hand (its bass and returns put out)
build/.../SF-000 --shot x.png --page library --finder 1       # a page with a finder lit at the head of its snapshots (1 RECENT, 2 FITS NOW, 3 MORE, 4 LESS)
build/.../SF-000 --shot x.png --page library --libtab 0 --open 1   # the library's rack: --libtab the shelf (0 stacks, 1 knobs kept, 2 modules, 3 arrangements), --open the pocket that is open (from 1; none: the bin), --rowsel a chip of an open stack's row in hand, --hand a chip of the bin in hand, --modknob a knob of an open module pressed
build/.../SF-000 --shot x.png --page library --libtab 0 --open 1 --keys down,down,right,]   # keys of the keyboard pressed before the picture: left right up down return RETURN (with Shift) tab TAB back = [ ] { }
build/.../SF-000 --shot x.png --page arrange          # the arrangement's page, with one made of the snapshots it comes with, playing (--arrs: three kept, the rack on ARRANGEMENTS with the second open)
build/.../SF-000 --shot x.png --edit 3                # the main view with part 3 of an arrangement being edited (--page arrange --edit 3: the arrangement's page meanwhile)
build/.../SF-000 --shot x.png --learn 6             # a picture with snapshot knob 6 being taught and a few knobs moved (--learnt 6: after KEEP, turned half way back; with --page macros, its card)
build/.../SF-000 --shot x.png --front 12 --pointer 15   # a picture with that module in front and the wheel's pointer on that one (counted along the wheel's own list: a lane, its inserts that hold an effect, the next lane ..., the snapshot knobs, the returns, the master, the sequencer strip)
build/.../SF-000 --shot 3d.png --3d               # the 3D stage as drawn (it opens a window): --view top|player|hero, --turn and --tilt degrees, --size WxH, --look x,y,w,h (the eye holding that much of the screen), with the options above
SF000_SKIN_TIMES=1 build/.../SF-000 --shot x.png  # how long each of the lit skin's pictures took to make
```

On Linux the two 3D lines need a display and OpenGL 3.2; a software driver will do:
`xvfb-run -a -s "-screen 0 2000x1300x24" build/.../SF-000 --stagetest`. `SF000_SHEET=2` (or 3)
sets how fine the sheet is, `SF000_SAMPLES=0` turns edge smoothing off, `SF000_3D_FAIL=1` makes
the stage refuse to start, `SF000_3D_FAIL=blank` makes it start and draw nothing, and
`SF000_3D_FAIL=stall` makes it start and never draw (to see the falling back from each). The checks first tell the X server the names of a window manager's
properties: on a server with none (Xvfb), JUCE's asking after them raised errors that could
lock the screen's thread and the drawing thread against each other.

```
```

### Load

Activity Monitor's figure is a share of one core (100 % is one of the Mac's cores busy all the time).
It is the engine plus the screen. SETUP shows the engine's share on its own (ENGINE: n %),
and `./build.sh perf` measures both. The screen redraws only what moved: a travelling knob, the
meters when a bar has moved, the playing step.

**Whether the sound kept up.** A buffer of sound has to be made in the time it lasts. One that
takes longer leaves a gap: a click, and a run of them a crackle at the sound's full level,
which a print of the same set does not have (a print is not made in real time). The engine
counts them. **The FAULT lamp lights for three seconds after a late buffer** (and while a
module says its own sound has gone wrong), and SET UP's ENGINE line has a second row: the most
a buffer took of its own time lately, how many have run late since the audio started, and what
the interface itself counted as dropped. A crackle with the lamp dark and none late is in the
sound; print the set and look there. What a set asks, part by part and lane by lane, without
the Mac: `--printsession … --load` (see Checks). The dearest thing a lane can play is many
long notes at once on a model that reads a sample for each (GRANULAR with a GRANULAR layer and
a four-note CHORD is sixteen); a morph between snapshots whose lanes differ in their make-up
plays both for its length.

## What is here

| Path | What |
| --- | --- |
| `Source/Rig.h` | the rig: each lane's module, fixed model and settings, and which knob drives what |
| `Source/Engine.*` | clock, lanes, inserts, returns, lane EQ, POCKETS, the drum bus, master chain, delay compensation |
| `Source/Sequencer.h` | patterns, ties, ratchets, the generators, MIDI on the sample |
| `Source/Session.*` | base scene, snapshot library, snapshot knobs and their morph, the saved file |
| `Source/Arrange.cpp` | the arrangement (the session's own): its parts, playing it from the clock, printing it to files on a rig of its own |
| `Source/Defs.cpp` | every lane, effect and return written out as data, with a print of each: what a snapshot was made with |
| `FORMAT.md` | the session file: what is in it, and the rules it grows by |
| `tests/fixtures/` | a session file as an older build wrote it, for the check that old files still open |
| `Source/Pockets.*` | POCKETS: the bank of bands each lane is heard through, who gives way to whom, the bells that cut |
| `Source/Screen.*` | the four screens |
| `Source/Help.h` | what each control does, in a line or two: INFO's boxes |
| `Source/Skin.*` | the lit skin: surfaces as heights and finishes (the 3D model's), lit into pictures the screen is put together from; for the 3D stage, the same calls written down as parts |
| `Source/Parts.h` | the solid parts a screen is made of, as the skin records them |
| `Source/Stage.*` | the 3D stage: the parts and the sheet drawn with OpenGL, the eye, what the pointer is on |
| `Source/Midi.*` | notes in, the E16 (addresses, rings, the scene file), the clock's sender, the clock followed |
| `controllers/e16/` | the E16 scene and its script |
| `controllers/oxi-one/` | the OXI One MkII project, and a checker for your own |
| `Source/Profile.*` | what a snapshot sounded like, worked out from the bars kept with it |
| `Source/Recorder.*` | takes: the set, the lanes and the returns to disk, off the audio thread |
| `Source/Online.*` | ONLINE: snapshots sent to the library on the web in the background, and what has gone |
| `Source/CueRig.*`, `Source/CueShared.h` | a cue that is the rig: the second process, what the two say to each other, the memory they share |
| `Source/Main.cpp` | the app, the audio device, the command-line tools |
| `suite/` | SF Live Suite's sound code, copied (see `suite/PORTING.md`). `suite/modules/FxDrums.cpp`, the drum bus, and `suite/modules/Finish.cpp`, FINISH, are SF-000's own |
| `tools/master_probe.py` | the test signal FINISH was fitted with |

## Where everything is kept

Two places, and a third for takes.

**The library** is all the instrument needs to be itself, in one folder:

| IN IT | WHAT |
|---|---|
| `session.json` | the snapshots, the snapshot knobs, the arrangement, the rack (with what the web library and this one last agreed each stack was, and what is still to be said there), the rig as it was left, each snapshot's profile (its layout and the rules that keep old files readable are in `FORMAT.md`) |
| `clips/` | each snapshot's four bars (`<id>.ogg`), its lanes each on their own (`<id>.stems`), and the same without loss (`<id>.lossless`, about ten megabytes) |
| `log/` | what was done with chips, a line each, a file a month (see [What is kept to be studied](#what-is-kept-to-be-studied)) |
| `samples/` | SF-000's own copy of every audio file it has been given: a lane's sample or phrase, an insert's impulse responses, SPACE's. Each under its own name, in a folder named for what is in the file (`samples/3fa9c2e1/kick.wav`) |
| `backups/` | the session as each of the last twenty launches found it. To go back, quit and copy one over `session.json` |
| `library-id.txt`, `online-sent.json` | this library's own id; what the web library has said it holds |

It starts in `~/Library/Application Support/SF-000/`. **LIBRARY** in SETUP puts it anywhere else:
CHOOSE a folder, quit, open SF-000 again.

- If the folder already holds a library, that library is used.
- If not, the library in use is copied there (session, clips, samples, log) and used from there. The
  old folder is left as it was: delete it when you are sure.
- A folder that is not empty and is not a library gets a folder of its own inside it, `SF-000`,
  so nothing is spilled into the top of a disk.
- **On another computer:** install the app, open SETUP, CHOOSE the folder on the drive, quit and
  open again. Everything is there. (The interface, the MIDI settings, the look and ONLINE's
  address are that computer's own and are set there.)
- **If the chosen folder is not there at a start** (the drive is not plugged in), SF-000 says so,
  uses the library on the computer that time, and keeps the choice: plug the drive in and open
  it again.

**Samples.** A file you load or drop is copied into `samples/` and read from the copy. The file
you loaded is never touched, and can be moved, renamed or deleted afterwards without anything
changing. The same file given twice is kept once. A copy that nothing holds any more (no lane,
insert or return, and no snapshot) is removed: when you delete a snapshot, the samples only it
held go with it; and a few seconds after you give a lane another sample, the one it replaced
goes if no snapshot holds it. A copy is deleted outright if the file it came from is still where
it was, and put in the Trash if it is not (it may be the last there is). The first time this
build opens a session from before, it copies in every file the session and its snapshots name
that is still where it was.

**This computer's own settings** stay in `~/Library/Application Support/SF-000/` wherever the
library is: `audio.xml` (the interface), `midi.json` (the clock sent or followed, CLOCK LEAD, ON
THE BEAT, TRIM, the E16), `look.json` (3D, lit or flat, light or dark, the eye), `online.json`
(ONLINE's address and strings), `places.json` (the folders chosen in SETUP), `cue.json` (a cue is
the rig or the clips), `3d.log` and `3d-starting` (the 3D stage's start).

**Takes** go to `Music/SF-000/`, or to the folder set for TAKES in SETUP. It takes effect from the
next take; nothing is moved.

Delete `session.json` to start again from the factory rig.

## Not built yet

- A knob made here does not go up to the web library yet, and the library's knobs cannot be
  listed and installed from inside SF-000: they come by LOAD, from the chassis there.
- LOAD does not bring a snapshot's lanes (its stems) or its lossless sound down from the web
  library: a chip brought from there has its four bars and no lanes here. And a snapshot from
  before has no lanes kept, and none from before the seventy-seventh build has lossless sound
  (they could be made again from the snapshot itself, as PRINT renders, where its samples are
  on the Mac).
- SF-000 does not yet read what SF-000 Analysis finds: it is listened to on the analysis's own
  page.
- The skin is a first pass, drawn from measurements of the 3D model, not rendered from the model
  itself: no texture maps, one finish per material, and the inside of the displays (the meters,
  the step keys' bars, CONFIG's own display) is the flat drawing behind glass. The model's choices
  it does not follow yet are listed in `DECISIONS.md` (items 172 to 174).
- The 3D stage: its shapes are built from the model's measurements (the cap's outline is the
  model's), not loaded from the model file, and its materials are one finish each. No reflections
  of the room, no shade in the corners where a knob meets its plate beyond the one shadow. A
  snapshot's fly-out goes by how many knobs are moving, not by knowing a recall happened. Metal
  (OpenGL is the Mac's older way of drawing; it works and Apple no longer improves it).

- POCKETS: the stereo field is not part of it (a lane is heard as its two sides added, and cut the
  same on both); a return gives way to the lanes but no lane to a return, and neither return to
  the other; HOLD is one figure a lane, not one a band; AUTO places HOME and WIDTH and leaves
  HOLD and YIELD to the hand. On the screen its page takes the place of the main one: there is
  no AMOUNT knob on the master's face, only its figure beside the key.
- MIDI: no MIDI learn for other controllers, no notes out. Following a clock takes its ticks,
  START, STOP and CONTINUE; a song position sent on its own is not read.
- The E16's SEQ page edits one step at a time, the one STEP is on: there is no page of sixteen
  knobs that are sixteen steps' velocities, and the steps are not on the E16's own keys.
- The E16: 7-bit knobs (128 places); TEMPO, KEY and the M and S keys are not on it. LEARN's KEEP
  and DROP are on the screen and the keyboard only. A stack knob has those 128 places for all of
  its snapshots (the E16 could send 16,384, as CC14: not built, and not tried).
- The cue's rig: it is the first rig as it stood when the cue was asked for, and does not follow
  what is turned on the set afterwards; a place between two snapshots of a stack knob is not
  in it; notes from outside are in it as the steps a snapshot would keep of them (the last bars
  played), not as they are being played. On a session with heavy lanes it takes a moment to be
  ready (SET UP says how long).
- The rack's kept shelves (knobs, modules, arrangements) have no find row. On MACROS and beside the
  arrangement an open pile is for taking from, not for putting in order (your choice: DECISIONS 634).
  The rack is on the keyboard and the mouse, not on the E16.
- The arrangement: one on the page at a time, of those kept; no tempo of its own and no change of tempo along it (it is printed at
  the tempo that stands); no loop of a stretch of it; a part cannot be a part of a snapshot (some
  lanes and not others); what is turned by hand while it plays is not kept, so a print is the scenes,
  the snapshot knobs as they stand and MOTION, and no performance. It is made with the mouse: nothing
  of it is on the E16 but the snapshot knobs, which the E16 turns to while its page is up. A print
  is at the audio's own sample rate, with no dither and no choice of file type.
- Recording of MIDI and knob moves, and rendering a set again from them (audio is recorded: REC;
  a snapshot keeps the notes of its last bars, not a performance).
- Playing a snapshot's notes from outside exactly as they came (kept in it, unused so far): the
  sequencer plays them as steps. NOTES has no address on MIDI yet.
- The library: the sound map drawn on the screen, more finders (traits by name), suggestions timed
  to where the set is, a learned model for likeness. Meaning filters; snapshot knob pages beyond the first.
- MOTION on the snapshot knobs: three of them, for the whole rig; none of a wave's own shape can
  be drawn; no way to hold a wave at a place; it does not listen to a lane other than the kick.
  From the E16 the way in is a held knob, which nothing on the E16 itself says.
- A snapshot knob moves sound knobs only: it does not cross between effects or models (a recall does).
  A knob tied to knobs (LEARN) does not reach an insert's knobs. A tie is changed or taken out in the library; a new one is made only by LEARN.
  A model changed by hand in CONFIG changes at once. Nothing on the screen shows a crossing yet.
- Lanes as modules that load into a slot, each with a descriptor of what its knobs do to the sound.
  The session file is ready for it: `FORMAT.md`.
- LOAD: a stack narrows the library screen only (the macro designer and the arrangement still list
  every snapshot); stacks are not on the E16 (the plan: a knob a stack on its FIND page); there is no
  way to have LOAD write over a knob or an arrangement that holds something; and nothing is put
  away: taking snapshots out of the instrument once they are safe online waits on the web library
  keeping samples too.
- PA and cue outputs with room EQ. A display mode made for a dark stage: the dark instrument (D)
  is a start on it, with nothing dimmed or enlarged for distance.
- Menus, switches and inserts changing on a beat or bar line at a recall (they change at once; step patterns wait for the bar).
- Crash recovery beyond the saved session; the 8-hour soak test.
