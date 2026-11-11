# The OXI One MkII project for SF-000

`SF000.oxipro` is the ONE's starting project for SF-000: every sequencer on its lane's channel and
four scenes of simple patterns. It is SF Live Suite's SF START with two things changed (49 bytes):
the name, and the two spare sequencers moved off channels SF-000 plays. Everything else already
fitted, because a lane's number in SF-000 is the number its Live track had.

Neither I nor a test has put it on a ONE. Load it into a spare project slot, with a backup of the
card first.

| FILE | WHAT |
|---|---|
| `SF000.oxipro` | The project. |
| `check_project.py` | Reads any ONE project and says what doesn't fit SF-000. Never writes. |
| `make_project.py` | Builds `SF000.oxipro` from SF START (`--base`). |
| `oxipro.py` | The project file, as far as it is read. Copied unchanged from the suite. |

## ONCE PER ONE: THE CONFIG

Global settings: the ONE keeps them, a project doesn't. **Shift + Back** opens the config; in it,
hold **Shift + Back** to save. There are two ways round, and SF-000's AUDIO AND MIDI panel has to
agree with the ONE (CLOCK: FOLLOW or SEND).

### THE ONE IS THE CLOCK (SF-000 ON FOLLOW)

Tempo, start and stop are on the ONE, and it can drive other gear alongside SF-000.

| SETTING | VALUE |
|---|---|
| Sync (ONE stopped: **Shift + Stop** until it shows) | **INT** in the header |
| MIDI › MIDI Transport Msgs Send | **On**: its START and STOP start and stop SF-000 |
| MIDI › MIDI USB Thru | **Off** |
| MIDI › Start Stop Msgs Ignore, Performance › Auto Sync | either: SF-000 sends no clock while it follows |

The ONE's clock ticks have to reach SF-000 over USB as well as its START and STOP. I do not know
whether the ONE has a setting of its own for that. If AUDIO AND MIDI still says NO CLOCK IS COMING IN
while the ONE plays, look in the ONE's MIDI config for one.

### SF-000 IS THE CLOCK (SF-000 ON SEND)

The same as for Live.

| SETTING | VALUE |
|---|---|
| Sync (ONE stopped: **Shift + Stop** until it shows) | **USB**, not INT, in the header |
| MIDI › Start Stop Msgs Ignore | **No** |
| MIDI › MIDI Transport Msgs Send | **Off** |
| MIDI › MIDI USB Thru | **Off** |
| Performance › Auto Sync | **On**: the ONE starts with SF-000 |

SF-000 sends its clock only while it runs, as Live did: RUN sends START and the ticks, stopping sends
STOP. With SF-000 stopped the ONE falls back to its own clock at the project's tempo (120).

## THE PROJECT

| SEQ | PLAYS | MODE | CH | OUT | PC |
|---|---|---|---|---|---|
| 1 | the drums | MULTITRACK | per track, bottom row up: 8 KICK 1A, 7 PERC 5A, 6 CLAP 4A, 5 OPEN HAT 3A, 4 CLOSED HAT 2A; 3-1 off | USB | off |
| 2 | 6 BASS | MONO | 6A | USB | off |
| 3 | 8 LEAD | MONO | 8A | USB | off |
| 4 | 9 CHORD | MONO | 9A | USB | off |
| 5 | 10 PAD | MONO | 10A | USB | off |
| 6 | 11 VOICE | MONO | 11A | USB | off |
| 7 | spare | MONO | 12A | USB | off |
| 8 | spare | MONO | 13A | USB | off |

- **A lane's number is its channel**, on the ONE, on the E16 and on the screen. 7 RUMBLE gets no
  notes: it follows the kick.
- **SF-000 listens to every port and goes by the channel alone.** Channel 7 on port B plays RUMBLE
  just as 7A would. That is why the spares moved from 7B and 16A to 12A and 13A, which SF-000 takes no
  notes on: use them for other gear.
- **The scenes** are SF START's, in columns 1-4 (on the Load page, **Shift + tap a column** loads it
  on every sequencer):

