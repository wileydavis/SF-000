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

### Building a combination, and making it a chip

Every SWAP, ADD and GOES WITH you press goes into **the combination**, in the bar at the foot of the
page, and the whole of it plays: a snapshot with lanes from others. Press more to build it up (one of
each lane; pressed again, it comes out). In the bar, a lane pressed is **taken away** (again: back).
PLAY plays it; give it a name; **MAKE CHIP** makes it a snapshot.

The chip is the snapshot's whole rig with each lane you chose taken whole from where you chose it:
its controls, its pattern (or the notes that played it), its effects and their settings, its setup,
its sound file. Everything that is not a lane's (the returns, the drum bus, the chassis, the key) is
the base's. A lane taken away is silent. It goes to the web library and into a stack there called
**MADE**, marked to load: **WEB LOAD in SF-000 brings it**, into the library and the rack, like any
other chip. It has no sound of its own until it is played (take it again there and it has).

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
| `measured/` | what was measured of each |
| `log/` | the log, a file a library |
| `found/latest.json` | what it found last, as it was sent |
| `report.html` | the page |
| `state.json` | how far it has taken the log; the chips it has made, and its own library's name for them |
| `lossless/` | with `--keep-lossless` only |

It only ever reads from the web library and adds its findings to it. It changes nothing SF-000 sent.

## Not yet

- **SF-000 reading what it found** (the "smart" module, DECISIONS 652).
- **Suggestions for combinations of three parts and more**: you build them by hand in the bar; it
  does not yet propose them.
- **Rendering the snapshots that have no sound** with the instrument run without a screen: for now
  those are listed at the foot of the page as not measured.
- **Families and paths** (groups of snapshots, and routes through them up and down in energy);
  **learning what you choose** beyond counting what followed what.
- Running as a service on the Mac Studio, without a window.

Its checks: `python3 -m unittest discover -s analysis/tests` (sounds made in the checks, whose answers
are known: hits a beat, brightness, width, loudness against the standard's own reference, families
told apart, energy the right way up, what followed what, the log taken once; a chord heard as its
three notes, a chord in the bass's key fitting and one a semitone off clashing, a bass between the
kicks fitting better than one on them, the same lane in two snapshots taken as one part, the chord in
C found for the bass in C).
