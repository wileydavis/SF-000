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
| Drag a knob up or down | turn it (hold Shift for fine; double-click for its starting value; the wheel works too) |
| Set GLIDE, then turn a knob | the sound travels there over the GLIDE time: the knob turns green and walks, with a white mark where it is going. It sets off on the next beat while the clock runs. Turn it again on the way and it carries on from where the sound is. OFF: at once |
| Click a lane's name, or its plate anywhere off the controls | make it the active lane: the inserts and the sequencer strip show it. Its display is the one lit up |
| Click an insert's plate, the returns', the master's, the snapshot knobs', the sequencer strip's, off the controls | bring that module to the front: its name lights up, and the E16 turns to its page (see MIDI and the E16) |
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
| **NOTES** (on the snapshot module, in the library; **N**) | whether a recall brings a snapshot's notes. Lit: each lane's sequencer is given what the lane was playing, and plays it. Out: a recall is the sound alone and every sequencer stays as it is (see What a snapshot holds of the playing, below) |
| Double-click a snapshot knob | recall its nearest snapshot as the new base scene |
| LIBRARY | the snapshot library: tap to recall, rename, hold DELETE. With **CUE** lit, a tap plays that snapshot's bars in the headphones instead, and **RECALL** takes it to the set |
| MACROS | the macro designer: pick a knob, tap snapshots to add them in sweep order, or press **LEARN** to tie it straight to knobs (see A knob tied to knobs, below) |
| ARRANGE (**A**) | the arrangement: snapshots in a line that morph one into the next, played from the clock and printed to files (see The arrangement, below) |
| Drag TEMPO or KEY | 60 to 200 bpm; the key every lane follows (parts are written in C: the pitched lanes move with it, and the kick, the percussion and the drum bus's boom tune to its nearest note). A lane's KEY FOLLOW, in its CONFIG, lets it be |
| Drop an audio file on a lane | load it, where the lane's model takes a sample (VOICE takes a phrase). Its CONFIG panel opens to show the waveform. SF-000 keeps its own copy (see Where everything is kept) |
| Drop an audio file on an open CONFIG panel | it goes to whatever that panel is for: the lane, the insert, or the return. An impulse response dropped on SPACE's CONFIG (or on the RETURNS panel) makes SPACE a convolution reverb of that place |
| Click the clip lamps above MIX | clear them |
| Arrow keys | up and down: the active lane. Left and right: the step page |
| **Tab** | the next module to the front: a lane, then those of its three inserts that have an effect in them, then the next lane and its inserts, and after the last lane the snapshot knobs, the returns, the master and the sequencer strip. Shift-Tab: the one before |
| **C** (the keyboard's) | CONFIG for what is in front, and shut again (the returns: DUB's, then SPACE's, then shut) |
| **N** | NOTES: whether a recall brings a snapshot's notes |
| **I** | INFO: every control MIDI reaches shows its CC number, a panel its channel, and a control the mouse rests on says what it does. **I** again puts it away |
| **H** | PHONES: headphones on the one pair of outputs. While a cue plays, the set is in the left ear and the cue in the right (see The library, below) |
| **3** | the 3D stage, or the lit skin it is drawn from (see The 3D stage, below) |
| **V** | 3D: the next view. From above, the player's, from the corner |
| **K** | 3D: unlock the view (drag off the controls to turn it, shift-drag or right-drag to slide it, scroll or pinch to come nearer), and lock it where it is put |
| **M** | 3D: whether the eye goes to what the controller moves |
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
one in the headphones to the set. Outputs 3 + 4 have to be ticked in SETUP once.

**PHONES** (beside CUE, or **H** on any screen) is for playing on headphones with only one pair of
outputs. While a cue plays, outputs 1 + 2 carry the set in the left ear (both its sides, summed)
and the cue in the right; when the cue stops, the set is back in both, over 10 ms. With no cue
playing nothing is different. The SETUP key at the top turns green and reads PHONES while it is
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

**A recall of part of a snapshot.** With CUE lit and a snapshot cued (or chosen), a row above the
keys says what RECALL takes: lanes 1 to 11, RET (the returns), BUS (the drum bus) and KEY. Click one to put it out:
that part then stays as it is, steps, inserts and CONFIG included, and RECALL takes the rest.
After the recall all are lit again. A tap with CUE off, a snapshot knob and a sequencer's RECALL
take everything, as always.

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

A switch that cannot travel and has nothing to cross (the key every lane follows, a lane's
sequencer muted or not) goes over on that same beat when the recall travels, all at once: it
used to go at once, ahead of the knobs. With GLIDE OFF it still does.

### The arrangement: snapshots in a line

**ARRANGE** on the main screen (under the snapshot knobs), or **A**. An arrangement is parts in a
line. Each part is a snapshot, how many **BARS** it lasts, and its **MORPH**: how long the way
into it takes, from its first bar. CUT: it is simply there. A morph as long as the part: the
whole of it is one travel from the snapshot before.

- **Making it.** Tap a snapshot in the library at the right: it becomes a part, after the one
  that is chosen (a part comes in as the one before it is: the same bars, the same morph). Drag
  a part's BARS (1 to 256) and its MORPH (the GLIDE list's own lengths; never longer than the
  part). A double-click puts either back (eight bars; cut). EARLIER and LATER move the chosen
  part, REMOVE takes it out, CLEAR (held) empties the line. Up to 64 parts.
- **The line** along the top is the whole of it to scale: each part as long as its bars, with
  the bar it begins on above it, and its morph as a slope. The part playing is green.
- **PLAY.** From stopped: the first part is recalled at once and the clock starts from the top.
  With the clock running: it comes in on the next bar line. FROM HERE plays from the chosen part.
  Each part after is recalled inside the beat before its bar line, so that everything in it sets
  off on the line: the knobs travel over the morph, patterns change on the bar, and what cannot
  travel is crossed over, exactly as in any recall. A cut lands on its line too (it is a travel
  of a few hundredths of a beat). At the end the clock stops. RUN stopped by hand puts the
  arrangement down; a recall made by hand while it plays is yours, and the next part still comes.
- **What plays over it.** The snapshot knobs, where they stand, and MOTION: an arrangement is
  the scenes, and those are what you play on top. So is anything you turn while it plays.
- **PRINT** renders it to files, for a release: faster than it plays, and the same every time.
  A folder `PRINT <date>` where takes go (SETUP's folder for them), holding `00 SET.wav`, the
  track (24-bit, after the limiter, bar 1 on its first sample, and what rings on after the last
  bar kept until it has died away), every lane, the two returns and the drum bus beside it as a
  take has them (32-bit float, in step with it, silent ones left out), and `PRINT.txt`: its
  length, its loudness (integrated LUFS and the highest sample), and the parts with the bar
  and the time each begins.
  - It is made on **a rig of its own**, built there and then from the session as it stands, with
    every module starting from its beginning. So the rig you are playing is not touched, and
    printing the same arrangement twice gives the same file to the byte. The sound stops while
    it is made (one rig sounds at a time) and comes back when it is done.
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

- A snapshot renamed here is renamed there. One deleted here stays there.
- One removed in the web library is not sent again.
- The snapshots the rig came with (INIT and the rest, CLEAN SLATE) stay at home.
- Each time it connects it asks the library what it holds, so the two never drift.

### Recording

REC writes a take to `Music/SF-000/`, or to the folder set for TAKES in SETUP (an external disk,
say), in a folder named for the moment it began. It starts at once, whether or not the clock is
running, and goes on until its key is held. Quitting ends and keeps it. If the folder set for
takes is not there when REC is pressed (its disk is not plugged in), the take is made in
`Music/SF-000/` and says so.

| FILE | WHAT |
|---|---|
| `00 SET.wav` | the set: the output, after the limiter. 24-bit |
| `01 KICK.wav` to `11 VOICE.wav` | each lane as it went into the mix: after its inserts, EQ, MUTE and SOLO. 32-bit float, so none can clip |
| `12 DUB.wav`, `13 SPACE.wav` | the two returns as they went into the mix. 32-bit float |
| `14 DRUM BUS.wav` | what the drum bus did: the lanes on it as the bus made them, less those lanes as they came. 32-bit float |
| `take.txt` | when, how long, what was kept, and anything lost |
| `session-at-start.json` | the rig as it stood when the take began |

- The lanes, the returns and `14 DRUM BUS` added together are what the master chain was given.
  Each begins with the master chain's look-ahead in silence (181 samples at 48 kHz), so it lines
  up with the set. A lane's file is the lane before the bus: leave `14 DRUM BUS` out and the
  drums are as they were without it. (It is only right beside the lanes at the level they were
  recorded: it is the difference the bus made to them together.)
- A lane or return that was silent all take is not kept, nor `14 DRUM BUS` if the bus was at rest
  throughout. `take.txt` names them.
- A take is about 19 GB an hour at 48 kHz. The bar says how many hours the disk has room for as it
  starts, and the take ends by itself, kept, with a minute of room left.
- The audio thread never waits for the disk. If the disk falls behind by more than four seconds,
  whole buffers are left out of every file alike (the files stay in step), the key turns amber, and
  `take.txt` says how much was lost.
- The files' lengths are written into them every ten seconds, so a take cut short by a crash or a
  pulled plug still opens, up to there. Past 4 GB a file is written as RF64.
- MIDI and knob moves are not recorded yet.

### A knob tied to knobs: LEARN

A snapshot knob can be made without snapshots: one knob that moves the knobs you pick, on the
lanes you pick, each as far as you show it.

1. **MACROS**, pick one of the sixteen knobs, press **LEARN**. The main screen comes up with
   `LEARNING` and the knob's number in the top bar, and that knob goes to its full turn.
2. **Set the rig as that knob's full turn should leave it**: turn any lane's sound knobs, sends
   and EQ, and the returns' knobs, on the screen or on the E16. Each one moved lights yellow, and
   the bar counts them. One turned and turned back is not counted.
3. **KEEP** (the yellow key in the top bar, or Return). Nothing changes in the sound: the knob is
   at its full turn. Turn it back and those knobs go back to where they were, each by its own
   share. **DROP** puts them back and ties nothing.

- It pushes **from wherever those knobs stand**, like every snapshot knob: an offset from the
  base scene, so it stacks with the others and works after any recall.
- Up or down is whichever way you turned each knob: one knob can open the bass's filter while it
  shortens the hats.
- **LEARN again adds** to what the knob has. A tied knob turned back to where the knob's zero
  has it is let go. CLEAR unties everything (and takes its snapshots off).
- The macro designer lists what a knob is tied to and how far its full turn takes each, in
  hundredths of that knob's travel (`BASS: CUTOFF +50`). DEPTH scales the lot, to twice.
- A knob can hold snapshots and be tied to knobs too: the two add up.
- **Pressed** (double-click; its knob's press on the E16), a knob that has no snapshot to land on
  lands on the nearer end of its own turn: past half way what it has pushed stays and the knob
  goes home, ready to push again; short of it the knobs go back.
- While a knob is being taught it stands at its full turn and cannot be turned. A recall made
  meanwhile starts the measuring again from the recalled scene.
- It reaches what a snapshot knob reaches: a lane's sound knobs, its sends and EQ, the returns'
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
  SPD, KNOB, AMNT). OPEN is the way back. (A short press on knob 15 still recalls its nearest
  snapshot.) They also have addresses of their own for a sequencer: channel 16, CC 102 to 113.

### Snapshots that come with it

CLEAN SLATE (always first in the library) puts every knob where the rig starts it, with no steps
and no inserts. INIT is the rig as it first comes up, patterns and all. The other five are the
pushes behind the ENERGY, BUILD, ACID, DIST and WARP knobs.

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
| its pocket's HOME, WIDTH, HOLD, YIELD (POCKETS) | the lane's number | 59, 60, 61, 62 |
| the sixteen snapshot knobs: a turn | 16 | 12 to 27 |
| GLIDE | 16 | 30 |
| a snapshot knob's press: recall its nearest (64 and over) | 16 | 44 to 59 |
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
  SEND B, LOW and HIGH. Press knob 16 and it is MID (its ring turns pink); press again for HIGH.
  Press knob 1 and it is GLIDE (its ring turns green), the same GLIDE on every lane's page; press
  again for the lane's first knob. Page 12 is the sixteen snapshot knobs (knob 1 pressed is GLIDE
  there too, and knob 13 pressed the wheel); a press on any of the others recalls that
  knob's nearest snapshot. (Knob 15 held for half a second is MOTION: see MOTION, above.) The rings follow the rig, and a travelling knob's ring walks green. A
  page that comes up is told its values again, so a knob's first turn starts where the rig is.
- **Every knob on the screen has a page**, so nothing needs the mouse. The second scene (SF-FX)
  holds what the first has no room for: the active lane's three inserts (INS 1 to 3: the eight
  places where the screen has them; a key among them, THROW or FREEZE, is its knob's press), the
  RETURNS, the MASTER (TAPE, COMP, LIMIT; on its second row the drum bus's four, on its third
  POCKETS' four), the sequencer strip (SEQ: LENGTH and the generator's four), CONFIG (CFG 1 to
  3: its controls in the order its panel has them, twelve to a page; with POCKETS up, every
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
  (on the library's map, where a press recalls, hold it for half a second)
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
- **Knob 14 pressed is OPEN** (held, on those same two pages): CONFIG for what is in front, and
  on CONFIG's pages SHUT. On the master, the press that shuts its CONFIG opens POCKETS, and the
  next is the way back. On the snapshot knobs it opens the library; in the library it is the
  way back. On CONFIG's pages knobs 15 and 16 pressed are the page before and the next.

## Checks

```
./build.sh test                                   # build, then the offline audio and timing checks
SF000_TEST_NEW=1 build/.../SF-000 --test          # only one of the newest blocks, for when that is what is being worked on: 1 POCKETS and the convolution's BANDS, b BANDS alone, m MOTION on the snapshot knobs, a the arrangement
build/.../SF-000 --test --wav out.wav             # the same, keeping 16 bars of the full rig
./build.sh perf                                   # the engine's share of real time and what the screen costs, on this machine
build/.../SF-000 --audit                          # does every lane knob change the sound? (slow)
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
build/.../SF-000 --shot x.png --page arrange          # the arrangement's page, with one made of the snapshots it comes with, playing
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
| `Source/Main.cpp` | the app, the audio device, the command-line tools |
| `suite/` | SF Live Suite's sound code, copied (see `suite/PORTING.md`). `suite/modules/FxDrums.cpp`, the drum bus, is SF-000's own |

## Where everything is kept

Two places, and a third for takes.

**The library** is all the instrument needs to be itself, in one folder:

| IN IT | WHAT |
|---|---|
| `session.json` | the snapshots, the snapshot knobs, the rig as it was left, each snapshot's profile (its layout and the rules that keep old files readable are in `FORMAT.md`) |
| `clips/` | each snapshot's four bars |
| `samples/` | SF-000's own copy of every audio file it has been given: a lane's sample or phrase, an insert's impulse responses, SPACE's. Each under its own name, in a folder named for what is in the file (`samples/3fa9c2e1/kick.wav`) |
| `backups/` | the session as each of the last twenty launches found it. To go back, quit and copy one over `session.json` |
| `library-id.txt`, `online-sent.json` | this library's own id; what the web library has said it holds |

It starts in `~/Library/Application Support/SF-000/`. **LIBRARY** in SETUP puts it anywhere else:
CHOOSE a folder, quit, open SF-000 again.

- If the folder already holds a library, that library is used.
- If not, the library in use is copied there (session, clips, samples) and used from there. The
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
(ONLINE's address and strings), `places.json` (the folders chosen in SETUP), `3d.log` and
`3d-starting` (the 3D stage's start).

**Takes** go to `Music/SF-000/`, or to the folder set for TAKES in SETUP. It takes effect from the
next take; nothing is moved.

Delete `session.json` to start again from the factory rig.

## Not built yet

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
- The E16: 7-bit knobs (128 places); TEMPO, KEY and the M and S keys are not on it. LEARN's KEEP
  and DROP are on the screen and the keyboard only.
- The arrangement: one of them; no tempo of its own and no change of tempo along it (it is printed at
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
  A knob tied to knobs (LEARN) does not reach an insert's knobs, and its ties cannot be edited one by one.
  A model changed by hand in CONFIG changes at once. Nothing on the screen shows a crossing yet.
- Lanes as modules that load into a slot, each with a descriptor of what its knobs do to the sound.
  The session file is ready for it: `FORMAT.md`.
- ONLINE the other way: loading a library file, buckets and snapshot knobs made in the web library.
- PA and cue outputs with room EQ. A display mode made for a dark stage: the dark instrument (D)
  is a start on it, with nothing dimmed or enlarged for distance.
- Menus, switches and inserts changing on a beat or bar line at a recall (they change at once; step patterns wait for the bar).
- Crash recovery beyond the saved session; the 8-hour soak test.
