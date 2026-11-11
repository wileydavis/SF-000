# The E16 scene for SF-000

`SF-000.oxie16` is one scene for the OXI E16: a page for each lane and a page for the snapshot
knobs. SF-000 writes it itself from the rig (`SF-000 --e16 SF-000.oxie16`), in the form the suite's
PERF scene has, so the screen and the E16 cannot drift apart. `rings.lua` is the script inside it,
the suite's GLIDE RINGS script unchanged.

It has not been on an E16 yet. It was checked against the suite's PERF scene object by object, and
its script against the suite's mock of the E16 (`tests/e16_scene_test.py`).

## Load it

1. OXI App > Scenes. Copy `SF-000.oxie16` into the Scenes folder on the computer and click
   Refresh Views.
2. Drag SF-000 from On Computer onto a free slot under On Device.
3. E16 settings, as for the suite: **Auto Transmit off** (scene and global) and **MIDI Thru off**.
4. Open SF-000. AUDIO AND MIDI should read `E16: OXI E16 ...`. Every ring jumps to the rig's values.

SF-000 listens to the E16's first USB port only (the E16 sends everything on each of its ports)
and sends the rings to the same port.

## The pages

```
pages 1-11: a lane each (KCK CHH OHH CLP PRC BAS RMB LED CRD PAD VOX), on the lane's own channel
 1  2  3  4      the lane's twelve knobs, where the screen has them:
 5  6  7  8      rows 1 and 2 its sound (white), 9-11 its shape (red), 12 LEVEL (light blue)
 9 10 11 12
13 SEND A   14 SEND B   15 LOW   16 HIGH [press: MID]

page 12: SNAP, the sixteen snapshot knobs (violet), on channel 16. Press: recall.
```

- **Knob 16** is HIGH. A press swaps it to MID and its ring turns pink; a press swaps it back. The
  E16 does this itself (Swap Destination) and keeps a value for each.
- **A snapshot knob's press** recalls the snapshot the knob is nearest to as the new base, and the
  knob goes back to zero, as a double-click on the screen does.
- **Turning to a lane's page** makes that lane the active one on the screen (its inserts, its
  sequencer strip). The snapshot page leaves the active lane alone. The screen cannot turn the
  E16's page.
- **Rings.** A value changed on the screen, by a recall or by a snapshot knob is sent to its knob.
  While a knob travels (GLIDE), its ring is green and walks with the sound; turn it and the ring
  shows your hand for a moment. Knob 16 keeps its own ring (the script cannot tell HIGH from MID).

## Addresses

| WHAT | CHANNEL | NUMBER |
|---|---|---|
| a lane page's knob in place p (1-16) | the lane's number | CC 11 + p (12-27) |
| MID (knob 16 swapped) | the lane's number | CC 28 |
| a snapshot knob p | 16 | CC 11 + p (12-27) |
| a snapshot knob's press | 16 | CC 43 + p (44-59), 127 down and 0 up |
| the page that came up | 16 | CC 119, value the lane's number |

A knob with a centre (TUNE, FILTER, LOW, MID, HIGH) is centred at 64 exactly.

## The snapshot knobs' names

The scene in this folder has the names the rig starts with (ENERGY, BUILD, ACID, DIST, WARP, then
SNAP 6 to SNAP 16). After naming knobs in the macro designer, write the scene again and reload it:

```
build/SF000_artefacts/Release/SF-000.app/Contents/MacOS/SF-000 --e16 controllers/e16/SF-000.oxie16
```

## Colours

The suite's ring colours by the OXI App's numbers: white 34, red 63, light blue 18, pink 50,
violet 9, green 90 (a travelling knob). They are in `Source/Midi.h`.

## The second scene: the library (`SF-000-LIBRARY.oxie16`, SF-LIB)

A scene has twelve pages and the first uses them all, so the library is a scene of its own. Load it
beside the first and change scenes on the E16 to reach it. `SF-000 --e16 SF-000.oxie16` writes both.

```
page 1: MAP, on channel 15. The grid is the library by sound:
        PEAK  DK  WM  CL  BR      rows: energy, the top row the most
        DRIVE DK  WM  CL  BR      columns: dark, warm, clear, bright
        EASY  DK  WM  CL  BR      (ring colours: red, pink, light blue, white)
        CALM  DK  WM  CL  BR
page 2: FIND, on channel 14: 1 RECENT, 2 FITS NOW, 3 MORE, 4 LESS
pages 3-12: empty
```

- **Turn** a knob: it goes through the snapshots it holds, and each is heard in the headphones
  (outputs 3 + 4), in time with the set. The set is not touched.
- **Press** it: the snapshot it is on is recalled to the set, over the GLIDE time, and the
  headphones go quiet.
- A knob's 128 places are shared out among what it holds: with four snapshots in a square, each has
  a quarter of the turn. The ring sits in the middle of the one it is on.
- A square with nothing in it does nothing, and its ring sits at nought.
- FITS NOW, MORE and LESS are worked out from what is playing when your hand comes to the knob, and
  stay put while it is there.
- Only snapshots taken while the clock ran have a sound kept, so only those are on the map.

| WHAT | CHANNEL | NUMBER |
|---|---|---|
| a MAP knob in place p (1-16) | 15 | CC 11 + p |
| its press (127 down, 0 up) | 15 | CC 43 + p |
| a FIND knob in place p (1-4) | 14 | CC 11 + p |
| its press | 14 | CC 43 + p |

Not tried on an E16. The scene carries the first scene's script (which has nothing to do here), so
its form is one the E16 is known to take.
