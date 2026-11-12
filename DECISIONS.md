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
    measures -9.6 LUFS with peaks at -1.1 dBFS (16 bars, started from a settled rig).
12. **GR is approximate.** It compares the limiter's output peak with its input peak, so it also
    shows AUTO's trim. A true gain-reduction readout needs a tap inside the limiter.
13. **Lanes are delay-compensated** to the latest one (the kick's pocket needs lookahead), so every
    lane leaves about 21 ms late and together. (Changed in the seventh build: see 100. The lanes
    now leave 15 samples late, and together.)

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
    tested path. (Changed in the sixth build: see 87.)
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

## Fifth build (Nov 11): the sequencer's mute

83. **Each lane's sequencer has its own mute**: **M** in the sequencer strip, after the step
    count, amber when on. The lane's steps stay where they are and go unplayed, drawn grey. The
    playing lamp still runs, so the place in the bar can be seen. Notes from MIDI in still sound
    on the lane, which is what it is for: the OXI One playing a lane without the lane's own
    pattern on top.
84. **Muting lets go of the note the sequencer holds, at once; unmuting waits for the next lit
    step.** It is a note-off, not a cut: the release, the echoes and the reverb ring out. Nothing
    needs timing by hand: the lane comes back on the grid. (A note of the same pitch held from
    outside at that moment is let go with it.)
85. **It is kept with the session, like MUTE and SOLO, and a snapshot recall leaves it alone.**
    *proposed*: say if snapshots should carry it. A snapshot can already silence a lane's
    sequencer by holding an empty pattern for it.
86. **It is on the screen only.** The E16's pages are unchanged.

## Sixth build (Nov 11): following a clock

87. **The clock is sent, followed, or neither**, one choice in AUDIO AND MIDI (it was a SEND
    switch). You asked for the OXI One as the clock: tempo, start and stop on it, and other gear
    driven from it alongside SF-000. A rig that followed and sent at once could be answered by its
    own clock, so it never does both.
88. **Following takes the sender's ticks, START, STOP and CONTINUE.** START's next tick is bar 1's
    first step. A song position sent on its own is not read. The clock is taken from one input at a
    time: the first to send, until it has been silent for a second. The ONE shows several USB ports
    and the same clock on two would otherwise count double. The E16's ports are never the clock.
89. **The sender's clock is a line through its ticks, not the ticks themselves**: each tick's time
    goes through a delay-locked loop, narrow (1.2 Hz) once settled and wide (3 Hz) for the first two
    beats and whenever two ticks running are more than 1.5 ms off the same way (the tempo was
    turned). One late tick on its own moves it little. Measured offline at 128 samples a block:
    ticks up to 1 ms early or late leave the steps within 0.45 ms (0.7 ms with the head start of
    ON THE BEAT, which reaches a tick or two past the last one heard); a jump from 126 to 140
    within 0.4 ms; a sweep from 120 to 135 over three seconds within 1 ms.
90. **The tempo shown and told to the instruments is that line's, slowed, and set on the tenth of a
    bpm it is plainly at**, so echo times do not wander. It is written to the TEMPO control: the
    readout says CLOCK IN, a hand on TEMPO is overruled, and it is the tempo the rig keeps.
91. **With no clock coming in it runs on its own.** If the ticks stop without a STOP it plays on
    alone at that tempo rather than stopping the show. If ticks begin while it is already playing,
    it takes the nearest one for where it is, so nothing jumps.
92. **RUN by hand still works while following.** Stopped and started here under a running clock, it
    comes back in at the sender's place in the bar. Started here while the sender is stopped, it
    plays from the top, and the sender's START later takes it back to the top.
93. **WITH NOTES IN is how it starts** (your word, Nov 11: gear on the ONE is the exception, the
    ONE and the E16 the rule). The rig's own steps get no head start and are heard with the lanes
    the sender plays by MIDI, so everything SF-000 plays is together; gear the sender plays
    directly is heard before it. **ON THE BEAT** is for that gear: it plays the rig's own steps
    ahead of the clock by the time the rig takes to sound (one buffer's wait, the interface's
    output delay as the system reports it, the look-ahead in the lanes and the master), so they
    are heard with the gear. Lanes the sender plays by MIDI cannot be played ahead and are then
    heard that much later.
94. **TRIM is plus or minus 30 ms on top**, for the ear. The figure is worked out afresh as the
    buffer or the inserts change, so it does not go stale.
95. **With ON THE BEAT, bar 1's first step after START cannot be played ahead and is played at
    once.** No step is ever skipped to catch up: what should already have sounded is played in the
    first block.
96. (Superseded by 100.) **Most of the delay was look-ahead, about 25 ms of some 30** at 48 kHz:
    512 samples in each instrument (the suite's pocket look-ahead), 512 more on a lane with a
    look-ahead insert, and 180 in the master.
    I left it alone. Shortening it would change the sound's design; it is the thing to look at if
    30 ms between the ONE's gear and the lanes it plays is too much.
97. **The followed clock has not met the ONE.** It is tested with made-up ticks. I do not know the
    ONE's jitter over USB, whether it needs a setting to send its clock, or whether the MkII has
    the per-sequencer time offset the first ONE's beta firmware had.

98. **AUDIO AND MIDI always shows the rig's LATENCY**, to a tenth of a millisecond, with its three
    parts: the figure to delay outside gear by. It is worked out, not measured. The buffer's wait
    and the look-ahead are exact (offline, a note from outside begins within 0.15 ms of their sum);
    the interface's part is what the system reports for the device. A measurement, a click out of
    an output and back into an input, is not built: SF-000 opens no audio input.
99. (Superseded by 100 and 101.) **The figure moved** with the buffer size and with which inserts
    were in: 14.4 ms of look-ahead with none, 25.1 with the starting rig's, at 48 kHz.

## Seventh build (Nov 11): the look-ahead taken out, the latency steady

You asked for a steady, known latency (and whether a smaller buffer or the driver could cut it).

100. **The suite's 512-sample look-ahead is built as 0 in SF-000** (`SF000_POCKET_LATENCY`, a build
     option; 512 builds the suite's own). Each instrument and each DELAY, REVERB and CONVOLUTION
     held its sound back 512 samples so the kick's reports reached it in time whatever order Live
     ran its plugins in. SF-000's engine plays the kick's lane first in every buffer, so the
     reports are already there. The pocket never looked ahead of the kick: it only waited.
101. **Every lane is held to one fixed wait, whatever is on it**: its instrument's and the slowest
     effect's in every slot, which is now 15 samples (the distortion's oversampling, 5 each). So
     the rig's own delay is 4.1 ms at 48 kHz, 196 samples, nearly all of it the limiter's
     look-ahead, and no insert or recall moves it. With a 128-sample buffer the whole LATENCY is
     about 10 ms; it was about 32 and could move by 11.
102. **Checked against a build with the 512 left in**, both started from a settled rig (two
     seconds stopped, as the app is before RUN). With no inserts or sends: kick, both hats, clap,
     perc, bass, lead, chord and voice come out the same to the last bit, 512 samples sooner; the
     pad differs by less than a thousandth; the rumble has the same level and spectrum and its
     noise falls differently. The whole rig has the same level (0.01 dB) and the same spectrum to
     0.15 dB in every octave. Its waveform is not the same, for the reasons in 103 and because
     the delay's wow and the reverb turn at a different moment against the music.
103. **What does change in the sound.** The two returns were not delay-compensated, so their sound
     left 512 samples (10.7 ms) after the lanes'. Now the dub delay's echoes land on their
     division (offline: 9.5 ms late before, within 1.2 ms now) and the reverb answers 10.7 ms
     sooner. A CONVOLUTION insert's wet sound now comes 10.7 ms after its dry (the convolver's own
     delay, as pre-delay) where the dry used to wait for it. Listen for these three; the build
     option puts the old way back.
