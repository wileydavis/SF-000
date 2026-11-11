# Decisions made while building the first SF-000

Wiley asked for a build he could compile in the morning and left the open choices to the build.
Each one is listed here so it can be overruled. Nothing below is locked.

## How it is built

1. **The suite's processors run inside the app.** Every lane, insert, return and master stage is the
   suite's own `SFProcessor`, built in-process and driven by SF-000's clock. This was the fastest
   way to the suite's real sound with its 64-sample chunking, smoothing and oversampling intact.
   The spec's fuller plan (a new registry with tags, plain state in place of SF BUS) is not done;
   `Source/Rig.h` is the one rig file the spec asks for.
2. **The suite's code is copied, not linked.** `suite/` is `SF-Live-Suite` `main` at `4eb9405`.
   One line is changed: the shared-memory bus has its own name, so SF-000 and Live never share one.
3. **Knob values go to the processors on the audio thread**, glided there, so a move lands the same
   way at any buffer size and a long glide does not depend on the screen.
4. **One target.** The checks, the screenshot tool and the knob audit are flags on the app itself.

## Lanes: one fixed model each

| Lane | Plays | Why |
| --- | --- | --- |
| KICK | KICK 909-TYPE, with KICK 808-TYPE as the layer playing the lows | LAYER brings the 808 body under the 909 |
| CLOSED HAT, OPEN HAT | PERC, HAT 909-TYPE, short and long | |
| CLAP | PERC, CLAP | |
| PERC | STRIKE, MEMBRANE | SIZE, TENSION and MATERIAL take it from a tom to metal to glass |
| BASS | BASS LADDER with SINE SUB layered under it | rolling bass with a steady sub |
| RUMBLE | RUMBLE, REVERB model, fed by the kick and its own steps | its pattern starts empty: the kick drives it |
| LEAD | ACID 303-TYPE | ties slide, hard steps accent |
| CHORD | LEAD module, STAB DUB | one note plays the chord |
| PAD | DUB WASH, chord MIN7, with drift | one held note is a moving minor seventh |
| VOICE | VOICE, RADIO model | a voice over a bad radio link. With no phrase it sings a formant choir; drop a file on the lane for a phrase |

To change one, edit its line in `Source/Rig.h`. The model is a parameter underneath, so a knob or
a snapshot could reach it later.

5. **Knobs map one to one** onto the suite parameter of the same name, across its whole range.
   Nothing is tamed at the edges. Parameters with no knob are fixed in `Rig.h`.
