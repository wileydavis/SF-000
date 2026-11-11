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

The same as for Live. Global settings: the ONE keeps them, a project doesn't. **Shift + Back** opens
the config; in it, hold **Shift + Back** to save.

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
2. In SF-000, recall **CLEAN SLATE**, or clear the steps of the lanes the ONE will play. A lane's own
   steps and the ONE's notes sound together, so with both you get every hit twice.
3. AUDIO AND MIDI in SF-000: the ONE's ports are under MIDI IN and CLOCK TO, SEND CLOCK is on.
4. RUN in SF-000. The ONE starts with it. On the Load page, **Shift + tap column 2** for MAIN.
5. **Set CLOCK LEAD once.** With MAIN playing, AUDIO AND MIDI shows NOTES IN LAND x MS LATE. Put that
   number on CLOCK LEAD, stop and RUN again, and it should read near zero. The number belongs to the
   ONE and the buffer size; it is kept.

SF-000 does not follow the ONE's clock: start and stop from SF-000.