104. **A smaller buffer helps a little, the driver not at all.** SF-000 is on Core Audio already.
     The buffer counts about twice (the wait for the next one, and the one on its way out): 64
     samples instead of 128 takes off about 2.7 ms. ENGINE in the panel is the headroom to watch.
     Not tried on a device at 32.
105. **The offline test now starts from a settled rig**, as the app does. Started cold, the first
     hit meets smoothers still moving, and the limiter's AUTO, which remembers the loudest second
     for a minute, held the level 0.6 dB down for that minute.

## Eighth build (Nov 11): recording, the audio

The spec asks for multitrack audio, MIDI and every parameter move, written off the audio thread,
and a set that can be rendered again. This build is the audio.

106. **A take is the set, the eleven lanes and the two returns**, each a stereo file, in a folder
     under `Music/SF-000/` named for when it began. The lanes and returns are taken where they
     enter the mix (after inserts, EQ, MUTE and SOLO, and a soloed return's silencing of the
     lanes), so together they are exactly what the master chain was given, and what was played is
     what is kept.
107. **The set is 24-bit; the lanes and returns are 32-bit float.** The limiter holds the set under
     full scale. A lane can pass full scale on its way to the mix, and a float file cannot clip.
     That makes a take about 19 GB an hour. *proposed*: say if 24-bit lanes (14.5 GB an hour, and
     a lane that runs hot clips in its file) are the better trade.
