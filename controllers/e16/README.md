# The E16 scenes for SF-000

Two scenes for the OXI E16. Between them every knob the screen has is on a page, and the E16 is
always on the page of whatever is in front on the screen.

- `SF-000.oxie16` (SF-000): a page for each lane, and a page for the snapshot knobs.
- `SF-000-FX.oxie16` (SF-FX): the library's two pages, then the active lane's three inserts, the
  returns, the master, the sequencer strip and CONFIG.

SF-000 writes both itself from the rig (`SF-000 --e16 SF-000.oxie16`), in the form the suite's PERF
scene has, so the screen and the E16 cannot drift apart. `rings.lua` is the script inside both.

**I have no E16: Wiley's is the only one this has met.** The scenes are checked against the
suite's PERF scene object by object, the script against a mock of the E16
(`tests/e16_scene_test.py`), and SF-000's side with made-up messages (`SF-000 --miditest`). What
his has shown so far, and what only the hardware can say, is at the foot of this page.

## Load them, and three settings

1. OXI App > Scenes. Copy both files into the Scenes folder on the computer and click Refresh
   Views. Drag SF-000 and SF-FX from On Computer onto two slots under On Device. (SF-FX takes the
   place of SF-LIB, the library's scene of before: drag it onto that slot.)
2. **On the E16**, system config (knob 8 on the home screen) > MIDI:
   - **PC Receive Channel: 12.** This is how SF-000 turns the E16's page.
   - **PC Mode: Page. This one matters.** From one page to another of the same scene SF-000
     sends a Program Change by itself, and the E16 takes that as a page only with PC Mode on
     Page (on Scene it would load another scene).
   - As before: **Auto Transmit off** (scene and global) and **MIDI Thru off**.
3. **In SF-000**, SETUP > E16 PAGES: the channel (12), then the numbers of the two slots the
   scenes are in on the E16 (SF-000's, then SF-FX's: 1 and 2 until you say otherwise). OFF in
   place of the channel and SF-000 never turns the E16's page.
4. Open SF-000. SETUP should read `E16: OXI E16 ...`. Every ring jumps to the rig's values.

If the E16 goes to the wrong scene or page when something is brought to the front, SETUP's E16
line says which page SF-000 asked for and which the E16 said it went to, or that it did not
answer (`ASKED FOR SF-000 PAGE 5, IT WENT TO SF-000 PAGE 1`). That line is the thing to send me.

SF-000 listens to the E16's first USB port only (the E16 sends everything on each of its ports)
and sends to the same port.

## What is in front

- **Bring a module to the front on the screen** (a click on its plate off the controls, Tab,
  the pointer's wheel below) and the E16 turns to its page, in whichever scene that is. CONFIG
  opened turns it to CONFIG's first page; the library opened, to the map; MACROS, to the snapshot
  knobs.
- **Turn the E16 to a page yourself** and that module comes to the front on the screen. A lane's
  page makes it the active lane (its inserts, its sequencer strip). SF-000 does not answer that by
  turning the E16 again.
- **An E16 plugged in, or there when SF-000 starts, is turned to the page of what is in front**,
  so the two agree from the first knob. If the script is not running and the page is turned by
  hand, the first knob turned there tells SF-000 which page it is.
- **The 3D stage's eye goes to what is chosen and stays there**: a page turned to, the wheel's
  second press. It moves on when something else is chosen, takes CONFIG in while CONFIG is open,
  and goes home for Esc on the keyboard (M turns following off altogether).
- **A page that comes up is told its values**, so a knob's first turn starts where the rig is.

## Knobs 13 and 14, on every page

- **Knob 13 is the pointer's wheel: press, turn, press.**
  - **Press**: the pointer is up. On the screen a yellow frame is round the module in front and
    the 3D eye goes home, so the whole rig is in view; the E16's display says `> ` and the
    module's name, and knob 13 reads SEL.
  - **Turn**: the frame moves, three clicks to the next (two when there are more than thirty-six
    to go through). The order is a lane, then those of its three inserts that have an effect in
    them (`3.1 DELAY`: lane 3's first insert), then the next lane and its inserts, and after
    VOICE the snapshot knobs, the returns, the master and the sequencer strip. Nothing comes to
    the front and the E16 stays on the page it is on, however long the pointer rests.
  - **Press again**: what the pointer is on is chosen. It comes to the front, the E16 turns to
    its page, the eye flies in to it and stays, and knob 13 is the page's own again.
  - **Not choosing**: turn any other knob (it does what it always does) and the pointer is put
    down where it was taken up. Eight seconds untouched does the same.
  - On a lane's page knob 13 is SEND A: the wheel never moves SEND A, and its ring is put back
    when the pointer is put down.
  - Its ring, while it is the wheel, moves with the pointer. When the pointer is taken up SF-000
    sets the knob to a number with room either side to reach both ends of the list (an E16 knob
    stops at 0 and at 127), and after that leaves it alone unless it has got more than six
    clicks from where it should be. Your clicks are counted from wherever the knob says it is:
    a number the E16 did not take cannot throw the pointer.
  - On the second scene's pages knob 13 is nothing else, and a turn takes the pointer up without
    the first press.
- **Knob 14 pressed is OPEN.** CONFIG for what is in front, and the E16 goes to CONFIG's page;
  pressed there (it reads SHUT) CONFIG shuts and the E16 is back on the module's page. The returns
  have two: DUB's, SPACE's, then shut. On the snapshot knobs OPEN is the library; in the library
  it is the way back. The master and the sequencer strip have nothing behind them.
- **On the snapshot page and the library's map every press already recalls**, so there knobs 13
  and 14 are **held for half a second** for the wheel and OPEN. A press let go sooner recalls as
  before (for those two knobs on those two pages the recall comes as the knob is let go, not as
  it goes down).

## The first scene: SF-000

```
pages 1-11: a lane each (KCK CHH OHH CLP PRC BAS RMB LED CRD PAD VOX), on the lane's own channel
 1  2  3  4      the lane's twelve knobs, where the screen has them:
 5  6  7  8      rows 1 and 2 its sound (white), 9-11 its shape (red), 12 LEVEL (light blue)
 9 10 11 12
13 SEND A [press: the wheel]   14 SEND B [press: OPEN]   15 LOW   16 HIGH [press: MID]

page 12: SNAP, the sixteen snapshot knobs (violet), on channel 16. Press: recall.
         13 and 14 held: the wheel, OPEN (the library).
```

- **Knob 1, pressed, is GLIDE** on every lane's page: its ring turns green and it sets how long
  knobs and recalls travel (OFF at the bottom, 256 bars at the top). A press swaps it back to the
  lane's first knob. It is one GLIDE whichever page it is turned on. (On the snapshot page a
  press recalls, so GLIDE is not there; the second scene has it as knob 16 of FIND.)
- **Knob 16** is HIGH. A press swaps it to MID and its ring turns pink; a press swaps it back. The
  E16 does this itself (Swap Destination) and keeps a value for each.
- **A snapshot knob's press** recalls the snapshot the knob is nearest to as the new base, and the
  knob goes back to zero, as a double-click on the screen does.
- **A snapshot knob tied to knobs** (LEARN, in the macro designer) is turned and pressed here like
  the others. With no snapshot to land on, its press keeps what it has pushed if the knob is past
  half way, and sends the knob home either way. While one is being taught, the lane knobs turned
  on the E16 are what it learns; KEEP and DROP are on the screen (Return is KEEP).
- **Rings.** A value changed on the screen, by a recall or by a snapshot knob is sent to its knob.
  While a knob travels (GLIDE), its ring is green and walks with the sound; turn it and the ring
  shows your hand for a moment. Knobs 1 and 16 keep their own rings (the script cannot tell which
  of a swapping knob's two is up).

## The second scene: SF-FX

```
page 1   MAP      the library by sound (below)
page 2   FIND     1 RECENT, 2 FITS NOW, 3 MORE, 4 LESS; 16 GLIDE
page 3   INS 1    the active lane's first insert: its eight places where the screen has them
page 4   INS 2      1 2 3 4     (DELAY: TIME FEEDBACK TONE DRIVE
page 5   INS 3      5 6 7 8              THROW FREEZE MIX WIDTH)
page 6   RETURNS  1-4 DUB TIME, FDBK, TONE, DRIVE; 5-8 SPACE SIZE, DECAY, DARK, DUCK
page 7   MASTER   1 TAPE, 2 COMP, 3 LIMIT
page 8   SEQ      1 LENGTH, 2 HITS, 3 ROTATE, 4 ACCENTS, 5 SEED (the active lane's)
page 9   CFG 1    CONFIG's controls in the order its panel has them, twelve to a page
page 10  CFG 2      15 [press]: the page before   16 [press]: the next
page 11  CFG 3
page 12  empty
on pages 2 to 11: 13 the pointer's wheel (pressed or not), 14 [press] OPEN
```

- **An insert's page is the active lane's**, whatever effect is in the slot. SF-000 sends the
  script the names of its eight places and a title (`BAS 1 DELAY`), and the E16's display shows
  them. A place that is a key on the screen is its knob's **press**: THROW is held while the
  knob is held down, FREEZE switches. (Turned past the middle, a key is on, too.) An empty slot's
  knobs read `-` and do nothing.
- **SEQ.** LENGTH's number and one is the steps (1 to 128). The generator's four rewrite the
  pattern while RANDOM or EUCLID is lit, as on the screen.
- **CONFIG's pages** hold whatever CONFIG is open on: an instrument's, an effect's, a return's,
  MOTION's two at the end. SF-000 sends each knob's name (four letters) and a title
  (`BAS CFG 1/3`). INFO on the screen shows each control's number, and resting the mouse on one
  says which page and knob it is. A list takes a share of the knob's travel for each choice; a
  switch is on in the upper half. A knob is left where your hand put it (its number is only moved
  when the control is changed from somewhere else). A key that has to be held down is not on a
  knob, and controls past the thirty-sixth are not on the E16.
- **Older firmware.** Names and titles need the E16's `slots.update` and `page.setTitle` (OS
  1.3.0's manual has them). Without them the script leaves the scene's own names: `INS1 1`,
  `CFG 7`. Everything else works the same.

### The library's pages

```
page 1: MAP, on channel 15. The grid is the library by sound:
        PEAK  DK  WM  CL  BR      rows: energy, the top row the most
        DRIVE DK  WM  CL  BR      columns: dark, warm, clear, bright
        EASY  DK  WM  CL  BR      (ring colours: red, pink, light blue, white)
        CALM  DK  WM  CL  BR
page 2: FIND, on channel 14: 1 RECENT, 2 FITS NOW, 3 MORE, 4 LESS; 16 GLIDE
```

- **Turn** a knob: it steps through the snapshots it holds, and each is heard in the headphones
  (outputs 3 + 4, or the right ear with PHONES lit), in time with the set. The set is not touched.
- **It steps, four clicks to the next** (fewer where it holds more than thirty), counted from
  wherever the knob is, and it stays on the snapshot it has come to until it is turned again.
  The first click on a knob that was on nothing plays the first it holds.
- **Press** it: the snapshot it is on is recalled to the set, over the GLIDE time, and the
  headphones go quiet.
- **The ring shows where that snapshot stands among the others**: the first a little up from
  nought, the last at the top, one alone in the middle. SF-000 puts it there a moment after your
  hand stops, which changes the knob's number on the E16 and nothing else.
- A square with nothing in it does nothing, and its ring sits at nought.
- What a knob holds is worked out when your hand comes to it (FITS NOW, MORE and LESS from what
  is playing then), and stays put while it is there.
- Only snapshots taken while the clock ran have a sound kept, so only those are on the map.

## Addresses

| WHAT | CHANNEL | NUMBER |
|---|---|---|
| a lane page's knob in place p (1-16) | the lane's number | CC 11 + p (12-27) |
| MID (knob 16 swapped) | the lane's number | CC 28 |
| a snapshot knob p | 16 | CC 11 + p (12-27) |
| a snapshot knob's press | 16 | CC 43 + p (44-59), 127 down and 0 up |
| GLIDE (knob 1 swapped, on any lane's page; FIND's knob 16) | 16 | CC 30 |
| the pointer's wheel, where knob 13 is nothing else | 16 | CC 31 |
| knob 13 pressed (the wheel), knob 14 pressed (OPEN) | 16 | CC 33, CC 34 (127 down, 0 up) |
| CONFIG's page before, the next (knobs 15, 16 pressed) | 16 | CC 35, CC 36 |
| the returns' eight knobs, the master's three | 16 | CC 60-67, CC 68-70 |
| the page that came up | 16 | CC 119: 1-12 in SF-000 (12: the snapshot page), 21-32 in SF-FX |
| a MAP knob in place p, its press | 15 | CC 11 + p, CC 43 + p |
| a FIND knob in place p (1-4), its press | 14 | CC 11 + p, CC 43 + p |
| the active lane's insert, slot s (0-2), place p (1-8) | 13 | CC 7 + 8 s + p (8-31) |
| the same place pressed | 13 | CC 39 + 8 s + p (40-63) |
| the active lane's LENGTH; HITS, ROTATE, ACCENTS, SEED | 13 | CC 70; CC 71-74 |
| CONFIG's control n (1-36) | 13 | CC 75 + n (76-95), then CC 81 + n (102-117) |
| **to the E16**: a page of the scene it is in | 12 | Program Change = the page less one, by itself |
| **to the E16**: a page of the other scene | 12 | CC 0 = 0 and CC 32 = the scene's slot less one, then Program Change = the page less one; the Program Change again when the scene says it is up |

A knob with a centre (TUNE, FILTER, LOW, MID, HIGH) is centred at 64 exactly. Nothing is on a
number MIDI keeps for itself (0, 6, 32, 38, 96 to 101, 120 and over), except Bank Select itself on
channel 12, which carries nothing else.

To the script, as SysEx (it takes them if the firmware can show them):

```
F0 7D 53 46 01 {channel-1, number, value / 128, value % 128} ... F7   a travelling knob: where the sound is
F0 7D 53 46 02 {channel-1, number} ... F7                              arrived
F0 7D 53 46 04 {page, sixteen names of four characters} F7             the page's names (four spaces: the scene's own)
F0 7D 53 46 05 {title, up to fifteen characters} F7                    its title (none: the scene's own)
```

## The snapshot knobs' names

The scene in this folder has the names the rig starts with (ENERGY, BUILD, ACID, DIST, WARP, then
SNAP 6 to SNAP 16). After naming knobs in the macro designer, write the scenes again and reload:

```
build/SF000_artefacts/Release/SF-000.app/Contents/MacOS/SF-000 --e16 controllers/e16/SF-000.oxie16
```

## Colours

The suite's ring colours by the OXI App's numbers: white 34, red 63, light blue 18, pink 50,
violet 9, green 90 (a travelling knob). They are in `Source/Midi.h`. The wheel and the keys of
the second scene's bottom row are pink.

## What only the E16 can say

**Seen on Wiley's E16:** with a Bank Select sent before every Program Change (every build up to
the twenty-fourth) the E16 ended on KICK's page, the scene's first, whatever page was asked for.
The knobs, their presses and the wheel reach SF-000.

**How SF-000 turns it now**, reasoned from that and from the manual, not yet tried:

- **Within a scene, a Program Change by itself.** No Bank Select, so the scene is not loaded
  again. This needs PC Mode on Page.
- **To the other scene, Bank Select and the Program Change, then the Program Change again.** A
  scene that loads comes up on its first page; its script says which page is up as soon as it
  runs, and if that is not the page asked for SF-000 says the page again by itself. If the
  script says nothing, the page is said again anyway 0.4 and 1.2 seconds later.
- **Any turn the E16 does not answer is said twice more** (a quarter and three quarters of a
  second later within a scene), and a page it says in answer that is not the one asked for is
  not followed on the screen.
- **Two numbers in `midi.json`** (beside the session; quit SF-000 before editing it):
  `"e16_page_first": 1` if every turn lands one page short (SF-000 sends the page less one, as a
  Program Change usually counts, and the manual does not say); `"e16_scene_always": true` to say
  the scene with every page as before.

Read from the E16's manual for OS 1.3.0 (docs.oxiinstruments.com, MIDI In and Lua Scripting), not
tried:

- **Whether a knob takes a number sent to it while a hand is on it.** The wheel's knob is set
  when the pointer is taken up (the hand has pressed it, and is not turning it yet), so that it
  is never at the end of its travel with modules still to go, and set again at a pause if it is
  found well away from there. SF-000 does not count on the number being taken (clicks are
  counted from wherever the knob says it is), but a knob that never took it and stands at
  nought has nothing left to send when turned back. If the pointer ever will not go back, that
  is what happened: pause for a moment and it should free itself; say so if it does not.
- **How quick a change of scene is.** A lane's page to one of its inserts is a change of scene.
  If it is slow or untidy in the middle of a set, the pages can be dealt out differently.
- **Whether the script is told of a page turned by Program Change.** It looks at the page on
  every update (50 ms) in case it is not, so either way it says the page and names it.
- **The press of a knob with no turn of its own** (OPEN on the second scene's pages): written as
  the suite's hold press is, on a knob whose turn is off.
- **Names of four letters** for an insert's and CONFIG's knobs: whether `slots.update` shows them
  where the scene's own names were.
