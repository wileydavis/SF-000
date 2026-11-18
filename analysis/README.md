# SF-000 Analysis

The program that studies the snapshot library away from the instrument. SF-000 keeps and sends; this
fetches, measures, relates, and sends back what it found. It runs on its own, when the instrument is
not being played: on the laptop for now, on the Mac Studio later (DECISIONS 647 to 653).

```
SF-000 (playing)  ──sends──▶  web library  ◀──fetches / sends back──  SF-000 Analysis (this)
                    snapshots, clips, stems,       snapshots.drosaka.com        measures, relates,
                    lossless sound, the log                                     learns from the log
```

## Starting it

Double-click `analysis/start.command` in Finder (or `./analysis/start.sh` in a Terminal). The first
time it makes itself a Python of its own (a minute or two, and it needs Python 3 on the Mac: if it
says so, run `xcode-select --install` once). After that it starts at once.

It finds the web library the way SF-000 does: the address and the two strings of the Access token
that SET UP keeps on this Mac. There is nothing to enter.

Leave the window open and it keeps going: a pass every five minutes, straight on while there is
more to measure. **While SF-000 is running it stands aside** (no disk, no processors, no line) and
carries on when SF-000 is closed. Ctrl-C, or closing the window, stops it; started again, it takes up
where it left off.

```
./start.sh --once          one pass, then stop
./start.sh --keep-lossless keep each snapshot's lossless sound here too (about 10 MB each)
./start.sh --help          the rest
```

## Listening to what it found

It opens its page in the browser when it starts: **http://localhost:8765/report.html** (served by the
analysis itself, from this Mac only, while its window is open). Press any chip to hear it; again to
stop. Everything plays from what is kept here, so it needs no line to the library. **This page is how
to tell whether it is right.** Where it is wrong by ear, that is what to change next.

**SNAPSHOTS**: every snapshot, most energy first. Beside each:

| | |
|---|---|
| **ALIKE** | the snapshots that sound most like it, whatever their energy (the number: how far, 0 the same) |
| **UP** | sound like it, with more energy: a step up for the room (the number: how much more, of 100) |
| **DOWN** | the same, with less |
| **SAME TEXTURE** | not alike overall, but the same to the touch: grit, resonance, noise, how alive |
| **SAME RHYTHM** | the same rhythm as it is heard, in another sound |
| **RHYTHM** | what is heard in the bar; each lane's written pattern; what the sound adds to the steps; which lanes run against each other; what the dub return's echoes do |
| **FOLLOWED BY** | what you played next after it, and how often (from the log) |
| **TAKEN FROM** | the chip the set was on when it was taken |
| **SWAP** | for each of its lanes, the same lane from another snapshot that would fit the rest of it. Pressed: this snapshot with that lane taken from the other. The lane's name plays this snapshot's own lane alone |
| **ADD** | a lane it has not got, from another snapshot, that would go over all of it |

**PARTS**: every lane of every snapshot on its own, a lane at a time (BASS, CHORD, PAD, ...). A part
that is the same in several snapshots (same sound, pattern and effects) is listed once, "+N". Beside
each: the parts of the same lane that sound like it (ALIKE), and **GOES WITH**: for every other lane,
the three parts that would fit it best. Pressed, the two play together. TOGETHER: they are already in
a snapshot together (the rest never have been: that is where the finds are). CHOSEN: you put them
together yourself (a recall of some lanes only, from the log).

**MORPH**: the way between two snapshots (below).

### The morph between two snapshots

**Choosing the two is by ear.** FROM and TO are chips that play. CHOOSE beside one opens every snapshot as a
chip, most energy first: press one and it plays and is that end, so you press through them until it is the one
(pressed again, it stops; the box at the top finds by name). MORPH FROM and TO on any snapshot of the SNAPSHOTS
tab do the same from there. Then SCAN.

**SF-000 itself renders it**, run with no screen, as a stack knob holding the two plays it: the sound goes from
one to the other as the knob turns, with the notes of the one it set out from (NOTES OF chooses which), in that
one's key. Eleven points along the turn, each from the same beginning so that they can be compared; about a
minute. SF-000 has to be closed while it renders, and built in this folder (`./build.sh`).