108. **WAV, not split.** JUCE writes plain WAV up to 4 GB and RF64 past it, which is the spec's
     "format without that limit". A 32-bit float file passes 4 GB after 2.9 hours. If something
     you use will not open RF64, say, and takes can be split instead.
109. **The lanes and returns begin with the master chain's look-ahead in silence** (181 samples),
     so every file in a take lines up. Checked: added together, they fit the set best at 0 samples.
110. **A lane or return silent all take is not kept**, and `take.txt` names it. A lane you never
     played would otherwise cost 1.4 GB an hour.
111. **REC starts on a press and ends on a hold** (0.7 s), like DELETE in the library, so a stray
     touch cannot end a set's recording. It runs whether or not the clock does: nothing before the
     first RUN or between stops is missed. No key on the keyboard, for the same reason.
112. **The audio thread copies each buffer into a four-second ring and a thread of its own writes
     the files.** If the disk falls more than four seconds behind, whole buffers are left out of
     every file alike, so the files stay in step; the key turns amber and `take.txt` says how
     much. A take ends by itself, kept, if a write fails, if the disk has a minute of room left,
     or if the sample rate changes.
113. **Lengths are written into the files every ten seconds**, so a take cut off by a crash still
     opens up to there. Quitting ends the take properly.
114. **The session is copied into the take as it begins** (`session-at-start.json`): the rig the
     recording started from.
115. **Not in this build: MIDI and knob moves, and rendering a set again.** They belong together:
     a log is only worth having if the set can be played back from it, to the sample.
116. **Recording has not met a Mac.** It is tested offline: the set's file is the output sample
     for sample, the parts line up, a silent lane is dropped, a second take gets its own folder,
     and the key starts on a press and ends on a hold. Not tested: `Music/SF-000` on macOS, a real
     disk falling behind, a file past 4 GB, hours of running.

## Ninth build (Nov 11): a snapshot keeps its sound

You put this ahead of recording knob moves: no barrier to taking a snapshot, nothing to name or
tag, and a library you can read at a glance on stage. The spec's sections 9 and 10 ask for the
same: a clip kept at every save, and a profile from the clip and from what the rig knows.

117. **The engine keeps the last twenty seconds of its output on a tape that goes round**, with
     where each bar line fell and how much of each lane and return there was. SNAPSHOT takes the
     last four whole bars from it, cut on the bar lines (and set back by the master's look-ahead,
     so the bar's first hit is the clip's first sample). What you heard when you pressed is what
     is kept, the One's notes included: nothing is rendered again.
118. **Four bars, not the spec's eight seconds**: at 132 that is 7.3 s, and a clip that is whole
     bars loops. Fewer if the clock has run for less.