| SCENE (COLUMN) | DRUMS | BASS | CHORD |
|---|---|---|---|
| 1 SPARSE | KICK 1 5 9 13, OPEN HAT 3 7 11 15 | none | none |
| 2 MAIN | KICK on the beats, CLOSED HAT every eighth, OPEN HAT on the off-beats, CLAP 5 13 | steps 2-4, 6-8, 10-12, 14-16, velocity 20, 35, 55 in each group | step 7 |
| 3 VARIATION | MAIN, and PERC 12 15 | as MAIN | steps 7 and 15 |
| 4 EXIT | KICK on the beats, CLOSED HAT every eighth | none | step 7 |

- **A project of your own made from SF START works as it is**, unless it uses sequencer 7 on 7B.
  `python3 controllers/oxi-one/check_project.py MY.oxipro` says.

## WITH SF-000

1. Copy `SF000.oxipro` to the ONE the way projects come off it (the OXI App, or the card), into a
   spare project slot, and **Load** it. Open **Shift + Seq 7** and **Shift + Seq 8**: they must read
   12A and 13A. (The channel is the one byte the suite has not yet seen the ONE write itself. If one
   reads otherwise, set it there and save.)
2. In SF-000, mute the sequencer of each lane the ONE will play (**M** in the sequencer strip), or
   recall **CLEAN SLATE**. A lane's own steps and the ONE's notes sound together, so with both you
   get every hit twice.

**The ONE as the clock:**

3. AUDIO AND MIDI in SF-000: CLOCK on **FOLLOW**. The ONE's ports are under MIDI IN.
4. Press play on the ONE. SF-000 starts with it, the panel says FOLLOWING and the ONE's tempo, and
   TEMPO on the main screen reads CLOCK IN. Stop on the ONE stops SF-000. On the Load page,
   **Shift + tap column 2** for MAIN.
5. SF-000's own steps are heard with the lanes the ONE plays (WITH NOTES IN, the way it starts).
   With gear the ONE drives directly, see the next section.

**SF-000 as the clock:**

3. AUDIO AND MIDI in SF-000: CLOCK on **SEND**. The ONE's ports are under MIDI IN and CLOCK TO.
4. RUN in SF-000. The ONE starts with it. On the Load page, **Shift + tap column 2** for MAIN.
5. **Set CLOCK LEAD once.** With MAIN playing, AUDIO AND MIDI shows NOTES IN LAND x MS LATE. Put that
   number on CLOCK LEAD, stop and RUN again, and it should read near zero. The number belongs to the
   ONE and the buffer size; it is kept.

## THE ONE AS THE CLOCK: WHAT LINES UP WITH WHAT

Three things play: gear the ONE drives directly (CV, gates, MIDI hardware), SF-000 lanes the ONE
plays over USB, and SF-000's own sequencers. SF-000 is heard about 10 ms after a note reaches it:
LATENCY in AUDIO AND MIDI is the figure, to a tenth of a millisecond, for the buffer size in use. The ONE's gear has no such delay, and a note cannot be played before it arrives.

| IN SF-000 | ON THE ONE | ITS GEAR | LANES IT PLAYS | SF-000'S OWN STEPS |
|---|---|---|---|---|
| WITH NOTES IN (as it starts) | nothing changed | that much early | together | together |
| ON THE BEAT | nothing changed | on the beat | that much late | on the beat |
| WITH NOTES IN | the sequencers that play gear sent that much late | together | together | together |
| ON THE BEAT | the sequencers that play SF-000 sent that much early | on the beat | on the beat | on the beat |

- The last two rows need a time offset for each sequencer on the ONE. The first ONE was given one,
  in milliseconds, up to 100 either way, in a firmware beta, by a forum post. I have not found it
  in what I have of the MkII's manual, and do not know whether it moves the CV and gate outputs.
- With no gear on the ONE, the first row is all there is to it: everything is SF-000 and it is
  together.
- If the MkII has the offset, the third row is the cleaner of the two: nothing is played early, so
  even the first step after START lines up. With ON THE BEAT, bar 1's first step from SF-000 is
  that much late, once.
- "That much" is LATENCY. Only the buffer size changes it, so an offset set on the ONE holds until
  the buffer size is changed.
- NOTES IN LAND in the panel shows the gap between the ONE's notes and SF-000's own steps as you
  change either side.