**The sound files the two snapshots name** (a lane's sample, the voice lane's phrase, an impulse response) are
put where the render can read them: from SF-000's own library on this machine if they are there (each is kept in
a folder named for the first eight digits of its SHA-256, so it is known to be that file), and otherwise from the
web library. If one can be had from neither, the page says which, in yellow, above the scan. A lane that would
play a stand-in for its missing file is **left out of every point**, with no notes: the voice lane without its
phrase does not fall silent, it sings its notes with a choir of its own, which is in neither snapshot and the
same in every morph. Another lane that names a missing file is named too, since it may not sound as you know it.

**It is the stack knob, with the keys you would set on it.** Beside SCAN are the two keys each chip has on a
stack knob in SF-000 (its README, the column of a stack knob), and what is scanned is that knob with them set
so, turned from FROM to TO and held at each of eleven places:

- **N** out: the notes are FROM's all the way. Lit: they are FROM's until the knob is at TO, where TO's come,
  on the next bar line. (The key stays FROM's either way: a knob does not bring a key.)
- **M** out: every lane keeps FROM's model and sample all the way, so the far end is TO's knobs on FROM's
  sounds. Lit: each lane whose model or sample is another in TO is held between the two, both sounding, mixed
  by where the knob is, and the far end is TO's whole sound.
- **SWAP** turns the knob the other way: FROM and TO change places.

It runs at **the tempo FROM was taken at** (a snapshot holds no tempo, and the delays and the motion run by
it); the line above the points says which. So a point is what you would hear in SF-000 with those two chips
on a stack knob, the keys so, the knob at that place, after the sound has settled. What can differ, the page
says when it does: a lane left out for want of its sound file, and everything else on the rig at rest (the
macro knobs, the other stack knobs, MOTION), since the render starts from a fresh rig.

A point of a morph with M lit is a place of the knob and not a chip (two instruments on a lane; a chip holds
one), so nothing is made from it here: to play it, set the same keys on the two chips in SF-000 and turn to
that place. N, M and the tempo need SF-000 of the seventy-eighth build as it now stands, built in this folder
(`./build.sh`); with an older one the page says so.

**TUNE goes over with the rest of the sound**, as the stack knob takes it: a lane tuned differently in the two
snapshots bends from one tuning to the other across the turn, and a point part of the way holds the bend where
it stood. (For a morning these scans held TUNE still with the notes; a scan made then says so and asks to be
made again.)

What comes back is the turn of the knob, left to right. Press any point to hear it. The bars say how much
the sound changes in each tenth of the turn: an even morph would have them level, and few do, because even in
the controls is not even to the ear. The ringed points are **worth hearing**, each for a reason:

| | |
|---|---|
| **NEW** | neither end sounds like this: it is not simply between them |
| **ROUGHER, SMOOTHER, MORE RESONANT, MORE DIFFUSE, NOISIER, PURER, MORE ALIVE, STILLER, BRIGHTER, DARKER, WIDER** | more of that quality of texture than either end has |
| **FITS** | its parts sit together better here than at either end |
| **LIFTS, DROPS** | more energy than both ends, or less |
| **EDGE, HOLDS** | where the sound changes most; where it stands still |

Any point can be made a chip (MAKE CHIP): the rig exactly as it stood there, with its four bars.

**Reshaping it into a stack.** A stack knob plays through its chips in order, so a morph can be given
a shape by putting snapshots between the two:

- **EVEN**: one to five snapshots between, placed where the sound has changed by equal amounts, so that an even
  turn is an even change. It says what the largest step is beside the mean, before and after; if the morph was
  even already it says there is nothing to gain.
- **LEAD WITH**: the lanes you choose go first. The first half of the turn changes them alone; the second
  half, everything else. The lanes are offered most changed first.

RENDER has SF-000 play the snapshots between, so they can be heard and measured; MAKE STACK sends them to the web
library as a stack marked to load (the two ends and what is between), which WEB LOAD brings: put it on a
stack knob.

What is compared along a morph is the sound (its spread, its texture, its width, its lanes), not its
rhythm: the notes do not change along it. No two renders of the same thing measure quite alike (hats are
noise, oscillators drift), so the first point is rendered twice, and nothing is called worth hearing unless it
stands well clear of how far those two are apart.

### Building a combination, and making it a chip

Every SWAP, ADD and GOES WITH you press goes into **the combination**, in the bar at the foot of the
page, and the whole of it plays: a snapshot with lanes from others. Press more to build it up (one of
each lane). **A chip is lit while its lane is sounding in what plays, and only then**: stopped, nothing
is lit. Press a lit one and it comes out; press one that is in the combination while it is stopped and
the combination plays.

**One key stops and starts everything**: at the top of the page, beside the box that finds by name. While
anything plays it is lit, says STOP and names what is playing, whatever tab is showing and whether or not
the chip that started it is still on the page (found by name, it may not be). Pressed, the sound stops;
pressed again, the same starts again. The space bar does the same, unless you are typing. And what is lit
is what sounds: every chip that is the thing playing, in each place it is listed, and nothing else.

In the bar each lane does one thing when pressed: it is **silenced** (struck through; again: back). A
lane that came from another snapshot has a key beside it: **OWN** puts this snapshot's own lane back,
or **OUT** takes it away if the snapshot never had that lane. PLAY plays it, and lit, stops it; give
it a name; **MAKE CHIP** makes it a snapshot (a silenced lane is left out of it); CLEAR empties it.

The chip is the snapshot's whole rig with each lane you chose taken whole from where you chose it:
its controls, its pattern (or the notes that played it), its effects and their settings, its setup,
its sound file. Everything that is not a lane's (the returns, the drum bus, the chassis, the key) is
the base's. A lane taken away is silent. It goes to the web library and into a stack there called
**MADE**, marked to load: **WEB LOAD in SF-000 brings it**, into the library and the rack, like any
other chip. It has no sound of its own until it is played (take it again there and it has).

A lane keeps its meaning across builds. From SF-000's seventy-ninth build a snapshot says which
lists of models it was taken on (`lists`), because the lists grew and some models have knobs of
their own; a chip made of lanes from snapshots of both kinds says so lane by lane (`listsBy`), and
SF-000 reads each lane as it was taken. Nothing to do about it: it is why a new kick put into an
old snapshot is still the new kick.

Parts at different tempos are played at the tempo of the one they go with, **stretched, not sped
up**: the pitch stays where it is, as the instrument would play the notes. (The stretching is the
page's own and a little grainy on held notes; the instrument's own is exact.) Parts are played dry of
the other snapshot's returns: the dub and space of the snapshot it is put into are its own.

## What it does, a pass at a time

1. Asks the web library for its list of snapshots.
2. **Measures** each one not yet measured, from the best there is of it: its sound without loss
   (the output and each lane on its own) if SF-000 kept it; otherwise its clip and its stems. A
   snapshot measured from less is measured again when more arrives. As many as fit in ten
   minutes a pass; the rest next pass.
3. Takes the log: every line SF-000 sent since the last pass.
4. **Relates** them all, from scratch each pass (seconds, for thousands).
5. **Sends what it found** to the web library (`POST /api/analysis`), if anything changed. The web
   library keeps every result and never writes one over; the newest is what is used.
6. Writes `report.html`.

### What is measured

From the output: loudness (LUFS, as the master's meter reads it), how many hits a beat, how strongly
it moves with the beat, the eighth and the sixteenth, how its sound is spread from sub to air (ten
bands), its brightness, how noisy or tonal, how wide, and how much it changes over its bars. From the
lanes on their own: how loud each lane, return and the bus is beside the others, and how many lanes
are sounding.

### Texture

A lot of what moves in this music is not notes or loudness but what the sound is like to the touch. Four
things are measured for it, of the whole and of each lane:

- **rough**: how much its level flutters 20 to 150 times a second (grit, growl, beating);
- **resonant or diffuse**: how far its spectrum's peaks stand above its valleys;
- **noisy or pure**;
- **alive**: how far its brightness wanders from bar to bar, where a loop would stand still (a slow sweep, a
  drifting delay), apart from how it **moves** within the bar, the same every bar.

They count as much as the spread of the spectrum in what is ALIKE, they are the whole of SAME TEXTURE, and
they are what the MORPH scan looks for between two snapshots.

### Rhythm: written, and heard

**Written** is read from the snapshot itself, which says what every lane's sequencer played: how long each
pattern is, what kind it is (four on the floor, offbeats, backbeat, straight, Euclidean k/n such as the tresillo
3/8, three of every four, every third step, syncopated, sparse, broken), how syncopated (Longuet-Higgins and
Lee's measure), and **which lanes run against each other**: a pattern of 12 steps over patterns of 16 is three
against four, meeting every three bars.

**Heard** is taken from the sound, because the steps are half of it: a delay's echoes, a density knob, ghost
notes and a reverb's swell make rhythm that no step says. For the whole, for every lane and for the dub and
space returns on their own, the analysis finds where in the bar the sound rises (96 places a bar: fine enough to
tell a triplet from a sixteenth) and how loud each hit is. Set against what was written, that gives: how many
hits a bar are heard, **how many the sound added**, how many are **ghosts** (quiet), how many fall on triplets
or off the grid, and **what it repeats at** (a dub delay at a dotted eighth shows as echoes every 3 steps:
three against the kick's four, made by the delay alone).

SAME RHYTHM compares snapshots by the heard rhythm only.

### What is measured of each part

Every lane, return and the bus on its own (from the lossless sound, else the stems): its level, its
spread from sub to air, its brightness, **which notes it is made of** (all twelve, each as a share,
and how clearly: a window long enough to tell a low C from a C sharp), **where in the bar it sounds**
(each sixteenth, in six bands from sub to air: the clips start on a bar line, so a sixteenth here is
a step), how many hits a bar, and how much of the bar it fills.

### How parts fit

Two parts fit as well as they do not get in each other's way:

- **Notes**: what the two add of seconds, sevenths and tritones to each other (the tonal lanes: bass,
  rumble, lead, chord, pad, voice). A chord in the bass's key fits; one a semitone off does not.
- **Room**: how much they sit on each other, in the same sixteenths and the same part of the
  spectrum. A bass between the kicks fits the kick better than one on them.

For a SWAP, a candidate is weighed against the rest of the snapshot together; for ADD, against all
of it. A candidate that is as good as the same as the lane already there is not offered, and no two
offered are near the same. A pair you put together yourself counts for more.

### What is related

- **Energy** (0 to 100): loudness, hits a beat, top end, low end, the beat's pull and how many lanes
  sound, together; then each snapshot's place among all of them. It is the library's own scale: 100 is
  the most driving snapshot you have, 0 the least.
- **Alike**: over the spread, the rhythm, the space and (where both have them) the lanes, each counted
  equally whatever how many numbers it has, and loudness left out (alike is not "as loud").
- **Up / down**: of the thirty most alike, those with eight or more points more (or less) energy, the
  nearest first.
- **Followed by**: one snapshot sounding (recalled, turned to on a stack knob, reached by the
  arrangement), then another within ten minutes, in the same run with no STOP between.

## Where it keeps things

`~/Library/Application Support/SF-000 Analysis/`

| | |
|---|---|
| `python/` | its own Python |
| `snapshots/` | each snapshot as the library has it |
| `clips/` | each clip, for the page |
| `parts/` | each lane, return and the bus of each snapshot on its own (Ogg), for the page |
| `morph/` | each morph scanned: its points as Ogg, each as a snapshot, what was found |
| `files/` | the sound files your snapshots name, fetched from the web library for SF-000 to render with |
| `measured/` | what was measured of each |
| `log/` | the log, a file a library |
| `found/latest.json` | what it found last, as it was sent |
| `report.html` | the page |
| `state.json` | how far it has taken the log; the chips it has made, and its own library's name for them |
| `lossless/` | with `--keep-lossless` only |

It only ever reads from the web library and adds its findings to it. It changes nothing SF-000 sent.

## Not yet

- **SF-000 reading what it found** (the "smart" module, DECISIONS 652).
- **Rhythm in what fits**: two parts are not yet judged by how their heard rhythms lock (the measure is
  there: on each other, between each other, apart).
- **A finer scan**: more points where a morph changes fastest, and morphs between more than two.
- **Suggestions for combinations of three parts and more**: you build them by hand in the bar; it
  does not yet propose them.
- **Rendering the snapshots that have no sound** with the instrument run without a screen: for now
  those are listed at the foot of the page as not measured.
- **Families and paths** (groups of snapshots, and routes through them up and down in energy);
  **learning what you choose** beyond counting what followed what.
- Running as a service on the Mac Studio, without a window.

Its checks: `python3 -m unittest discover -s analysis/tests` (sounds made in the checks, whose answers
are known: hits a beat, brightness, width, loudness against the standard's own reference, families
told apart, energy the right way up, what followed what, the log taken once; grit, resonance, noise and a
slow sweep each heard as what it is; every kind of pattern named; a delay's echoes heard as added and at their
own period, ghosts as ghosts, triplets as triplets; a morph that changes all at first found uneven and evened;
nothing called worth hearing that is within the noise; a chord heard as its
three notes, a chord in the bass's key fitting and one a semitone off clashing, a bass between the
kicks fitting better than one on them, the same lane in two snapshots taken as one part, the chord in
C found for the bass in C).