119. **The clip is Ogg Vorbis** (about 80 KB), written by a thread of its own into `clips/`. The
     profile is worked out at once, on the press. Deleting a snapshot deletes its clip.
120. **The profile**, each from 0 to 1: energy, weight, bright, density, tone, movement, wet; the
     spectrum in sixteen bands; where the hits fall in the bar, low end and top; how much of each
     lane. Its scales are set from this rig's own sound and are fixed, so a snapshot's profile
     does not shift as the library grows. They will want tuning against your own library.
121. **Energy is 55% the mix's level before the master, 30% hits a beat, 15% brightness.** The
     output's own loudness would not do: the limiter evens it out. *proposed* weights.
122. **The card draws the profile**: one block as tall as the energy and as hot as the
     brightness, the spectrum, the hits in the bar, the lanes. *proposed* look: say what reads
     and what does not at arm's length.
123. **With the clock stopped nothing is kept** unless something is plainly being played (the
     stopped rig idles at -40 dB: the RADIO on the lead's lane). The snapshots the rig comes with
     have no sound kept either: they were never played.
124. **Noted, not built (as of this build):** the clip in the headphones; the E16 page (the grid as a map of energy
     against brightness, a knob for the most recent, a knob of what fits what is playing now);
     suggestions timed to where the set is; a learned model for the categories; effect chains
     that cross over the glide.

## Tenth build (Nov 11): cue

125. **The headphones are outputs 3 + 4** (the Babyface's phones). The app now opens four outputs;
     an `audio.xml` kept from before has two, so 3 + 4 want ticking in AUDIO once. With only two
     open there is no cue, and the library says so.
126. **The cue is the clip, played in time.** While the clock runs, where the clip is read follows
     the rig's place in its four bars, so it sits on the set and follows the tempo; its pitch
     moves with the tempo's ratio to its own (a clip kept at 132 and heard at 126 is 0.8 of a
     semitone down). It is the sound as it was kept, not the rig run a second time: no load to
     speak of and nothing added to the set's delay. The set is the same to the last bit with a
     cue playing.
127. **In the library, CUE turns a tap into listening and RECALL commits it.** *proposed*: a tap
     with CUE off recalls at once, as before. Leaving the library ends the cue.
128. **Effect chains that cross over the glide are required** (your word, Nov 11), after the
     library and cue.

## Eleventh build (Nov 11): the library on the E16

129. **The library is a second E16 scene (SF-LIB), not a page of the first.** A scene has twelve
     pages and the first uses all twelve (eleven lanes and the snapshot knobs). You asked for a
     page; this is two pages of a scene of their own, a scene change away.
130. **MAP: the grid is the library by sound** (your choice, A): energy up the rows, brightness
     along them. A snapshot's square is fixed by its own profile (each scale cut in four), so it
     does not move as the library grows. Squares will fill unevenly; say if the cuts want moving.
131. **A turn is heard, a press recalls.** Turning goes through what the knob holds, each in the
     headphones in time with the set; pressing recalls the one it is on over the GLIDE time. The
     E16's knobs send places, not steps, so a knob's 128 places are shared out among what it
     holds, and its ring shows which.
132. **FIND: RECENT, FITS NOW, MORE, LESS** (you asked for the first two; MORE and LESS are mine,
     *proposed*, toward dialling a room's energy up and down). FITS NOW is nearest in sound to
     what is playing. MORE and LESS are a step up or down in energy from it, nearest step first,
     kept to the twelve nearest it in colour. What is playing is profiled from the tape when a
     hand comes to the knob, and the list stays put while the hand is there.
133. **Likeness is a weighted distance over the profile**: brightness, weight, density, tone,
     wetness, the spectrum, the hits in the bar, the lanes, and energy. No learned model. With one
     rig's sounds and a library of tens or hundreds, measures made for this rig go a long way and
     can be checked; a learned embedding could add a finer sense of "sounds like" and is worth
     trying once there is a real library to judge it against. It could run offline.
134. **Noted, not built:** suggestions timed to where the set is (how long a state has held, where
     the energy has been), the map drawn on the screen, more finders by named trait, and showing
     on the main screen what is in the headphones.

## Twelfth build (Nov 11): the session file made to last

135. **Slots keep their roles** (your word): a slot is for a kind of part, and takes the modules
     made for that kind. The module format, when it comes, is built on that.
136. **The snapshot format only ever adds** (you asked for one that can grow and still read the
     old). `FORMAT.md` is the contract: a key keeps its name and meaning for good, new things are
     new keys, and the format number stays at 1 for every change of that kind.
137. **A library snapshot is written back exactly as it was read.** Mine. It is the simplest way
     to be sure nothing a newer build or another tool put in a snapshot is ever dropped, at any
     depth. It leans on a snapshot never changing after it is taken, bar its name; if that ever
     stops being true this has to change with it. It costs memory: the parsed file is kept, about
     fifty kilobytes a snapshot. When the library moves to a file per snapshot this cost goes.
138. **Every snapshot says what it was made with, and the file keeps the definitions.** Mine. Each
     lane, effect and return of the build is written out in full (what plays, what is fixed, which
     knob drives which parameter over what part of its travel, and every parameter's range), and a
     snapshot names each by a print, a hash of the definition. Prints are worked out, not
     maintained: change a lane's line in `Rig.h` and its print changes by itself. The one number
     kept by hand is `kChassisVersion` (the sends, the EQ, the generator knobs), to be raised
     when one of those changes what a stored value means. Today nothing reads the prints: they are
     for the day a slot can hold different modules, and for any tool that wants to know what a
     value was a position of without SF-000's source. 35 definitions, about 87 KB in the file.
139. **A snapshot's uid is the library's id, then its own.** *proposed.* For the online library a
     snapshot needs a name that is its own everywhere. It is made from two things that already
     last (a sixteen-digit id made once for the library, and the snapshot's id), so a build that
     drops it gives back the same one. The library's id is also kept in `library-id.txt`.
140. **A session in a newer format is left alone, and nothing is saved that time.** Mine. The
     other way was to put it aside and start a fresh file; then going back to the newer build
     finds a fresh library and the real one has to be put back by hand. This way nothing moves.
     The cost: snapshots taken in that sitting are lost, and a notice says so when the app opens.
141. **A session that will not parse is copied aside before it is replaced**, as
     `session-unreadable-<date>.json`, outside the twenty backups that come and go.
142. **An engine starts with no kick reports on the suite's bus.** The bus is shared memory and
     outlives an engine; a kick reported by an earlier one stayed "alive" for four seconds by the
     wall clock, so the first moments of a render depended on what had run just before. It showed
     as a check that failed now and then (two engines side by side, compared to the bit), and was
     there before this build; the build's changes to timing brought it out.
143. **Noted, not built:** the chassis (lanes as modules that load, an instrument fading as an
     effect will), each module's descriptor (declared and measured), the outbox and the online
     library, logging every recall as it is played (what follows what, for suggestions and an
     auto mode), and the rendered instrument: plates and strips of frames from your model, with
     the layout in one file both the renderer and the app read.

## Not verified

- Built and tested here on Linux only: nothing in this repo has been run on macOS by me. Wiley
  builds and plays it on his Mac.
- Not yet reported back from the Mac: the fix for the crash on quit, the cut in the screen's load
  (and the Metal split-paint flag behind it), and everything MIDI on real hardware: the E16 scene,
  its rings, knob 16's swap, the page following the lane, the OXI One project, the clock out and
  CLOCK LEAD, the clock followed, and the sequencer's mute with the One playing a lane.
- Not yet heard on the Mac: the rig without the suite's look-ahead (seventh build), recording
  (eighth), a snapshot's clip and profile (ninth), cue on outputs 3 + 4 (tenth), and the library's
  E16 scene (eleventh), which has not met an E16.
- The session file's changes (twelfth) are checked here against a file the eleventh build wrote.
  On the Mac the first launch saves your session again with a uid on every snapshot and the
  definitions added; the copy in `backups/` is the file as it was.
- The mix is balanced by measurement: levels, loudness, peak. It needs ears.