5a. **Two knobs differ from the drawn screen**, because the audit (`SF-000 --audit`) showed they did
   nothing on the chosen model. BASS place 2 is LAYER (the sine sub's level) in place of ACCENT.
   LEAD place 1 is TUNE in place of GLIDE: the 303 model always slides over 60 ms and ignores GLIDE.
5b. **Knobs that need the right conditions to be heard:** BASS GLIDE needs tied steps. VOICE's TUNE,
   SIGNAL and GATE need a loaded phrase. SPREAD on the hats and clap and WIDTH on RUMBLE are stereo
   only. Every other lane knob changes the sound with the starting patterns.
6. **VOICE's stutter is not on top of every model yet.** The suite has STUTTER as a model of its
   own. Stutter over any model needs new sound code and is not in this build.
7. **ALLOC (automatic frequency space) is off** on every lane. It needs the suite's ALLOC module in
   the graph, which is not wired in.

## Mixing and loudness

8. **Lane EQ:** LOW is a shelf below 200 Hz, MID a wide bell at 700 Hz, HIGH a shelf above 2.5 kHz.
   Centre is flat, down cuts to -24 dB, up lifts to +9 dB. It sits after the inserts.
9. **Sends are after the EQ and the level.** SEND A feeds a dub delay (1/8 dotted, ping-pong),
   SEND B a reverb. Both returns are wet only. Each lane starts with sends that suit it.
10. **Master chain:** the lows are made mono below 150 Hz (LOWEND), then TAPE (the suite's DRIVE at
    its tape end), COMP (GLUE, 2:1, slow attack, auto release), LIMIT (the suite's true-peak limiter,
    ceiling -1 dB). One knob each.
11. **The LIMIT knob sets the loudness target** (-16 to -5 LUFS, starting at -9). The limiter's AUTO
    trims its own input slowly until the loudest passage sits at the target. It lifts only a mix
    already within 6 dB of the target, so a breakdown is held where it was. The factory rig
    measures -9.4 LUFS with peaks at -1.1 dBFS.
12. **GR is approximate.** It compares the limiter's output peak with its input peak, so it also
    shows AUTO's trim. A true gain-reduction readout needs a tap inside the limiter.
13. **Lanes are delay-compensated** to the latest one (the kick's pocket needs lookahead), so every
    lane leaves about 21 ms late and together.

## Sequencer

14. **A sixteenth per step**; step rate per lane, probability and timing nudge per step are not in.
15. **Legato and ties.** A gate that reaches the next lit step overlaps it (a slide on ACID, a glide
    on the mono models). If the next step holds the same note, it is a tie: one note, no new attack.
16. **Ratcheted steps do not tie.** Turning a step into a ratchet pulls its gate back to one step.
17. **NOTE steps through all twelve semitones** and OCTAVE through octaves 1 to 5. KEY transposes.
18. **Lanes that take notes:** BASS, LEAD, CHORD, PAD, VOICE (on VOICE a note picks a slice of the
    phrase). The drum lanes play one fixed note.
19. **RANDOM leans to the beat.** Each step has a seeded rank, weighted to quarters, then eighths.
    HITS takes the top of the ranking, so turning it never moves a hit already there.
20. **Generated steps** get velocity 60 % (accents 100 %), gate half a step, no ratchet.

## Snapshots and the snapshot knobs

21. **A snapshot holds** every lane knob, sends, lane EQ, insert types and their knobs, returns,
    generator knobs, KEY and all eleven patterns. It leaves alone TEMPO, GLIDE, the master knobs and
    the snapshot knobs themselves.
22. **A snapshot knob's first snapshot is its zero.** Turning the knob adds, to the base scene, the
    difference between where it is in its sweep and its first snapshot. Knobs stack and clamp.
23. **Only sound knobs morph.** Menus, switches, insert knobs and patterns change on a full recall
    only. Insert knobs could morph when both snapshots hold the same effect; they do not yet.
24. **A recall glides the sound knobs over the GLIDE time** and switches the rest at once. A glide
    starts on the next beat while the clock runs. Knobs moved by hand are immediate.
25. **Pressing a snapshot knob** (double-click) recalls its nearest snapshot as the base scene and
    returns that knob to zero. The spec left open whether to take the in-between state instead.
26. **Five factory snapshots and knobs** (ENERGY, BUILD, ACID, DIST, WARP), each a push in one
    direction from INIT, so the knobs do something on the first run. They are ordinary snapshots:
    rename, delete or rebuild them.
27. **Taking a snapshot does not change the base scene.** Recalling one does.
28. **No audio preview, traits or sound map** in the library yet. A card shows how busy each lane's
    pattern is.

## Screens

29. **AUDIO** is a white key in the top bar. It opens the one set-once dialog (interface, outputs,
    buffer size). Nothing else opens a dialog.
30. **The screen is drawn by the CPU**, repainting only what changed. The spec asks for GPU drawing;
    measure on the Mac first.
31. **Deleting a snapshot** is press-and-hold on the red key, with no question asked.
32. **The stage (dark) palette is not built.** Only the day palette is.
33. **Sheet 1 of the visual language still says legends sit on white plates.** The main screen
    dropped them; the sheet has not been updated.

## Second build (Nov 11): CONFIG, mute and solo, meters

34. **C, M and S are small lettered keys** at the right of every lane, insert and return header, in
    place of the three INS lamps. The letters go on the keys because there is no room for a legend
    under them. C is white (it opens another surface) and black while its panel is open; M lights
    amber, S lights green.
35. **CONFIG moves the top three rows over by two columns** and leaves the sequencer strip alone,
    so the two right-hand columns (and MASTER) are off the screen while it is open. Opening a
    lane's CONFIG makes that lane the active lane. (Superseded by 52.)
36. **CONFIG shows every module parameter that has no knob on the main screen**, six to a row, in
    the module's own order, under the module's own display from the suite. Nothing is hidden or
    regrouped yet.
37. **SOLO on an insert plays only what the effect adds:** its MIX is held at full and its own DRY
    switch off while SOLO is lit. MUTE passes the lane round it; the effect keeps running so its
    tail is current when it comes back.
38. **SOLO on a return** silences every lane's dry signal and the other return.
39. **Mute and solo are not stored in snapshots.** They are kept in the session, so they come back
    when the app is reopened.
40. **A gate of exactly one step is not a tie.** New steps start with a whole-step gate, so two lit
    steps side by side strike twice. Legato needs the gate dragged a little past the next lit
    step's edge (the bar shows a link). The factory LEAD pattern keeps its two slides this way.
41. **Generated steps** now start at velocity 75 (accents 100) and a whole-step gate, as hand-made
    ones do.
42. **CLEAN SLATE cannot be lost:** if it is deleted it is back the next time the app starts.
43. **Dropped files are remembered by the session, not by snapshots.** A snapshot recall never
    loads audio from disk. (For inserts this is superseded by 56: their files travel with
    snapshots. It still holds for a lane's sample and for SPACE.)
44. **VOICE starts with a clearer radio (SIGNAL 70 %) and a one-beat step**, so a dropped phrase
    is plainly heard. The first build's step was a tenth of a second long, which is the likely
    reason the phrase "did not seem to load": the offline test shows the file loading and
    changing what the lane plays.
45. **Lane meters read each lane after its EQ, mute and solo**, before the master chain. The MIX
    pair reads the final output.
46. **A gliding knob is green with a white mark at its target.** (In this build knobs moved by hand
    did not glide. They do from the fourth build: 59.)

## Third build (Nov 11): the convolution on SPACE, CONFIG beside its panel, the quit crash

47. **SPACE (return B) is REVERB or CONVOLUTION, chosen by MODEL in its CONFIG.** The spec says
    "B SPACE is SF REVERB, or SF TERROIR in its place". It starts as REVERB, so the rig sounds as
    it did. Dropping a file on SPACE's CONFIG, or on the RETURNS panel, switches it to CONVOLUTION
    and loads the file. Both reverbs stay built, so the places are still loaded when CONVOLUTION
    comes back; only the chosen one runs. The change takes about 20 ms of silence on the return.
48. **Each reverb has its own four SPACE knobs** (same labels, separate values, separate snapshot
    ids `ret.space_*` and `ret.conv_*`). SIZE and DECAY mean different things on the two: on the
    convolution they start at 100 %, the place as it was captured. Snapshots taken before this
    build hold no convolution knobs, so recalling them leaves those where they are.
49. **Which reverb SPACE is belongs to the rig, not to a snapshot.** Recalling any snapshot,
    CLEAN SLATE included, leaves the model alone: the room is where you are playing, not part of
    a scene. The rest of SPACE's CONFIG (PLACE included) is recalled only by snapshots taken on
    the same model. Say if the model should travel with snapshots instead.
50. **Impulse responses are remembered by path**, per place, and read again when the app opens.
    If a file has moved, that place is empty (the built-in room). The suite can also keep the
    audio itself in its state; SF-000 does not use that yet.
51. **A file dropped on an open CONFIG panel goes to that panel's target.** If the target takes
    no file (the DUB delay, a compressor), nothing loads and the panel says so. Dropped anywhere
    else it goes to the lane under the pointer, an insert that takes files, or the active lane.
52. **CONFIG brings its panel beside it.** The top three rows slide so the configured lane's
    column (an insert's, the returns') is the third column, next to CONFIG. To keep the screen
    full the six columns wrap round, so for the right-hand columns the left-hand ones appear to
    the right of them, and MASTER (two columns wide) can be cut in half at CONFIG's edge.
53. **The convolution's CAPTURE key does nothing useful here.** It asks for SF CAPTURE on the
    suite's bus, which SF-000 does not have. SWEEP FILE works: it writes the sweep to
    Music/SF Live Suite/TERROIR, and the recording of it can be dropped on a place.
54. **CLEAN SLATE is rebuilt each time the app opens**, so it covers controls a newer build adds.
55. **The crash on quit** was the embedded typefaces being destroyed after JUCE had shut down
    (macOS only). They are now released in the app's shutdown.

56. **An insert travels with snapshots whole:** the effect, its knobs, its CONFIG and the files
    it holds (a convolution's impulse responses, per place). Only the two returns are rig
    settings. A recall leaves an insert running if it is already as the snapshot has it;
    otherwise the slot gets the snapshot's effect with the snapshot's files.
57. **A recall does not wait on the disk for an insert's files.** A file-holding insert that is
    taken out is kept aside with its places loaded (up to twelve), and when the app opens every
    such insert the library's snapshots name is made ready ahead of time. It goes back in with
    the right room from its first block. The one case that reads the disk at recall: a snapshot
    whose insert held files that have since been replaced on the running insert by a new drop;
    the built-in room sounds for the moment the read takes.
58. **Snapshots taken before this build say nothing about insert files.** Recalling one changes
    the effect in the slot as before and leaves whatever files that slot last held.

## Fourth build (Nov 11): glide under the hand, MIDI, the E16, the clock

59. **A knob turned by hand travels over the GLIDE time**, from the screen or the E16, as the suite's
    SLOW ALL did and as the spec's glide section says ("applies to both knob turns and presses").
    Until now only recalls and snapshot knobs glided, which is why turning a knob with GLIDE on did
    nothing to see or hear. OFF is at once. TEMPO, the master knobs, menus and switches never glide.
60. **A move already on its way does not wait for the beat again.** A new move sets off on the
    next beat while the clock runs; turned again before it arrives, it carries on at once from
    where the sound is. Before this a knob still being turned kept putting its start off.
61. **An insert's knobs glide under the hand, but a recall still sets them at once** and no snapshot
    knob reaches them: the same cell is a different knob when the slot holds another effect.
62. **Notes in: every input is open, a lane's number is its channel.** Of the E16's USB ports only
    the first is listened to, since it sends everything on each. Notes from the E16 are ignored.
63. **A note sounds in the block after the one it arrived during**, at the matching place in the
    block, so even spacing in is even spacing out. That is one buffer of delay on notes from
    outside, on top of the device's own.
64. **The clock goes to every output that is not the E16**, on or off as a whole. It runs ahead of
    the sound by two blocks and CLOCK LEAD so a follower's notes are back in time to land on the
    beat, and RUN waits that long before the first sound. LEAD starts at 0 ms: the OXI One's delay
    has to be measured on the hardware (the panel shows how late its notes land).
65. **The ticks are sent from their own thread**, each at its own moment, to within about a
    millisecond. USB MIDI is no finer than that. If the OXI One needs tighter, CoreMIDI can be given
    timestamps directly; that needs code only a Mac can build and test.
66. **SF-000 does not follow an outside clock.** The spec proposed the internal clock as the main,
    tested path.
67. **The E16 scene is written by the app** from the rig table, in the suite's PERF scene's form:
    lane n is page n and channel n, a knob in place p sends CC 11 + p. Ring colours follow the
    screen (white sound, red shape, light blue levels and EQ); the snapshot knobs are violet (the
    suite's colour for the set's own moves) and MID is pink.
68. **HIGH and MID share knob 16 by the E16's own Swap Destination press**, as knob 1 and SLOW ALL
    did in the suite. That knob's ring never goes green: the script cannot tell which is up.
69. **The E16's page sets the active lane, not the other way round.** The script sends the page's
    lane number as the suite's TRACK FOCUS did. Nothing in the suite's script turns the E16's page
    from outside, so a lane clicked on the screen leaves the E16 where it is.
70. **A knob the hand is on is not sent its own value back** for 150 ms, so the rig never argues
    with a turn in progress.
71. **The returns, the inserts and the master are not on the E16.** You asked for a page per lane
    and the snapshot page.
72. **A recalled step pattern waits for the next bar line** while the clock runs, to the sample. An
    edit changes at once. The screen shows the new pattern straight away. Insert changes, menus and
    switches still change at once.
73. **The session is backed up each time the app opens** (the file as found, into `backups/`, the
    last twenty kept).
74. **None of the MIDI has met the hardware.** It is tested with messages made in the test, and the
    scene's script on the suite's mock of the E16.

75. **The clock is sent only while SF-000 runs** (changed within the fourth build: it first ran
    free while stopped). Live did the same, and the OXI One's settings from the suite (Sync USB,
    Auto Sync on) are known to work with a clock that begins with START. Stopped, the ONE falls
    back to its own clock.
76. **The OXI One's project is SF START with 49 bytes changed:** the name (SF000) and the spare
    sequencers 7 and 8, moved from 7B and 16A to 12A and 13A. The parts' channels already matched.
    Its four scenes are the suite's. The tempo (120) is left as the ONE wrote it.
77. **SF-000 takes notes from every port and goes by the channel alone**, so port B's channel 7
    plays RUMBLE as port A's does. The suite kept the two ports apart.

## Load (Nov 11)

78. **Where the time went, as far as it can be seen from here.** On this build machine (a slow
    Xeon) the engine takes about 55 to 65 % of one core at any buffer size: BASS, RUMBLE, PAD, LEAD
    and CHORD about 5 to 8 % each, the returns 9 %, the master 7 %. An M4 is several times faster,
    so the engine should be well under 20 % there. Drawing the whole screen costs about fifty times
    what drawing the meters does. macOS draws a window's changed parts as one box round all of
    them, so meters in one corner and a moving knob or the step lamp in another meant most of the
    screen was drawn 30 times a second. That is the likely bulk of the 63 % seen in Activity
    Monitor; it has not been measured on the Mac.
79. **Each changed part of the window is drawn on its own** (JUCE's Metal layer renderer,
    `JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS`). JUCE says whether this helps depends on
    the app; it is the case it was made for. It can be turned off to compare:
    `cmake -B build -DSF000_SPLIT_PAINT=OFF && cmake --build build`.
80. **A part-redraw draws only the panels it touches** and keeps the layout from the last whole
    redraw. The meters are redrawn only when a bar has moved by a pixel or a number has changed, so
    a silent rig draws nothing. Text is measured once. A knob is marked as drawn only when it was.
81. **The clock's thread sleeps until a quarter of a millisecond before each tick** (it kept awake
    for a millisecond), and looks for work every 10 ms when no clock is being sent.
82. **AUDIO AND MIDI shows the engine's own load**, so the sound's share can be told from the
    screen's. `SF-000 --perf` measures both offline.

## Not verified

- Built and run on Linux only. It has not been compiled on macOS or heard by anyone.
- The mix is balanced by measurement: levels, loudness, peak. It needs ears.
