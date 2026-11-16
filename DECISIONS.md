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
    from outside, so a lane clicked on the screen leaves the E16 where it is. (Undone in the
    twenty-third build, 242: the E16's own firmware takes a page from outside.)
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
    *proposed*: say if snapshots should carry it. (Changed in the twenty-ninth build, 289: a
    recall with NOTES lit sets it.) A snapshot can already silence a lane's
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

## Thirteenth build (Nov 11): ONLINE

144. **The web library is at an address you set** (you: snapshots.drosaka.com; its code is the
     repo `snapshot-library`). The address and the service token's two strings are settings,
     typed into SET UP and kept in `online.json`; nothing of them is in this repo.
145. **The strings are kept in a file only you can read** (mode 600), not the Keychain. Mine: JUCE
     has no Keychain call, and the token opens this one library and nothing else. The Keychain
     can come if you want it.
146. **ONLINE is a key on the library screen, with SET UP beside it.** *proposed.* The lamp says
     how it stands; a line says how many wait. It is not on the main screen: nothing about it
     needs a hand while playing.
147. **What has gone is what the library says it holds.** On each connection SF-000 asks the
     library which of its snapshots are there and which have their clip, and sends the rest. So
     "buffered for later" needs no queue of its own: a snapshot not yet acknowledged is the queue,
     whether ONLINE was out, the network was down, or the app was shut.
148. **The rig's own snapshots stay at home** (INIT and the five that come with it, CLEAN SLATE).
     Mine: they are the same on every rig and you did not take them. Say if you want them there.
149. **Deleting here does not delete there; removing there stops it being sent again.** Mine. The
     web library is the long memory; the instrument's library is what you carry.
150. **Definitions go once.** A snapshot's first send carries the definitions it names that the
     library has not had; the library says if it is missing any, and they are sent again.
151. **Sending never touches the audio thread, and reads the session only under the message
     thread's lock**, for the moment it takes to write one snapshot out. If the app is quit while
     a send hangs on a dead network, quitting can take up to eight seconds.
152. **Plain http is sent over a socket of SF-000's own; https by the system.** JUCE's Linux
     client puts a stray line break before a request's body, so the checks here (which talk to a
     library on this machine over http) could not use it. On the Mac, https goes through
     NSURLSession. That path is the one thing in ONLINE the checks here do not run.

## Fourteenth build (Nov 11): a snapshot's sound is the state it holds

153. **The clip is bars the rig held the snapshot's state through** (you heard a clip with what
     came before the snapshot in it). It used to be the last four whole bars before SNAPSHOT was
     pressed, whatever had happened in them. Now a bar counts only if nothing a snapshot holds
     changed in it, no lane or effect was muted or soloed in it, nothing was still gliding, and a
     bar has passed since any of that (for the tails of what went before).
154. **Held for four bars already, its sound is there at once. Otherwise it waits for them.** The
     snapshot itself is taken the moment the key is pressed, as before; its card fills in a few
     bars later, and ONLINE sends the snapshot at once and its sound when it has come. A knob set
     off on a 64-bar glide is waited for: the sound kept is the rig once it has arrived.
155. **If the rig moves on first, it keeps what was held** (one to three bars), **and if nothing
     was, the bars it was taken over**, marked in its profile (`"steady": false`). Mine: a clip of
     the moment is worth more than none.
156. **Tempo, GLIDE, the master's knobs and the snapshot knobs are not changes** for this (what a
     snapshot knob moves is). Notes from outside cannot be seen to change: if the One starts
     playing something else on a lane, the bars still count as held.

## Fifteenth build (Nov 11): a recall crosses over what cannot travel

157. **What cannot glide is crossed over, over the GLIDE time** (required, your word; "effects,
     modules, models"). Each stage of a lane (its instrument, then its three inserts) can hold one
     that is leaving beside the one that has taken its place. Both are given the same input, and
     the stage moves from the old one's sound to the new one's. Only what differs runs double.
158. **The fade sets off with the knobs**: on the next beat while the clock runs, over the GLIDE
     time. With GLIDE OFF it takes a fiftieth of a second, so a recall no longer clicks.
159. **An effect is crossed when anything of it that cannot travel differs**: another effect, none,
     other files, another CONFIG, a menu or switch of its own. Its knobs alone differing, it stays,
     and they travel. (Before, an insert's knobs jumped at a recall.)
160. **An instrument is crossed when its model, anything else in CONFIG, or a menu among its
     twelve knobs differs.** The one playing is left exactly as it is; a copy of it is made, put in
     its place, and changed. Knobs that travel move both, so the two differ only in what could
     not travel. Mine: it means a sustained note carries through on the old one and is not cut.
161. **A straight-line crossing, not an equal-power one.** Mine. The two sides mostly carry the
     same dry signal, which an equal-power curve would lift by 3 dB half way. The cost: tails that
     have nothing in common dip a little half way.
162. **A lane's instrument is now a part that can be replaced while it plays** (the processor and
     what each of the lane's twelve knobs moves in it). This is what a slot taking different
     modules needs; a fork is today always a copy of the same module.
163. **Costs.** Forking a lane takes about 4 ms of the message thread here (42 ms for all eleven);
     the audio thread is not held up. A lane that crosses runs both instruments, or both effects,
     until the crossing is over. A second recall while one is crossing lets the earlier leaving
     one go at once.
164. **Not built:** a snapshot knob crossing between effects or models; a model changed by hand in
     CONFIG being crossed rather than switched; the returns' CONFIG; anything on the screen to
     show a crossing; lining up the distortion's five samples of delay against the effect it
     crosses with.
165. **The cue check's two rigs share one clock.** Two engines in one process share the suite's
     bus, whose instruments judge a kick report's age by the wall clock; with a pause in the
     render they could differ by that alone (up to -56 dB), which is what made that check fail
     now and then (142 found one cause; this is the other). For that check the clock is the time
     rendered. One engine, as the app has, is not touched by any of this.

## Sixteenth build (Nov 11): from the first night on the E16 and the ONE

166. **A knob's first turn no longer jumps** (you: the E16 should start where the rig is). I could
     not see from here why the values sent when the E16 was plugged in did not all hold, so both
     likely causes are dealt with: a page that comes up (or a scene that loads) is told its
     sixteen values again, and the two hundred a newly plugged-in E16 is told go two dozen at a
     time, not in one burst. The snapshot page now says when it comes up too (12).
167. **GLIDE is a press on knob 1, on every lane's page** (you: it worked that way in the suite's
     scene, as SLOW). One address for all of them. Knob 1 keeps its own ring for it, so the
     lane's first knob no longer shows a travelling ring. The snapshot page has no press to
     spare; the library scene has GLIDE as knob 16 of FIND. *proposed*, the last two.
168. **Static rides on the sound** (you: the lead and the voice hissed on when stopped, and the
     lead chirped like birds). The lead's RADIO, and the VOICE's RADIO model, added their hiss
     whether or not anything was playing, and the radio's dropouts squealed on their own. In Live
     a silent track's devices are put to sleep, which hid it. Now the hiss follows the level of
     what is coming in, and a dropout needs a signal to drop: a lane that is not playing is
     silent, and after a stop the static dies away with the echoes. NOISE, DROPOUT and SQUELCH do
     what they did while the lane plays. The change is in SF-000's copy of the suite
     (`suite/PORTING.md`); the suite's own repo is untouched.

## Seventeenth build (Nov 11): a lit skin, first pass, from the 3D model

169. **The screen is drawn as the instrument in your model** (you: a quick first pass at
     reskinning from the model, surfaces that look real, light with a little shadow). It starts
     lit; **L** gives the flat drawing back. The flat drawing is kept exactly as it was (checked
     pixel for pixel against the build before), so if the skin is slow or wrong on the Mac the
     night is not lost.
170. **Nothing moved.** The model's module is 49.72 by 63.09 mm and the screen's lane panel is
     272 by 344 units: 5.46 units to the millimetre both ways, and the model's knob grid, display,
     three keys and the six-by-two, four-and-a-wide, one-long pockets fall on the screen's own
     places. So the skin changes how things are drawn and not where they are; every hit, drag and
     check is as it was.
171. **How it is made.** *Mine.* Not rendered from the model file. Each surface is described in
     `Source/Skin.cpp` as heights, colours and finishes with the model's measurements, and lit by a
     small renderer in the app: one light (upper left and a little behind, 64 degrees up), the
     shade things cast, dimness at the foot of what stands up, a highlight on rounded edges, a
     grain on the plates and the case. The eye is 6 degrees in front of straight above, so a cap
     shows its front. Each part is lit once into a picture at the device's own pixels and the
     screen is put together from the pictures. Why not render the model: it would be one fixed
     picture per state made outside the app; this way the layout stays in the code, a new module
     is a few lines, and your model's next revision is a change of numbers.
172. **Where it leaves the model, on purpose.** *proposed*, each a line or two to change:
     the ring's lights keep the screen's colours (white, red, blue, teal; green while travelling)
     where the model's are all green, because the colours carry meaning while playing; the ring
     is turned half a segment so a gap, not a light, is at six o'clock where a value starts;
     C, M and S keep their own colours (white, and putty until lit amber or green) where the
     model's three are amber, and stay 22 units square (the model's are 16.7) so their letters
     read; the lane's display is 126 units wide (the model's is 107) so CLOSED HAT fits.
173. **The active lane's display is the lit one.** *proposed.* In the flat drawing the active
     lane's name was black and the rest white. The model gives every module a dark display, so
     lit it is the other way about: dark for all, pale and lit for the active one. In the
     library the black name is still the base scene.
174. **A ring shows more than sixteen places.** *Mine.* Whole segments are lit whole and the one
     the value stands in is as bright as the value is far into it, as the E16's rings do. A knob
     with a middle has its middle printed on the plate above it.
175. **What stands near a part-paint is drawn again with it.** *Mine.* Lit, a thing's picture
     reaches past its own edge (its shade, its glow), so a part of the screen drawn by itself
     takes in what is within 12 units of it; the check that a part-paint gives the same picture
     as a whole one runs for both drawings. A control is now recorded as drawn only when its own
     body was in the paint.
176. **Cost.** Measured here in software at twice the size: the whole screen 82 to 89 ms lit
     against 76 to 80 flat, the meters 3.4 ms against 1.6, one knob 0.4 against 0.3. The first
     paint makes the pictures (0.75 s here); CONFIG's plate and the other pages' cards are made
     just after, so the first opening does not wait. On the Mac the numbers will be different
     (its drawing is not this machine's): `./build.sh perf` prints both.

## Eighteenth build (Nov 11): the dark instrument

177. **D is a dark instrument** (you: a dark version that can be toggled). The same shapes, the
     same light, other materials: a graphite case (`#24272A`), dark plates (`#3B3C3A`, the master
     and CONFIG `#303231`), dark keys, print in a light putty (`#D8D3C8`). The colours that
     carry meaning are untouched: the rings' lights, the lit keys, the yellow keys, red PANIC.
     *Mine*, the colours: tell me the ones your dark materials should be.
178. **Pale displays turn over.** *Mine.* The meters, the step pages, the readouts and the small
     notes are dark with light marks, and a step key is a dark cap with light bars, so the dark
     instrument is dark all over and not dark with white windows. Two things stay bright: the
     active lane's display, and the base scene's name in the library (the lit name there; the
     library's own line of help says so).
179. **It is the lit skin's.** *proposed.* The flat drawing has no dark form and stays exactly as
     it was; from it, D goes to the lit dark instrument.
180. **The look is kept.** *Mine.* L and D write `look.json` beside the session and the app opens
     as it was left.
181. **Shine comes off the shape, not its grain.** *Mine.* A surface's grain now only roughens how
     the light falls on it. On a dark surface the grain's own glints were most of what one saw.
     Both instruments are cleaner for it.
182. **The spec's display mode for a dark stage** is not this, though this is a start: nothing is
     dimmed, enlarged or simplified for distance. *proposed:* say if the dark instrument is enough.

## Nineteenth build (Nov 12): a sequencer's addresses, INFO, PHONES, and the E16's library knobs

183. **What the ONE could already reach** (you asked): a lane's twelve knobs, its sends and EQ
     (the lane's channel, CC 12 to 28), the snapshot knobs' turn and press and GLIDE (channel 16).
     Any port, as notes are. Not the inserts, the returns or the master, and no way to recall a
     named snapshot.
184. **RECALL: a snapshot knob's snapshots are the bucket** (you: put snapshots in a bucket, then
     send CC to set the glide and to set the snapshot from the bucket). CC 80 to 95 on channel 16,
     one for each snapshot knob; the value is which of its snapshots, 1 the first. *Mine:* the
     bucket is the snapshot knob's own list, because the MACRO designer already fills and orders
     it; the value is the number itself and not a share of 0 to 127, so a step's value does not
     depend on how many the knob holds; only a new value acts, because a sequencer's lane says its
     value on every step; 0 is none. A GLIDE that arrives with it is taken first. It is acted on
     as it arrives, not at the forty-a-second look at the rest.
185. **More addresses** *Mine:* an insert's eight places by slot (CC 32 to 55 on the lane's
     channel, whatever effect is in the slot), a lane's MUTE, SOLO and sequencer mute (56 to 58),
     the returns' knobs and the master's (channel 16, 60 to 70). So that a knob the screen shows
     is a knob a sequencer can turn. Nothing is sent back for them and the E16's scenes are
     unchanged. Still not on MIDI: tempo, key, the generator's knobs, an insert's or return's C M S.
186. **INFO is I** (you: an info toggle, the MIDI channel info, and what a control does when you
     hover). With it on, a control's CC is on the control, a panel's channel is a teal plate in
     its header, and the top bar says the rest. The mouse resting on a control brings a box under
     it: where it is, what it does, its channel and number, and what its values mean (a menu's
     choices each with its value, a bipolar knob's middle, a switch's threshold). *Mine:* the
     words are the suite manual's, cut to what the knob does in SF-000 (`Source/Help.h`); a check
     holds every lane knob and effect place to having a line. CONFIG's own controls do not answer
     yet.
187. **PHONES is H, and a key beside CUE** (you: on headphones with one output, cue to the right
     and PA to the left, only while a cue plays). While a cue plays or fades, output 1 is the set
     (both sides summed) and output 2 the cue (both sides summed); otherwise the outputs are the
     set, untouched. It comes and goes over 10 ms. *Mine:* only outputs 1 and 2 change (a
     recording, the meters and a snapshot's bars stay the set); the AUDIO key turns green and
     reads PHONES while it is on; and **it is not kept between runs**, because left on at a PA it
     would put the set in one speaker during every cue. *proposed*, that last.
188. **The E16's library knobs step, and stay put** (you: the knob kept changing its number, like
     it was fighting me; they should scroll through the snapshots, play the one they are on in
     cue, not move unless turned, and apply on a press). The cause: a knob's number was the place
     (128 values shared out among what it held), and when the hand paused SF-000 put the number
     back to the middle of its share. A slow turn, a click at a time, was undone at every pause.
     Now a turn is counted in clicks from wherever the knob is, four to the next snapshot; the
     ring is still put where that snapshot stands, but that no longer counts as a turn. What a
     knob holds is worked out when a hand comes to it and stays put while the hand is there (this
     was so for FITS NOW, MORE and LESS; now for the map and RECENT too). A check turns one a
     click at a time with the ring put back between clicks.
189. **I read "bucket/snapshot knobs" as the library scene's knobs** (MAP and FIND), which is
     where snapshots are previewed. The main scene's sixteen snapshot knobs are unchanged: a turn
     pushes the rig toward their snapshots, a press recalls the nearest. *proposed:* say if you
     want those to browse and cue as well; the two ways cannot share one knob.

190. **What a sequencer turns, the E16 shows** (you asked). A lane knob or a snapshot knob turned
     by CC from another port is sent on to the E16, so its knob's number and ring follow; this
     was already so. A GLIDE set from another port was not sent on (it was taken to be the E16's
     own): now it is. The addresses that are on no E16 page (inserts, returns, master, mutes)
     have nothing there to update.

## Twentieth build (Nov 12): the 3D stage

You saw the three.js study, said "that's amazing, I want that, let's build it", that it must be
world class, lag free, efficient and stable, that every button must work (mute, solo, the
sequencer), and that if in the app is best for performance and reliability, to do that.

191. **In the app, drawn by the app** (OpenGL on a thread of its own), not a web view. A web view
     would be a second interface in JavaScript, kept in step with the first over a bridge, with
     every display drawn twice; and it runs in another process the app cannot answer for. Native,
     there is one interface, no bridge, and the drawing thread reads the engine directly.
192. **The 3D stage is a third look for the same screen** (flat, lit, 3D), not a new screen. The
     lit skin already drew the screen as a set of solid things (panel, knob, key, lamp, window);
     in 3D those calls write the things down, and the stage builds them. Everything flat goes
     into one picture laid over their tops. So every page and every control came across at once,
     and there is one place that lays out and one that takes the hand. A check reaches all 341
     controls of the main screen through the eye, from above and from the player's view; from the
     corner 318, where a cap hides what is behind it, and every knob.
193. **OpenGL 3.2, and what that risks.** It is what JUCE gives on both the Mac and the machine
     this is built on, where a software driver runs the checks and draws the pictures. On the Mac
     it has been "deprecated" since 2018 and still works; if Apple removes it, the stage's drawing
     (one file) is rewritten for Metal and nothing else changes. *Mine.* I cannot run a Mac from
     here: see Not verified.
194. **If it cannot start, the lit skin stays** and says why on the bar and in `3d.log`. A shader
     the driver refuses, a buffer it will not make, no OpenGL at all: each is a sentence, not a
     black window. `SF000_3D_FAIL=1` shows it. *Mine.*
     **And if it starts and draws nothing, it notices.** The first picture with the instrument in
     it is read back at 63 points, before the glow (are they numbers?) and as finished (is it one
     flat colour?). Blank twice running and the stage gives up, writes what it found, and the lit
     skin comes back. This is the failure the study had on your Mac, which no software driver
     shows; the shaders are written so that it cannot happen (205), and this is for the case
     where I am wrong about that. A real picture's darkest and brightest points are 200 or more
     apart of 255, in every view, pale and dark; under 6 counts as blank. `SF000_3D_FAIL=blank`
     shows it. *Mine.*
195. **It opens in 3D** (you: "I want that"). **3** goes to the lit skin and back; **L** still
     goes to the flat drawing (and leaves 3D). *proposed.*
196. **Three views, and the view is locked until K** (you: "position it and lock it in a preferred
     position"). TOP, PLAYER (how it starts) and HERO, as in the study, each fitted to the window.
     Locked is the default because a live instrument must not turn when a hand misses a knob.
     Unlocked: drag off the controls to turn, shift- or right-drag to slide, wheel or pinch to come
     nearer. *proposed:* the keys (V, K, M); there is nothing on the screen for them but a line of
     words that comes up when one is pressed.
197. **Following, and what must not move the eye** (you: MIDI follow moves "worked amazingly";
     "watch out that midi sequencer pulses don't trigger the follow moves"). The eye goes to a
     knob's module when the knob is turned on the E16, and out to hold everything when three or
     more knobs move together with no hand on them (a snapshot). It never moves for notes, the
     clock, the tempo, keys (mute, solo, THROW, FREEZE), or a knob moved from any port but the
     E16's: that is a sequencer playing it, and it stays the sequencer's until the knob has come
     to rest. For this the MIDI code now notes which port moved each control. *proposed:* a
     snapshot recalled by a sequencer (RECALL) does fly out, since it is many knobs travelling
     and not a pulse; say if an arrangement should leave the view alone entirely.
198. **Tags** (you: "a popup hud should appear by the knob showing its value"). For a knob in the
     hand on the screen, or under a hand on the E16; the three moved last. They show the engine's
     own reading (the study could only show 0 to 127). Not for a sequencer's moves, and not one
     per knob of a snapshot. A hand on the screen does not move the eye: the hand is already there.
199. **A knob's lights are read from the engine on the drawing thread**, every frame, not handed
     over by the screen thirty times a second. They move with the sound even if the rest of the
     app is busy for a moment. *Mine.*
200. **A dimmer underglow** (you: "a little dimmer"). The glow under the case and the warm light
     up its sides are well under the study's.
201. **Efficiency.** Nothing is drawn twice that has not changed: the shadows only when something
     that casts one moves; the sheet only where it differs from what was last sent (compared in
     64-pixel squares); and with nothing moving a frame is the last picture shown again. On the
     Mac the display asks for each frame, so the eye and the lights move at its rate (120 a
     second on yours). Here, in software: recording the screen 0.03 ms, printing a knob's part of
     the sheet 0.1 ms, all of it 25 to 40 ms.
202. **The sheet is three pixels to the unit on a display like yours** (two on a plain one), so
     that the print is clean when the eye is close to one module. It costs 70 MB twice over.
     *Mine.* `SF000_SHEET=2` halves that if it is ever wanted.
203. **Empty places are dimples**, the plate's own colour in shade, where the study had black
     discs. *proposed.*
204. **A name is typed in a small window over the stage**, since nothing of the screen's own can
     be drawn over OpenGL's picture.
205. **The study page is not in this repo.** It was a way to find out; the app's stage is written
     from its findings, in C++, with none of its code. One thing it found the hard way is kept:
     a colour that is not a number, from a power of a value read just off a triangle's edge, was
     spread by the glow over the whole picture and turned it black on your Mac and not on the
     software driver. The stage's shaders clamp every colour they write.
206. **The checks name the window manager's properties before they start** (Linux only, where
     the checks run; nothing on the Mac). One stage test in six stopped dead where the typing
     window opens. A debugger on the stopped test showed why: the X server the checks run on has
     no window manager, JUCE asked it about a property it had never heard of, and its error
     arrived on the drawing thread while the screen's thread held Xlib's lock, so each waited for
     the other. With the names said first the server raises no such error (25 a run before, none
     after), and the test has run seventeen times in a row since. The Mac has no X server and no
     Xlib: this was the test bench, not the instrument.

## Twenty-first build (Nov 12): an outside audit

You sent an audit of 006242a made with ChatGPT ("assess if any changes need to be made"). I
checked each finding against the source before believing it. It was good work: twelve of
fourteen hold, and four of those were mine from this week. What follows is finding by
finding. Numbers in brackets are the audit's.

207. **[1] The clock switched on with the rig running sent every tick since bar 1 at once.**
     True: SEND pressed two minutes in, a sequencer plugged in mid-set, or the audio starting
     again, and the follower got hundreds of ticks in one burst. It now takes up where the rig
     is. *proposed:* the follower is sent no START then, so it joins at the next RUN; the other
     way would be a START on the next bar line. Say if you want that.
208. **[2] A tempo jump could put a tick before the one already sent.** True, and small (3 ms,
     two ticks). The ticks of a block now lie evenly across that block, and a jump is made up
     at twice the tempo at most.
209. **[3] MIDI keys and GLIDE waited for the screen's thread** (25 ms, more if it was busy), and
     a key pressed and let go between two of its visits was never heard. True. MUTE, SOLO, a
     lane's sequencer muted and a hold (THROW, FREEZE) now act from the MIDI thread, as lane
     knobs always did, on the next block of sound; GLIDE too, so a knob sent straight after it
     travels by it. Everything else still waits for that thread, but in the order it came with
     nothing dropped. *Not done, on purpose:* the audit's full answer is a timestamped command
     queue read by the audio thread. That is sample-exact where this is block-exact (2.7 ms at
     128), and a larger change than the fault called for. Snapshot knobs and recalls stay on
     the screen's thread: they touch the whole session.
210. **[4] Quitting with an upload stalled waited 8 s, then killed the thread by force.** True,
     and it can take the app down as it leaves. A request under way is now called off; the
     sender is gone in a few ms. Checked here against a library that takes the call and never
     answers, both through SF-000's own socket and through JUCE's client. Not checked: the
     Mac's own client (NSURLSession) being called off, which is the path https takes.
211. **[5] A recall that crosses a voice over read its sample from the disk again** (a third of
     a second for a minute of sound, with the screen stopped), and lost the sample if the disk
     had gone. True. A sample already read and still held is now shared: 0.004 ms. *Not done:*
     a snapshot's profile is still worked out on the screen's thread (30 to 60 ms); moving it to
     a worker is right and is not small.
212. **[6] One allocation on the audio thread at the first voice crossfade.** True. Reserved.
213. **[7] An insert swap publishes its state in several steps.** True as a matter of form: a
     tool for finding races would flag it. I can find no way for it to do more than fade one
     block of one swap wrongly, in a window a few millionths of a second wide. *No change.* It
     wants a sanitizer run on the Mac before anyone rewrites the handoff.
214. **[8] A recording under way when the sample rate changes** got up to a second of sound at
     the new rate in a file marked with the old. True, rare. The engine lets go of the recording
     the moment the audio starts again at another rate.
215. **[9] At 96 kHz below 94 bpm a snapshot's four bars never came.** True: the tape held 20 s
     at 48 kHz and half that at 96. It is now sized by the rate (four bars at 60, the slowest),
     and takes as many bars as it holds if an outside clock is slower still. 48 kHz is as it was.
216. **[10] A replaced sample stays in memory until the next is dropped on that lane.** True and
     bounded: one sample per lane at most, and only for samples swapped by hand. *No change.*
217. **[11] The 3D stage rebuilt and re-sent every shape at every refresh.** True, and against
     what I told you in 201. Unchanged parts are now neither handed over, built nor sent; a
     knob travelling changes no part and prints nothing (a snapshot gliding moved a hundred
     patches of the sheet thirty times a second for nothing). *Not done:* the whole sheet is
     still printed again for a tempo drag, a snapshot knob under the mouse and CONFIG. That is
     a matter of naming the right patch at each place; I would rather see it on your Mac first.
218. **[12] If macOS refuses an OpenGL context, JUCE walks into it before SF-000 can answer.**
     True in JUCE's source; I cannot make it happen and it is very unlikely on your Mac today.
     Three things for the day it does: a start that never finished is not tried again at the
     next launch (a note, `3d-starting`, is left while the stage comes up, and found if the app
     died meanwhile: 3D is left off, the bar says so, **3** tries again); a stage that is made
     and never draws is given six seconds in front of you, then the lit skin; and a failed
     attachment is always cleared before another try. *Not done:* asking macOS beforehand
     whether it will give a context. That is Mac-only code I cannot compile here, and a build
     that breaks on your Mac is worse than the fault it guards against.
219. **[13] The ALLOC analysis ran on all eleven lanes with no ALLOC to read it.** True. It rests
     unless an SF ALLOC is on the bus (in Live beside it, say). About two points of the engine's
     39 here, which is near what the measurement itself wanders by. This is in the suite's own
     code (`suite/core/AllocTap.h`, marked): the other chat should take it too. *Not done:* the
     pocket's scan of kick reports, which is in the sound's inner loop; not without ears.
220. **[14] A cue clip played at the wrong pitch if the interface's rate had changed since it
     was taken.** True, rare. It plays at its own speed.
221. **Not changed, and why.** *Memory:* the audit puts the 3D stage at about 750 MB at full
     screen on your display, most of it the smoothed picture. That is its sum of the formats, not
     a measurement, and it is right. On your machine it is affordable; two-times smoothing in
     place of four would save some 180 MB and I cannot judge the look from here. *MIDI times:*
     notes are stamped when they reach SF-000, not when the Mac received them. The difference
     is under a millisecond as a rule and nobody has measured it here. Both want your Mac.

## Twenty-second build (Nov 12): MOTION, CONFIG in boxes, LOAD, SETUP, the icon

You asked what had happened to the motion system, then for: two motions in each instrument's
CONFIG (waveform, amount, speed); tooltips on CONFIG's controls and logical groups with labels
and boxes; a load button for anything that takes a file, usable full screen while playing; a way
out of full screen; AUDIO renamed SETUP, with knobs there for the underglow's colour and
brightness; and a new icon from a 4 x 4 module, then "use dark instrument for icon".

222. **What had happened to MOTION: nothing was decided, it fell out.** The suite's two modulators
     came across inside every module and ran, switched off, with no control anywhere: CONFIG
     listed only a module's own parameters, and MOTION is the suite's. Mine to have flagged.
223. **MOTION is the suite's own, all of it, in each lane's CONFIG**: WAVE, SPEED, TARGET, AMOUNT,
     twice. The names are yours (the suite says SOURCE, RATE, DEPTH). *proposed:* TARGET lists the
     lane's twelve knobs first, in the panel's order and by the panel's names, then the rest of
     the instrument's. *proposed:* instruments only, as you said; an effect's CONFIG has none.
224. **MOTION is kept in snapshots**, and a snapshot from before it could be set means it off:
     that is what it sounded like. A recall that changes a lane's MOTION crosses the voice over,
     as any change of CONFIG does.
225. **Not built: seeing it on the main panel.** A knob being moved by MOTION shows nothing on
     its ring yet; you hear it. A mark on the ring for where the sound is now is the next thing
     I would add. *proposed.*
226. **CONFIG's boxes**: SOUND, LAYER, SAMPLE, WAVETABLE, FM 4-OP, POCKET, ECHO, RIG, MOTION 1,
     MOTION 2 (EFFECT on an insert or a return). The groups are the suite's own where it has them
     and the control's name where it does not. A small box shares a row with others; MOTION's two
     have rows of their own, last. *proposed.* (You, on seeing it: the box's line must not run
     under its label, and the boxes want more room between them. The line now stops either side
     of the label; boxes stand 8 apart side by side and 20 one above another.)
227. **A model's own controls show only while that model plays** (FM 4-OP's thirteen, WAVETABLE's
     table, a sample's four where SAMPLE is one model among others). The lead's CONFIG was 38
     controls in seven rows; as an acid line it is 18 and MOTION, with nothing to scroll.
     VOICE's phrase controls always show: every model of it plays the phrase. *proposed.*
228. **ALLOC is gone from CONFIG.** The rig has no ALLOC; the switch did nothing. *Mine.*
229. **Every CONFIG control answers INFO**: 548 of them across the instruments, effects and
     returns, held to it by a check. The words follow the suite's manual, cut to a line or two.
230. **LOAD is a list in CONFIG's own panel**, not the system's file window: folders, then the
     sound files it can read, a click to go in or to load. So it works full screen, and in 3D.
     It remembers where a lane's file and an effect's last came from. Loading is what dropping
     was: the file is read on the screen's thread, and the sound carries on. *Mine:* no preview
     of a file before it is loaded, and no typing of a path.
231. **Full screen could be entered and never left.** JUCE's kiosk mode, on the Mac with the
     system's title bar, asks for full screen on the way out as on the way in. F now uses the
     window's own full screen, which goes both ways; Esc only ever leaves it; SETUP has a key for
     it too. (Until you build this: ctrl-cmd-F, or the green button at the top of the screen.)
232. **AUDIO is SETUP**, and holds what is set once: the interface, MIDI and the clock as before,
     FULL SCREEN, and two knobs for the glow under the case, its colour and its brightness
     (you had asked where the dimmer was). As it comes, the glow is exactly what it was.
233. **The icon is the instrument's tag** (you: first a 4 x 4 module, then the dark instrument's,
     then "based off the SF-000 label tag instead, something very utilitarian"). White letters in
     the tag's own face on the tag's black, one thin line inside the edge, nothing else.
     *proposed:* SF over 000, stacked, because SF-000 on one line cannot be read at the size of
     a Dock icon; `python3 tools/icon.py assets/icon.png one` makes the single line if you want it.
234. **INFO lists the keyboard's keys** (you: "have info toggle show key command menu when
     activated"). At the foot of the screen, left; it steps to the right while the pointer is on
     it, so that nothing under it is out of INFO's reach. On the 3D stage it is a tag at the foot
     of the window. *proposed:* that it stays up for as long as INFO is on.
235. **PULSE, the underglow's third knob** (you: "from none to intense", "driven by kick"). The
     drawing thread follows the kick lane's own meter: full on a hit, gone 0.3 s later. At none
     the glow is steady and the picture is, bit for bit, what it was. At intense it falls to a
     quarter between hits and rises to three times on one. While it pulses the whole scene is
     drawn again for every frame that the pulse has moved in; with PULSE at none, never for this.
     *Mine:* it follows what is heard, so a muted kick does not pulse.
236. **MOTION is in the effects' CONFIG too** (you: "can we add the motion system to the configs
     of the effects as well"): every insert and both returns, the same two rows, kept in
     snapshots the same way. TARGET lists the effect's own panel first. Among its waves INPUT,
     the level coming in, stands where an instrument has NOTES. This replaces "instruments only"
     in 223.
237. **What a snapshot holds of MOTION** (you: "make sure the snapshots capture the motion
     states"). It did hold the settings, everywhere MOTION can be set: a lane's, each insert's,
     both returns', written to the session file by name like any CONFIG value. A check now says
     so. One thing it did not hold: RANDOM, DRIFT and STEPS were drawn from numbers made afresh
     for every voice and every launch, so a snapshot's random wave came back as a different one.
     Each place in the rig (a lane, an insert's slot, a return) now has its own numbers, always
     the same: the wave is the same wave after a recall, a crossing-over, a restart. *Mine:* so
     two snapshots cannot have different STEPS patterns on the same lane.
238. **A recall of part of a snapshot, from the library** (you: "recalling a snapshot recalls
     everything; but in library, when a snapshot is cued, you can deselect components you don't
     want to recall... the next time you recall, it defaults again to all selected"). With CUE
     lit and a snapshot cued or chosen, a row says what RECALL takes: lanes 1 to 11, RET (the
     returns) and KEY. A part put out stays exactly as it is now: its knobs, its steps, its
     inserts, its CONFIG. After the recall, and for another snapshot, all are lit again. Every
     other way of recalling takes everything, as before. *Mine:* the thirteenth key is KEY and
     not "master", because the tempo, GLIDE and the master's three knobs were never a
     snapshot's; a lane left out under a turned snapshot knob stays where the knob has it.
     **Not built, as you said, for later:** making a new snapshot from one with parts put out.
239. **A worn black anodized finish on the dark instrument's faceplates, and powder coat on its
     case** (you: "subtle but realistic slightly worn", then a picture of scratched black metal
     as a rough guide, and "same could be done for the case, which is powder coated"). Worked
     out in the surface's shader, with no pictures loaded. The plates: a fine blasted grain, a
     slow unevenness in the dye, a pit here and there, hair-thin scratches (most up and down or
     across, each its own length), and the anodizing rubbed toward the metal in patches along
     the edges. The case: an orange-peel that breaks up what shines in it, a fine speckle, a few
     scuffs, its upper edge rubbed. The print sits on top, as sharp as it was. Much slighter
     than your picture: that one is a plate at the end of its life. *Mine:* the pale instrument
     is as it was; and how much of each there is, which is a number apiece.
240. **The scratches are your photograph's now** (you: "the scratches are a little unrealistic.
     can we make them more like the one in the photo", then "take the photo, make a square
     version of it, and then rotate 90 degrees to create four variations that get used randomly
     for modules so it looks less uniform"). The ones I had worked out in the shader are gone.
     `tools/wear.py` makes `assets/wear.png` from the photograph: its black taken away, its own
     worn border cut off, made to repeat without a seam, enlarged, and each scratch smoothed
     along its length and thinned across it so that it is still a hairline with the eye close.
     Every plate takes its own piece of that sheet, turned one of four ways. *Mine, past what
     you asked:* as often as not the piece is mirrored too, and it is a different piece for
     every plate, not one of four, so no two modules match at all; the piece is the same scale
     on every plate, so a scratch is as fine on the sequencer's long strip as on a lane (a
     square stretched to fit each plate would have made them coarse on one and fine on the
     other); a plate keeps its piece when CONFIG slides the rows over; most of what is on the
     sheet only just shows and a few scratches are deep, more in some stretches of a plate than
     others; the edges' wear is specks that thin out as they come inward, where it was soft
     patches; and the case's few scuffs come from the same sheet, larger and fainter. The
     photograph itself is not in the repository, only the sheet made from it: if it is not
     yours to use, that sheet is what to replace (any picture of scratches on black, through
     the same tool). It costs the card about 13 MB and is cheaper to draw than what it replaces.

## Twenty-third build (Nov 12): every knob without the mouse; the E16 on the page of what is in front

241. **What is in front** (you: "whenever I make a module or an effect (or a config page) active,
     the e16 would switch to that module's page or scene"). There was an active lane and nothing
     else. Now one module is in front: a lane (it is the active lane), one of its three inserts,
     the returns, the master, the sequencer strip, the snapshot knobs. Its name lights up as the
     active lane's does. *Mine:* a press anywhere on a module's plate off its controls brings it
     to the front (before, only a lane's name did anything); an insert in front is the active
     lane's, so the lane's name stays lit too.
242. **The E16 is turned from here** (you asked whether it could be; the E16's manual for OS
     1.3.0 says it takes Bank Select for the scene and Program Change for the page, on its PC
     Receive Channel). SF-000 sends them on channel 12 whenever what is in front changes here.
     This undoes 69 ("the screen cannot turn the E16's page"). *Mine:* it is never sent because of
     a page the E16 turned to itself (that module comes to the front here, and the E16 is already
     there), and nothing is sent at start-up: only a change made here turns it. The scene is said
     with every page, so that the E16's PC Mode does not matter (`e16_bank_always` in `midi.json`
     says it only when it changes). (That was the fault of 262: the scene is now said only to
     change it, and PC Mode matters.) The scene is controller 32 with the slot less one, the page
     the Program Change with the page less one: the manual gives the first by way of Ableton's
     "Sub" and does not give the counting. The slots are SETUP's (E16 PAGES), 1 and 2 to begin.
243. **Pages for the effects** (you: "I'd also create them for the effects"). The first scene's
     twelve pages are full, so they are in the second, which was the library's (SF-LIB) and is
     now SF-FX: MAP, FIND, INS 1 to 3, RETURNS, MASTER, SEQ, CFG 1 to 3. *Mine:* an insert's page is
     the **active lane's** insert (new addresses on channel 13 that mean "the active lane's"),
     not a page for each of thirty-three; a key among an insert's places (THROW, FREEZE) is its
     knob's press; SF-000 sends the script the names of what a page holds (an insert's places,
     CONFIG's controls) and the E16 shows them, where the firmware can.
244. **SEQ, and the generator's knobs get addresses** *Mine*, for your "every parameter that's a
     knob": LENGTH and HITS, ROTATE, ACCENTS, SEED had none. They are the active lane's, on
     channel 13.
245. **CONFIG on the E16** (you: "or a config page"). Three pages, twelve controls each, in the
     order the panel has them, named by SF-000. *Mine:* twelve and not sixteen, to keep the bottom
     row the same on every page of the scene; a list takes a share of the knob's travel for each
     choice and a switch the upper half, and a knob is left where the hand put it (188's lesson: a
     number put back to the middle of its share at every pause fights a slow turn); controls past
     the thirty-sixth, and keys that are held, are not on it.
246. **The library** (you: "as well as the library as you suggested"). Opened on the screen, the
     E16 goes to the map; MACROS, to the snapshot knobs' page.
247. **Knob 13 is the pointer's wheel** (you: "one of the knobs on every page act as a scroll wheel
     to activate modules or effects when pressed (similar to how the glide adjust works, knob just
     has to be consistent)"). *Mine, all of the how.* (The order, what chooses and how it ends
     are yours since 255 to 257; what is left of this list is knob 13, who keeps the wheel, the
     three clicks and the held press.)
     - knob 13, the bottom left corner, which on a lane's page is SEND A;
     - **SF-000 keeps the wheel, not the E16.** GLIDE's press swaps the knob on the E16, page by
       page. Done that way the wheel would have stopped being the wheel the moment the page
       turned under it, and left knob 13 swapped on every page it was used from. Instead the
       press is a message; while the wheel is in use SF-000 takes knob 13's turns, whatever page
       sends them, as the pointer's, and puts the knob's own value back afterwards. SEND A is
       never moved by it;
     - three clicks to the next module, counted from wherever the knob is (188 again);
     - the order is the screen's, row by row;
     - what the pointer **rests on** (a third of a second) comes to the front; what it **passes
       over** does not. Otherwise the inserts, which come after the lanes, would always be the
       last lane's;
     - it ends with a second press, with any other knob turned (the hand has arrived), or after
       six seconds untouched;
     - **on the snapshot page and the library's map, where every press recalls, it is held for
       half a second**, and for knobs 13 and 14 there the recall comes as the knob is let go.
       That is the one place it is not simply "pressed"; the other way was to take the recall off
       two snapshot knobs and two squares of the map.
248. **Knob 14 pressed is OPEN** *Mine:* you did not ask for it, but CONFIG's knobs cannot be
     "accessed without a mouse" unless CONFIG can be opened without one. CONFIG for what is in
     front, and shut; the returns' two in turn; the library from the snapshot knobs, and back. On
     CONFIG's pages 15 and 16 pressed are the page before and the next.
249. **The eye goes to what comes to the front** (you: "the follow cam zoomed in on an active
     module when it's activated in the same way it does when it gets midi input"). (It stays
     there now, and is home while the wheel turns: 258.) A page turned
     to on the E16, the wheel (the eye goes along with the pointer), Tab, CONFIG opened by OPEN or
     C (it holds CONFIG and the module beside it). Home after a couple of seconds, as before; M
     turns it off. *Mine:* **not for a click of the mouse**, for the reason a knob dragged with
     the mouse never moved it: the hand is already there, and the view would jump under it and
     jump back two seconds later in the middle of whatever the mouse did next. *proposed:* say
     if you want the click to move it too; it is one condition.
250. **CONFIG keeps to what is in front** under a key or a controller: a lane or an insert brought
     to the front takes an open CONFIG with it, the returns take it to DUB's, and the master, the
     sequencer strip and the snapshot knobs shut it. Under the mouse it stays where it was put,
     as it did. *Mine.*
251. **Tab, Shift-Tab and C on the keyboard** *Mine:* the wheel's and OPEN's doing, for when the
     E16 is not there. INFO's list has them.
252. **INFO** shows each of CONFIG's controls its number while CONFIG is open, the generator's
     knobs theirs, and says which page and knob of the E16 a CONFIG control is.
253. **An E16 that goes somewhere other than where it was sent is not followed there**, and SETUP's
     E16 line says which page was asked for and which it said. (Within a third of a second of
     being sent; after that a page it says is one a hand turned to.) *Mine:* the counting, the
     slots and the E16's own settings are all things I could have wrong, and this is how it
     would show.
254. **Not done.** *Mine:* TEMPO and KEY are still not on the E16 (they are readouts, not knobs,
     and a CC's 128 steps is more than a beat a minute a click); M and S are not on it either. Say
     if you want any of them. Making a new snapshot from part of one (238) still waits.

## Twenty-fourth build (Nov 12): the wheel, after its first try on the E16

You: "works but needs some small fixes."

255. **Press, turn, press** (you: "a press enters scroll mode and a press again makes the
     selection end exits scroll mode"). The first press takes the pointer up and nothing else.
     Turning moves it. The second press chooses what it is on: that comes to the front, the E16
     turns to its page, and knob 13 is the page's own again. Resting on a module no longer
     chooses it (247's third of a second is gone), and **nothing changes while you turn**: not
     the module in front, not the active lane, not the E16's page. *Mine:* the three insert
     panels show the inserts of the lane the pointer is on (they would otherwise show the active
     lane's while the frame was on another lane's insert); OPEN pressed with the pointer up
     chooses first, then opens.
256. **The order** (you: "module, then inserts 123 if they exist, then next module, then module
     inserts 123, etc."). A lane, then those of its three inserts that have an effect in them,
     then the next lane and its inserts. An empty insert is not a stop. *Mine:* after the last
     lane come the snapshot knobs, the returns, the master and the sequencer strip (they have to
     be somewhere, and the end is where they are on the screen); the E16's display names an
     insert by its lane and place (`3.1 DELAY`); Tab and Shift-Tab go through the same list;
     three clicks to the next, two when the list is longer than thirty-six (every lane with all
     three inserts filled is forty-eight, and a knob has 128 numbers).
257. **Putting it down without choosing** *Mine:* any other knob turned (which does what it
     always does), or eight seconds untouched (it was six, and ended the wheel on a module; now
     it leaves everything as it was, so a pause to think costs nothing but two seconds more).
258. **The eye** (you: "the zoom should stay zoomed out and only zoom in once a selection has been
     made, and then it shoud stay zoomed in until anoer selection or configure is toggled").
     While the pointer is up the eye is home. A module chosen (the second press, a page turned
     to on the E16, Tab) fetches it, and it stays: no going home after two seconds. Another
     module chosen moves it there; CONFIG opened by OPEN or C widens it to hold CONFIG and the
     module, and CONFIG shut brings it back to the module. *Mine:* **Esc lets it go home** (after
     it has shut CONFIG, if CONFIG is open), and so do V, M and a drag of the view: there was no
     other way out of a held view but choosing something else. The pointer put down with nothing
     chosen sends it back to what it held. A knob of another module turned on a controller
     still takes it there for two seconds, then it is back. A click of the mouse still does not
     fetch it from home, but a module clicked to the front while it is held is what it holds
     (it would otherwise be held on a module that is no longer the one in front). The pointer's
     frame is three times as heavy, to be read from the whole rig's distance.
259. **"Sometimes the select knob gets stuck and can't go backwars" and "pressing the button takes
     it back to kick".** One cause, as far as I can tell without the E16 in front of me: an E16
     knob sends a number from 0 to 127 and stops at each end, and the wheel was working out
     where the pointer was from numbers it had *told* the knob to stand at. In the last build
     the pointer coming to rest turned the E16's page, so the knob under the hand became another
     page's knob 13, standing wherever that page had left it: at nought (a SEND A turned right
     down) it has nothing to send when turned back, and when its number was far from the one
     SF-000 believed, the next message read as a leap to the far end of the list, KICK. Three
     changes: the E16 no longer changes page while the wheel turns; the knob's clicks are counted
     from the nearer of what it last said and what it was last told, and a leap of more than
     sixteen is taken as the knob having been somewhere else, not as a turn; and when the pointer
     is taken up the knob is set to a number with clicks enough either side to reach both ends
     of the list (one number to a click, the list in the middle of the knob's travel), then left
     alone unless it is found more than six clicks from where it should be, so that a number is
     hardly ever sent to a knob while it is being turned. A made-up knob walked back and forth
     under the wheel ends where its clicks put it, whether it takes its number or never does.
     *Not verified:* this is reasoned from your two reports and the manual. If it still sticks,
     the place to look is whether the E16 takes a number sent to a knob with a hand on it
     (`controllers/e16/README.md` says what that would look like).
260. **An E16 newly here is turned to the page of what is in front** *Mine:* until it said a page
     or was sent to one, SF-000 did not know where it was, and the first thing chosen on the
     screen could leave the two apart. And **a knob turned on a page SF-000 did not know it was
     on** (the script not loaded, or not saying) puts it on that page, with nothing brought to
     the front and nothing sent to the knob under the hand.
261. **The E16's answer to being sent somewhere chooses nothing.** It says the page it arrived at,
     and that used to count as a page turned to from the controller, which under 258 would have
     fetched the eye for every click of the mouse.

## Twenty-fifth build (Nov 12): the E16 went to KICK's page whatever was chosen

262. **You:** "the zoom and select works but the e16 page goes to kick, even when something else
     is selected." Every turn was sent as a Bank Select and a Program Change together (242, "so
     that the E16's PC Mode does not matter"). What your E16 does with that, as far as I can
     tell from here: the Bank Select loads the scene it names, the one it is already in
     included; a scene that loads comes up on its first page, which is KICK's; and the Program
     Change sent with it does not survive the loading. So:
     - **within a scene the page is a Program Change by itself.** No scene is loaded. **This
       makes the E16's PC Mode matter: it must be Page** (on Scene a Program Change by itself
       loads another scene);
     - **to the other scene, Bank Select and Program Change as before, and then the page again
       by itself** when the scene's script says it is up on another page, or 0.4 and 1.2 seconds
       later if the script says nothing.
     This is very likely what 259 was as well: with the E16 on KICK's page the wheel's knob was
     KICK's SEND A, at nought, with nothing to send when turned back. **I was wrong in the
     last build's notes** to write that Bank Select with Program Change had been tried on your
     E16 and turned it as sent: I read that into "works but needs some small fixes". Both
     places are corrected.
263. **A turn is watched until the E16 answers it** *Mine:* its script says the page that is up.
     Until it says the page asked for (0.3 s within a scene, up to 2.5 s across scenes), a page
     it says is its answer and not a hand's turn: nothing on the screen follows it there (a scene
     coming up on its first page would otherwise have opened the library, and an answer that
     came after the E16 had been sent on would have brought the last module back). A wrong page
     in the right scene gets the page said again, up to three times; no answer gets it said
     twice more. An E16 that answers at once is sent nothing twice.
264. **SETUP's E16 line says what happened** when it did not come round: `ASKED FOR SF-000 PAGE 5,
     IT WENT TO SF-000 PAGE 1`, or `NO ANSWER`, or that PC Mode must be Page when a turn within a
     scene ended in the other scene. *Mine.* That line is what to send me if this is still wrong.
265. **Two settings in `midi.json`** *Mine:* `e16_page_first` (0; 1 if every turn lands one page
     short: the manual does not say how the E16 counts a Program Change) and `e16_scene_always`
     (false; true says the scene with every page as before). `e16_bank_always` is no longer read:
     every `midi.json` written so far has it set to true, which would have kept the fault.

## Twenty-sixth build (Nov 12): a knob tied to knobs

You: "Is there a way to create macros that just adjust certain knobs on certain lanes? It would
be nice, in addition to the snapshot macros, to eb able to create a macro that ties one knob to
many."

266. **There was a way, a roundabout one.** A snapshot knob moves only what differs among its
     snapshots (22), so two snapshots that differ in three knobs make a knob that moves those
     three. It costs two snapshots in the library for every such knob, and nothing says what
     the knob moves until it is built.
267. **LEARN ties a knob straight to knobs** *Mine, all of the how:*
     - **by showing it, not by filling in a table.** LEARN in the macro designer puts the knob
       at its full turn and brings the main screen up; whatever is then turned, on the screen
       or on the E16, is what the full turn does, and KEEP ties it. The other way was a list of
       targets each with a range to set, which is more to draw, more to click, and cannot be
       done from the controller;
     - **an amount for each knob, in its own direction**, measured as how far it was moved.
       Applied as every snapshot knob's push is: an offset from the base scene, times DEPTH,
       stacked with the others and held to the knob's travel. So it works from wherever the
       rig stands and after any recall;
     - **nothing changes in the sound at KEEP**: the scene under the knobs is put back as it
       was and the knob stands at full, which comes to the same. Turned back, they go back;
     - **LEARN again adds**: the knob goes to full first, so what is turned then is measured
       from its full turn. A tied knob turned back to where zero has it is let go, which is
       the only way to untie one knob short of CLEAR;
     - **one moved and moved back is not tied** (under two thousandths of its travel);
     - **the sixteen knobs are the same sixteen**: a knob holds snapshots, or ties, or both
       (they add up). No second bank, no second page on the E16;
     - **what it reaches is what a snapshot knob reaches** (a lane's twelve sound knobs, its
       sends and EQ, the returns' knobs). An insert's knobs would need the tie to know which
       effect it was made on, as 23 says of snapshots; not done.
268. **On the screen** *Mine:* the top bar says `LEARNING`, the knob's number and name, with
     KEEP and DROP beside it and a count of the knobs moved; each moved knob's ring is yellow
     until KEEP or DROP, in 3D as well, and so is the knob being taught, which stands at full
     and cannot be turned meanwhile. Return is KEEP. The designer's card says `TIED TO 5 KNOBS`
     and lists them with how far each goes (`BASS: CUTOFF +50`, hundredths of its travel).
     LEARN on the knob being taught reads KEEP.
269. **Its press** *Mine:* a snapshot knob pressed recalls its nearest snapshot and goes home
     (25). A knob with ties and no snapshots has none to land on, so it lands on the nearer end
     of its own turn: past half way what it would push at full is where those knobs now stand,
     short of it they go back, and either way the knob is at zero and can push again. *proposed:*
     say if you would sooner it did nothing.
270. **In the session file** a knob gains `links`: each tied knob's id and its amount
     (`FORMAT.md`). A file with them opened by an older build keeps them, unread, and writes
     them back. A link to a knob this build does not have is carried the same way.
271. **Not done.** *Mine:* KEEP and DROP from the E16 (the knobs can be turned there; the two
     keys are the screen's and the keyboard's); editing one tie's amount without learning it
     again; an insert's knobs; a curve other than a straight line through the knob's turn.

## Twenty-seventh build (Nov 13): the snapshot knobs in both scenes

272. **You:** "selecting the snapshot module takes me to page 12 in the fx scene, not page 12 in
     the sf scene", where page 12 was empty. The snapshot knobs are now page 12 of **both**
     scenes, the same sixteen on the same addresses, and choosing them sends the E16 to page 12
     of whichever scene it is in: a Program Change by itself, no scene loaded. *Mine:* that
     way round and not "make the scene change work", because I cannot say from here why your
     E16 stayed in SF-FX (SF-000 sends the scene before the page when it takes the E16 to be in
     the other one; either it took it to be in the first scene already, or the E16 did not act
     on the Bank Select), and this is right in both cases. A snapshot knob turned no longer says
     which scene the E16 is in. Turned to by hand in either scene, page 12 brings the snapshot
     knobs to the front. **SF-FX has to be loaded onto the E16 again** (SF-000 is unchanged).
     *Not verified:* if going from an SF-FX page to a lane ever lands on an SF-FX page too, it is
     the same fault and SETUP's E16 line will say what was asked for and where it went.
273. **The snapshot page has GLIDE and the wheel on their usual presses** (you: "the snapshot pge
     needs the lane select, and glide press knobs as well"). Knob 1 pressed swaps to GLIDE (its
     ring green) and knob 13 pressed is the pointer's wheel, at once, as on a lane's page. Those
     two snapshot knobs are no longer recalled by a press on the E16: a double-click on the
     screen and their RECALL addresses still do it. *Mine:* knob 14 there is left as it was
     (held for half a second: OPEN, the library; let go sooner: its recall), since you did not
     name it; say if OPEN should be a plain press there too. Both scenes' page 12 have it, so
     **both scenes have to be loaded onto the E16 again**.

## Twenty-eighth build (Nov 13): a drum bus in the master; KEY reaches the sound

274. **You:** "do you know how ableton's drum bus makes drums sound so good? Would be nice to do
     something like that built in", then "do it as a bus across lanes and put it in the master
     module." DRUM BUS is in the master: the lanes its CONFIG names play through it together on
     their way to the mix. What it does, in order: a gentle compressor, drive, a second
     distortion on the highs alone, a transient shaper that leaves the sub be, a tuned resonance
     under it all, a low-pass, a dry/wet mix. (A first version of it as an insert, one lane at a
     time, was not kept: it was never pushed.)
275. **Four knobs on the master's face: DRIVE, CRUNCH, PUNCH, BOOM. The rest in a CONFIG the
     master now has.** *Mine.* Eight did not fit beside TAPE, COMP, LIMIT, the readouts and the
     meters; the four are the ones a hand rides, and what is set once (DAMP, BOOM TUNE, BOOM
     DECAY, MIX, TYPE, COMP, BOOM TO KEY, LEVEL, MOTION) is behind the C beside DRUM BUS. The
     meters gave up the room: the same eleven lanes and the mix, narrower. With the master in
     front, C on the keyboard and OPEN on the E16 open it. Say if you would rather have other
     four, or the meters as they were and the knobs smaller.
276. **LANES, in that CONFIG, is which lanes play through the bus.** *proposed.* The five drums to
     begin with; a switch each for all eleven, so the kick can be taken off it (a common way to
     keep the kick clean) or RUMBLE put on. A switch thrown crosses the lane over in one buffer.
     A lane's sends leave before the bus.
277. **At rest it is not there.** *Mine.* With DRIVE, CRUNCH, PUNCH, BOOM and DAMP at nought and
     LEVEL at 0 dB the lanes reach the mix as they did before there was a bus: checked, the bus
     is recorded as doing exactly nothing, and the set is the set with no lane on it to the last
     bit of a float. So every session and snapshot you have sounds as it did. The first knob to
     move brings the bus in over a hundredth of a second.
278. **DRIVE is glue and heat on one knob.** *Mine.* The compressor (3:1 from 6 dB under the
     bus's peaks, 8 ms in, 90 ms out) comes in over the first third of DRIVE's turn and the
     saturation over all of it; COMP off in CONFIG leaves saturation alone. Both are worked out
     against the bus's own level, read slowly (up at once, down over half a minute while it
     plays, held through silence), so the knob does the same whatever the lanes' levels are,
     and a break of a few bars with the kick out barely moves it. Measured on the kick: full up, 11 dB denser, its
     peak 0.4 dB lower; the same with the lane 11 dB down. Not what Ableton's does: there the
     drive is against a fixed level and makes it louder.
279. **Nothing in it splits the sound into bands.** *Mine.* The first build used a crossover so
     that PUNCH could leave the sub alone, and a crossover's phase threw a hard-driven kick's
     peaks up by 6.8 dB. PUNCH is now a shelf above the sub (160 Hz) whose gain moves with the
     hits: at the centre it does exactly nothing, and the kick's 50 Hz moves by 0.3 dB at either
     end. Either way the hit comes out (up to 9 dB); right the tail is lifted (8 dB), left it is
     pulled down (14 dB). It listens for hits in a way that does not take a noisy tail for one.
280. **BOOM is in the key.** *Mine.* A resonance at BOOM TUNE, rung by the lows, held under a soft
     ceiling. BOOM TO KEY is on to begin with: with KEY set, it rings on the nearest note of the
     key to BOOM TUNE; with KEY OFF it is BOOM TUNE as set. BOOM DECAY lengthens the ring and
     does not thin it. What the bus hands on is held under two and a half times its own level
     however far PUNCH and BOOM are up (1.9 times the kick's peak, with everything full).
281. **A snapshot holds the bus.** *Mine.* The four knobs are stored with the other values
     (`bus.drive` ...), travel on a recall, and a snapshot knob or LEARN reaches them (an ENERGY
     that pushes DRIVE and BOOM is one LEARN away). Its CONFIG is stored beside the returns'
     (`setup.bus`). The row that says what RECALL takes has BUS. A snapshot from before there was
     a bus says nothing of it and leaves it as it is. The snapshots the rig comes with are
     unchanged. TAPE, COMP and LIMIT are still the rig's and no snapshot's.
282. **A take has a fifteenth file, `14 DRUM BUS.wav`: what the bus did.** *Mine.* The lanes on the
     bus as it made them, less those lanes as they came. That way round, every file added
     together is still what the master was given, a lane's file is still the lane, and leaving
     the one file out gives the drums without the bus. The other way (the bus's own output as a
     file, to use in place of the five lanes) is the usual one and I can change to it: say.
     Silent, and so not kept, when the bus was at rest all take.
283. **KEY had not been reaching the sound at all, and now does.** Found while checking BOOM TO
     KEY. The suite's modules take the key only while SF CONTROL is running, and SF-000 has no
     CONTROL: it wrote the key down and nothing read it. SF-000's copy of the suite's processor
     now takes the rig's own key as in force. **If KEY is set to a note in a session or a
     snapshot of yours, it will now sound different**: the pitched lanes move by the key, and the
     kick and the percussion tune to its nearest note (KEY FOLLOW, on in each lane's CONFIG,
     lets a lane be). With KEY at OFF nothing changes. There is a check for it now (17 said "KEY
     transposes" and nothing had tested it).
284. **On MIDI:** the four are on channel 16, CC 71 to 74, and on the second row of the E16's
     MASTER page. **SF-FX has to be loaded onto the E16 again** (SF-000 is unchanged).

## Twenty-ninth build (Nov 13): a snapshot holds what was playing

285. **You:** "When a snapshot is taken, would it be possible to record the midi as well, and when
     recalling, add the option to recall with midi in the sequencer or recall without? Ideally it
     could capture external midi. It should also capture any samples playing as well. The goal
     is complete recall of the snapshot sound even if the midi is internal or external." A
     snapshot now keeps, for every lane: its own steps (as before), the notes that were coming in
     from outside as steps, whether its own sequencer was muted, and the sample it held.
286. **The notes from outside are written down as steps.** *Mine*, from "recall with midi in the
     sequencer": that is where you can see them and change them. SF-000 keeps the notes that
     come in on each lane's channel while the clock runs, with where in the song each fell, for
     the last seventeen bars. At a snapshot each lane's are made into a pattern: as long as the
     part takes to come round (the shortest of one, two, four and eight bars that the last bars
     repeat), or its last bars up to eight if it never does. A note up to a quarter of a step
     early, or anywhere later in the step, is that step's: so notes that arrive some
     milliseconds late (about 11 on your recording of the kick, with ON BEAT lit) land in the
     right step. Two to four hits in a step are a ratchet. A
     note's length is the gate, its velocity the velocity, its pitch the note and octave on a
     lane that takes notes.
287. **The notes are kept as they came, too** (`played.in[].notes`). *Mine.* A snapshot never
     changes (FORMAT's rule 3), and steps lose what is finer than a sixteenth; with the notes in
     it, a later build can play a snapshot's part exactly, or write it down better.
288. **NOTES: a recall with the notes, or without.** A key on the snapshot module (where PAGE 1
     stood, which said nothing), one in the library, and N. *Mine:* one switch that every recall
     goes by, and not a question at each recall, because a snapshot knob's press and a
     sequencer's RECALL have nowhere to ask. Kept with the session. **Lit, as it starts**, which
     is what recalls did until now.
289. **Lit, a recall gives each lane's sequencer what the lane was playing, and sets its M to
     match.** Its own steps; or the notes from outside as steps (alone if its own sequencer was
     muted); or both where both were sounding, the outside's note where there is one (so a part
     the ONE and the lane were both playing, as on your recording of the kick, comes back once
     and not twice). M goes off where there is something to play and on where the lane's steps
     were muted and nothing was playing it. **This changes 85** (*proposed* then: "a snapshot
     recall leaves it alone: say if snapshots should carry it"): with the goal you gave, a
     snapshot whose notes come back muted has not come back. It holds for snapshots taken from
     now on; an older one gives its steps and leaves M alone, as before.
290. **Out, a recall is the sound alone.** Every sequencer stays exactly as it is: steps,
     generator and its knobs, M. This is the one for a set the ONE is playing: nothing is
     doubled. A recall of part of a snapshot goes by NOTES too.
291. **The sample a lane held comes back.** *Mine.* Kept by its path, as an insert's impulse
     responses already were. A lane whose file differs is crossed over to a voice with the
     snapshot's file, like any change that cannot travel. Not kept: SPACE's impulse responses
     (49: which reverb SPACE is and its files are the rig's), and no copy of the audio is made:
     a file that has moved is not found and the lane keeps what it has. Say if snapshots should
     carry copies of their samples; it makes the library stand by itself and larger. (You did:
     the thirtieth build.)
292. **What is lost in the writing down** is said in the README: timing inside a sixteenth, more
     than one note on a step (the lowest is kept), parts longer than eight bars, and notes played
     with the clock stopped. A part's steps are placed by where the bar lines were, so a part
     starts on the step it started on.

## Thirtieth build (Nov 13): the library holds its own samples, and can be put anywhere

293. **You:** "I think a copy makes sense. Ideally deleting a snapshot deletes the copy so I can
     manage just one repository. So moving the instrument to another computer would simply be a
     matter of adding the app and selecting a drive location for the files it needs. Would be
     useful to set a recording location as well (as part of setup) that way I could use external
     disks. And later, we can work on the online library of snapshots and store samples there
     as well."
294. **Every audio file the rig is given is copied into `samples/` in the library folder and read
     from the copy.** A lane's sample or phrase, an insert's impulse responses, SPACE's. The
     file you loaded is never touched. *Mine:* a copy keeps its own name, in a folder named for
     what is in it (the first eight digits of its SHA-256), so the same file given twice is kept
     once, two files of one name do not collide, and the name on the screen and in the Finder is
     still the file's. The whole SHA-256 is kept in the session: it is what the web library
     will want to know a sample by, when samples go there.
295. **In the file a copy is written from the library folder down** (`samples/3fa9c2e1/kick.wav`),
     so the library is right wherever its folder is. *Mine*, and the one place this build
     stretches FORMAT's first rule: a path used to be whole, and a build from before this one
     will not find a sample written this way. Opened with one, a lane comes up without its
     sample, and when that build saves, the rig's own note of which file the lane held is gone
     (the snapshots keep theirs: they are written back as read). Do not go back a build with a
     library you care about without taking `session.json` from `backups/` first.
296. **A session from before is brought over when it is opened**: every file it or its snapshots
     name that is still where it was is copied in. *Mine:* the snapshots are not rewritten
     (rule 3). The session keeps a list of the copies it made and where each came from, and a
     snapshot that names the old place is read as the copy made from it. A file that was already
     gone stays missing, as it was.
297. **A copy that nothing holds any more is removed.** When a snapshot is deleted, the samples
     only it held go with it. *Mine:* also a few seconds after a lane, insert or return is given
     another file, if no snapshot holds the one it replaced; otherwise trying ten samples on a
     lane would leave nine behind until the next snapshot was deleted. "Holds" is every lane,
     insert and return as they stand and every snapshot in the library. *Mine:* the copy is
     deleted outright if the file it came from is still where it was (nothing is lost), and put
     in the Trash if not, since it may be the last there is. Only copies SF-000 made are ever
     removed: anything else in `samples/` is left alone.
298. **LIBRARY, in SETUP: the folder the library is in.** CHOOSE, quit, open again. *Mine:* it is
     taken up at the next start and not at once, so it happens before anything has been read
     and with everything saved, in one place in the code. If the folder holds a library, that
     one is used; if not, the one in use is copied there and the old folder left as it was. A
     folder that is not empty gets an `SF-000` folder of its own inside it.
299. **What goes with the library and what stays.** With it: the session, the clips, the samples,
     the backups, the library's id and what has been sent online. *Mine:* staying on the
     computer, in Application Support: the interface, the MIDI settings, the look, the folders
     chosen, and **ONLINE's address and strings**, which were beside the session until now. A
     drive that travels should not carry the token. So on another computer ONLINE is set up
     again, once.
300. **A chosen library that is not there at a start is not replaced or forgotten.** SF-000 says
     so, uses the library on the computer that time (which may be older, or the factory rig) and
     keeps the choice for the next start. *Mine.* The other way, starting with nothing saved,
     would lose a night's snapshots.
301. **TAKES, in SETUP: the folder takes go to**, from the next take. If it is not there when REC
     is pressed, the take is made in `Music/SF-000/` and says so. *Mine:* a take is not refused
     for want of a disk.
302. **The web library and samples: not in this build.** You said later. What is ready for it:
     every copy has its SHA-256, and a snapshot sent to the web names its samples from the
     library folder down, not by a path on your disk.

## Thirty-first build (Nov 13): POCKETS, the master's tool for who has which part of the spectrum

303. **You:** "Some kind of tool on the master that is world class and allows for the management
     of frequency overlap. This can be ai/smart and automatic or manual (or a mix of both) but
     when making a sound, it would be nice to be able to carve up the bands to create a mix where
     everything sits in a pocket. What those pockets are and how they overlap is a critical
     sound design tool, not just a mixing step. [...] I want to spare no expense on getting this
     right." With it, an overview of what is sold for the job: Trackspacer, FUSER, soothe2's
     sidechain, Neutron's Unmask, smart:EQ 4's tiers, Pro-Q 4, Waves F6.
304. **What I built is none of those with another face: every lane has a pocket, and the pockets
     are the instrument's.** *Mine.* Those tools sit on one track and listen to one other. Here
     the whole rig is known: eleven lanes and two returns, each heard, each carved, by one rule.
     A lane has a stretch of the spectrum that is its own (HOME, WIDTH), held so firmly (HOLD),
     and gives at most so much (YIELD). Where two sounds meet, the one with the weaker claim to
     that band gives way, there, while the other sounds.
305. **How much it gives: what it takes to sit CLEAR below the other, and no more.** *Mine.*
     Trackspacer's law is the other one on offer: cut in proportion to how loud the other is,
     whatever is under it. That cuts a sound that was never in the way. Here a band is cut by
     how far the lane stands above (the other's peak there, less CLEAR), so a lane already
     under it is not touched, and AMOUNT says how much of that is done. What it costs: a quiet
     sound with the stronger claim can ask a lot of a loud one. YIELD is the limit on that, and
     a weak band of the other's (12 dB or more under its strongest) asks no more room than its
     strong ones.
306. **Who wins: HOLD, and a pocket is worth four times as much inside as out.** *Mine.* A lane's
     claim to a band is its HOLD inside its pocket and a quarter of it outside; the claim that
     is stronger by a fifth of full takes the band outright, less than that takes part. So a
     pocket is respected by all but a much firmer lane, and where neither has a pocket (or both
     do) the firmer one wins. Two lanes with the same claim leave one another alone. I tried
     without HOLD (the narrower pocket winning) and dropped it: a wide sound could then never
     be put in front.
307. **What counts as sounding.** *Mine.* A lane claims a band while that band is within 8 dB
     of its own remembered peak there, less and less down to 26 dB under, and only in bands
     that are part of its sound (within 6 to 24 dB of its loudest). The peaks are remembered
     through silence and let down (10 dB a second) only while the lane plays, so a kick back
     after a break is known for what it was. A sound heard for the first time has its claims
     come in over a tenth of a second.
308. **The kick is known before the bottom bands have heard it.** *Mine.* A band at 50 Hz takes
     some 60 ms to ring up, which is longer than the part of a kick that matters. For that long
     after a hit (a jump of the lane's whole level), each band is taken to be where the lane's
     remembered spectrum says, at the level the whole sound has now. Measured: the bass's 50 Hz
     is three quarters of the way down 8 ms into the fifth kick; on the first kick, not yet
     known, it has not begun by then. No look-ahead: nothing is delayed.
309. **Twenty-eight third-octave bands, 31 Hz to 16 kHz, and bells that only cut.** *Mine.* Each
     part is heard through a bank of band-passes (two sections a band), and what a band's
     neighbours let through is taken out of it, so a tone is in its own band and not in five.
     Each part is carved by 28 bells in series (Q 3.5, wide enough that a run of them cut alike
     is a smooth dip), each set to what its band still lacks once its neighbours' cuts are
     counted. A cut comes in over 1 to 6 ms (slower in the lows) and goes at SPEED. A bell that
     is not cutting is not run, and a part with none is not touched: checked, to the last bit.
310. **Where in the lane.** *Mine.* After MUTE and SOLO and before everything else: the sends,
     the drum bus, the stem and the meter are of what was left. So **what is not heard covers
     nothing**, and a soloed lane is heard as it came, not as the mix leaves it. That is the
     one I would most expect you to want the other way; each row of the page has M and S, and
     with them a lane can be heard carved by muting what covers least.
311. **The returns have no pocket.** *Mine.* They give way to every lane, where the lane sounds,
     up to RETURNS; no lane gives way to them. At the sends the rig starts with they are
     already under the lanes and are not touched: it shows when a send is thrown.
312. **Forty-eight knobs, and they are knobs.** *Mine.* In snapshots, travelling over the GLIDE
     time, within reach of a snapshot knob and LEARN, with MIDI addresses. AMOUNT starts at
     nought, so nothing you have sounds different until you turn it; a snapshot from before
     there were pockets leaves them as they are (as with the drum bus, 281). In the row that
     says what RECALL takes, a lane's pocket goes with its lane and the rig's four with BUS.
313. **Where the pockets start** is *proposed*, every figure of it: a HOME for each lane where
     its own sound, as the rig starts it, measured loudest (the kick 55 Hz, the bass 80, the
     rumble 45, PERC 220, the pad 350, the voice 400, the chord 600, the lead 900, the clap
     1.4 kHz, the hats 7.5 and 9 kHz), widths of 1.4 to 3 octaves, HOLD in the order kick, bass,
     voice, lead, clap, chord, closed hat, perc and open hat, pad, rumble, YIELD from nought
     (the kick) to 60 %, CLEAR 3 dB, SPEED 120 ms, RETURNS 12 dB. At full AMOUNT on the rig as it
     starts: nine lanes give somewhere, the most in a band is 9.6 dB (the lead), no lane loses
     more than 2.9 dB over all (the rumble), the set is 0.8 dB quieter.
314. **AUTO places each pocket where its lane's own sound is, and that is all it does.** *Mine.*
     The narrowest pocket, of an octave to four, that holds three fifths of the lane's power,
     where it holds the most. I first had it also keep clear of what firmer lanes had taken,
     which reads as smarter and sent the bass's pocket to 314 Hz, off its own fundamental,
     where the rumble then beat it at the bottom. Who wins where pockets meet is HOLD's to say,
     and AUTO leaves HOLD and YIELD alone. It goes by what the lanes played while the page was
     up (or while AMOUNT was up); a lane heard for less than two seconds is left where it was.
315. **A page, not a panel.** *Mine.* Thirteen maps need the whole screen. P, or a key on the
     master beside AMOUNT's figure; there was no room on the master's face for the knob itself.
     A pocket is dragged on its map; every knob on the page is also on the E16.
316. **On the E16.** *Mine.* The rig's four are the MASTER page's third row, which is the one
     change to a scene: `SF-000-FX.oxie16` wants loading again for those four knobs. The lanes'
     forty-four are on CONFIG's three pages while POCKETS is up, by sending names, with a fourth
     page on the third's knobs since a scene has no room for one. OPEN on the master now goes
     drum bus, POCKETS, back, where it went drum bus, back.
317. **The format.** `values` gains the ids, `made` a `pockets` print, `defs` a `pockets`
     definition saying what a value of each knob means. The chassis is unchanged, so its print
     is too.
318. **The engine plays every lane into its own pair of channels before any reaches the mix**,
     so that POCKETS hears them all at once; it used to play one, add it, play the next.
     Nothing about the sound changes with AMOUNT at nought: sixteen bars of the whole rig from
     this build and from the last one are the same file, byte for byte.
319. **The name.** The suite already says "pocket" for the kick's duck (POCKET DEPTH, POCKET
     SHAPE): a pocket in time. This is a pocket in frequency, and it is your word for it. I
     kept both and say which is which wherever they meet. If two meanings are one too many,
     POCKETS is the one to rename.
320. **A correction to what I told you was missing.** Mono below a set frequency is not missing:
     the master chain has had it since the first build (LOWEND, at 150 Hz). It is not a knob.
321. **Still to come, in the order I will take them**: the convolution with more than one
     impulse response across the spectrum (your first of the two); then your additions of this
     morning: keys that go down on the 3D stage, motion on a snapshot knob, an arrangement of
     snapshots that can be printed for release, and a page of the E16 for the sequencer's steps.

## Thirty-second build (Nov 13): a convolution with three places at once

322. **You:** "Making convolution more powerful for sound design, not just room emulation. Maybe
     turning it into a multiband convolution tool that can take separate IRs for low/mid/high
     bands, for example."
323. **BANDS, in a convolution's CONFIG: a place for the low, one for the middle, one for the
     top.** Each with its own LEVEL, DECAY and SIZE, and two knobs for where the bands meet.
     *Mine:* which knobs a band has. PLACE, LEVEL and DECAY are the ones the idea needs; SIZE is
     there because a band at another size is the least room-like thing it can do. Not there: a
     REVERSE or a START for one band alone (REVERSE and LENGTH work on the whole; one band
     backwards would need the response cut again band by band when the tempo moves, and I left
     it until you ask).
324. **It is one response, not three convolutions.** *Mine.* Convolving with three banded
     responses and adding is the same as convolving once with their sum, so the three are added
     when a knob moves, on the thread that already builds every change to a place, and the
     engine runs what it always ran. It costs nothing more to play, the swap from one response
     to the next is the crossfade there already was, and with BANDS out nothing is different.
325. **The bands meet at 18 dB an octave (third-order Butterworth), not the usual 24.** *Mine.*
     A crossover for one signal wants its two sides to add up to the whole; one for two different
     places wants their powers to. The usual pair does the first and leaves a 3 dB dip where two
     different places meet (measured: the bands came out 1.4 dB short). A third-order pair does
     both: three bands of one place are that place to a hundredth of a dB, and two places meet
     with nothing missing.
326. **This is the suite's module, changed in SF-000's copy** (`suite/PORTING.md`): fifteen
     parameters after its last, so every stored state and every address is what it was.
327. *proposed, not built:* **a place taken from the rig itself.** The last bar the set played,
     written as a file and given to a place, so a lane is convolved with what the rig just did.
     The tape is there (a snapshot's clip comes from it) and so is everything a file needs. It
     is the other way to make the convolution a sound and not a room; say if you want it.

## Thirty-third build (Nov 13): keys that go down

328. **You:** "Make the mechanical buttons change not just their lighting but also make them
     physically detent when pressed (mute/solos, configure, most of the square/rectangular
     buttons) in a way that matches theyre function (latching, momentary, etc)".
329. **A key has three places to stand: up, latched, and all the way in.** 1.27 mm above its
     plate (the model's key), 0.66 and 0.26. *Mine:* the two new figures. Latched is half way,
     so that it reads from the player's view by its shorter side and shorter shade; in is as
     near flush as the rounded edge allows. The lit skin draws the same three.
330. **Which keys latch.** *Mine,* by what each does: a key that is a state stays down while
     the state is on (MUTE, SOLO, C while its CONFIG is open, RUN, REC while a take runs, every
     toggle in CONFIG, CUE, NOTES, PHONES, ONLINE, the scope's keys, LOAD while its list is
     open, RANDOM or EUCLID while armed, LEARN while teaching, the sequencer's M). A key that
     does a thing once is in only under the hand. **The step keys are momentary**, as a drum
     machine's are: the step's bars say it is on, and sixteen keys at two heights would read as
     a broken row. SETUP and POCKETS open something and come back up, though their colour
     says a state (PHONES lit, AMOUNT above nought).
331. **They travel, and a key latched from elsewhere goes by way of the bottom.** In 45 ms, out
     70 (a finger is quicker than a spring). *Mine.* A latching switch cannot go from up to
     latched without touching bottom, so one set by the E16, a snapshot or the keyboard does
     that too: the press is seen even though no pointer made it. A press that lifts at once
     still goes all the way in before it comes back.
332. **An action done from the keyboard holds its key in for 120 ms.** *Mine.* Not done: the
     same for a momentary action that comes from the E16 (a snapshot taken there, a page
     turned): those do not pass through the screen, and nothing on the stage shows them but
     their result.
333. **Travel builds nothing.** The keys are a batch of their own on the card; a frame with a
     key on its way sends that batch (a few hundred keys) and draws the shadow again, and the
     plates, knobs and print are left as they were.

## Thirty-fourth build (Nov 13): a wave on a snapshot knob

334. **You:** "Maybe a way to apply modulation to the snapshot knobs (like a wave, speed and
     amount to a selected knob)."
335. **Three MOTIONs, each WAVE, SPEED, KNOB, AMOUNT.** *Mine:* that it is three units that are
     each put on a knob, and not a wave built into each of the sixteen knobs. It is the shape
     the lanes' MOTION already has in CONFIG (wave, speed, target, amount), twelve knobs fit one
     page of the E16 with a row for each, and a set rarely wants more than a slow one, a fast
     one and the kick. Sixteen knobs with three settings each would be forty-eight controls,
     most of them never touched.
336. **The waves are the suite's own** (`suite/core/Motion.h`, unchanged): the same shapes,
     the same speeds, locked to the song position the same way, so a lane's MOTION and a
     snapshot knob's fall together. Left out: the two that listen to a module's own sound.
     KICK is in: it reads the kick lane's hits from the suite's bus, as a lane's MOTION does.
337. **It is worked out on the audio thread, not by the screen turning the knob.** *Mine.*
     The screen's thread could have turned the knob sixty times a second, and the wave would
     then have stuttered whenever a window was dragged or a file chosen. Instead the session
     writes down, for each knob a MOTION is on, the law by which that knob moves each control
     (a line through what each of its snapshots says, a slope for each knob it is tied to):
     exactly what turning it works out, for every place it could stand. The engine swings the
     knob and reads the laws, every block. The laws are made again only when a knob's
     snapshots or ties change, and handed over without a lock.
338. **What sounds and where a knob stands are now two things in the engine.** A control has
     where it stands (travelling under GLIDE, as before) and MOTION's push on that; everything
     the sound is made from reads the two together, and so do the knobs' lights. Snapshots,
     the hand, the E16's knobs and "is it travelling" go by where it stands. With no MOTION on,
     the two are the same number and the sound is to the sample what it was (checked).
339. **MOTION is not in a snapshot.** *Mine.* The snapshot knobs are not either: they are what
     is played over the scenes. So a recall leaves the waves running, and a snapshot taken
     under a wave holds where the knobs stand and not where the wave had them. (What that
     snapshot is filed as in the library, its sound, is what was heard: wave and all.)
340. **AMOUNT is a share of the knob's turn, either way, and the knob's ends stop it.** A knob
     at nought under a SINE has only the upper half. *Mine:* that it is not rescaled to fit.
     Where the knob stands is yours to choose, and it is the plainest thing to explain.
341. **The 3D eye does not follow a wave.** A snapshot knob swung by MOTION moves many knobs at
     once, which is how the eye knows a recall; it now goes by where knobs stand, so the lights
     move and the view stays, as for a sequencer's knob.
342. **On the E16: knob 15 held on the snapshot knobs.** *Mine,* and the part I am least sure
     of. OPEN there is the library and I left it so; a second press of OPEN for MOTION would
     have made the way back from the library two presses. Knobs 13 and 14 held are already the
     wheel and OPEN on that page, so the next knob held is MOTION. Nothing on the E16 says so.
     If it should be somewhere else, say where.
343. **Addresses for a sequencer: channel 16, CC 102 to 113.**

## Thirty-fifth build (Nov 13): the arrangement, and a print of it

344. **You:** "Maybe some kind of arrangement tool where snapshots could be added to a linear
     flow that morph from one to the other. Some kind of good way to create an arrangement for
     output as an official track release."
345. **A part is a snapshot, its bars, and its MORPH: the way into it, from its first bar.**
     *Mine:* that the morph belongs to the part it leads into and begins on that part's bar
     line, and does not reach back into the part before. It is what a recall with GLIDE does
     now, so an arrangement sounds as the same recalls made by hand would; and a part's length
     is then simply its bars, with no overlap to reckon. A morph as long as the part makes the
     whole part one travel, which is the "morph from one to the other" with nothing held.
346. **Played, it is recalls, each made inside the beat before its bar line.** A recall's knobs
     already set off on the next beat, its patterns change on the bar and its effects cross
     from that beat. So nothing new plays the arrangement: the session asks for each part half
     a beat early, and the engine puts it on the line. CUT is a travel of 0.08 of a beat, the
     least that still waits for the line.
347. **A recall that travels now takes its switches over on the beat too** (the key, a lane's
     sequencer muted or not). They went at once, half a beat ahead of the sound they belong to;
     in an arrangement that is a lane's old pattern heard for two sixteenths before its part.
     *Mine,* and it changes a recall made by hand with GLIDE on: those two now wait for the
     beat with the knobs. With GLIDE OFF nothing is different.
348. **PRINT is made on a rig of its own.** *Mine,* and not where I began. The first print
     rendered on the rig being played and put it back afterwards; two prints of one arrangement
     then differed, because a module that has been playing does not start from its beginning
     again (an oscillator's phase, what a hat draws at random). A release has to be the same
     file each time it is made. So a second rig is built from the session as it stands, the
     arrangement is played on that, and it is let go. It costs a second or two and some memory;
     the rig in your hands is never touched, and a print cannot leave it changed.
349. **The sound stops while it prints.** The suite's bus (the kick's hits, the key) is one for
     the whole app, so two rigs cannot sound at once. The audio device is left running and
     silent, and takes the rig back when the print is done.
350. **What a print is:** the track at 24 bits after the limiter, bar 1 on the first sample
     (each file leaves out the time the rig holds its sound back by), the last bar's ringing
     kept until it has been below -90 dB for half a second (a minute at most), and the parts
     beside it as a take has them, all one length. Its page gives the loudness, integrated,
     and the highest sample. *Mine:* all of these figures, and that the tail is kept (a hard
     end at the last bar line is a trim away; a tail that was never printed is not).
351. **Each part brings its notes in a print, whatever NOTES says.** Nothing outside is
     playing, so a part without its notes would be silent lanes. In PLAY, NOTES is as you set
     it: the ONE may be playing.
352. **The snapshot knobs and MOTION are part of a print; what the hand turns while it plays is
     not.** The arrangement is the scenes, the knobs as they stand and the waves on them. A
     performance over it is recorded with REC, as a set is.
353. **ARRANGE took a place beside LIBRARY and MACROS**, and the SNAPSHOT key gave up a third of
     its width for it. *Mine.*
354. *Not built, and the next thing I would do here:* a tempo for the arrangement and a change
     of it along the line; the hand's moves kept with it (so a print could be a performance);
     the E16 on its parts.

## Thirty-sixth build (Nov 13): the step under the hand, on the E16

355. **You:** "And probably an e16 sequencer control scene with knobs for position, velocity,
     gate, note, octave, etc."
356. **It is the SEQ page there already, filled in, not a third scene.** *Mine.* Both scenes
     have their twelve pages, and SEQ had eleven knobs doing nothing. So the page a lane's
     sequencer already turns the E16 to is now the whole strip.
357. **One step at a time: STEP chooses it, and five knobs are what it holds.** *Mine,* over
     sixteen knobs that are sixteen steps. A row of sixteen velocities is one thing a step has
     out of five, on one page of a pattern that may have eight; and the E16's knobs have no
     keys under them to say which step is lit. One knob to walk the pattern and the rest for
     the step it is on reaches everything, at any length, and is how the screen's own strip
     works (a step chosen, then RATCHET, NOTE, OCTAVE).
358. **The page, by rows:** LENGTH, STEP, VEL, GATE; RATCH, NOTE, OCT; then the generator's
     four in a row of their own, as the screen has them (they were knobs 2 to 5: **this moves
     them**, and the second scene has to be loaded onto the E16 again). STEP pressed lights
     the step or puts it out; HITS pressed is RANDOM and ROTATE pressed EUCLID, which had no
     way in from the E16 before; knobs 15 and 16 pressed are the nudges.
359. **The laws are the screen's.** Velocity 15 to 100; a gate that stops at the step's edge
     (the knob's middle) and at the next lit step's edge, and is legato a little past that
     (the knob's top); VEL and GATE light an unlit step, as a drag does. *Mine:* that the
     gate's upper half stretches to wherever the next lit step is, so its top is always "tied
     into the next", however far that is.
360. **The display's title says the step** (`BAS SEQ 7 D#3`), since the E16 has no other way to
     show which of 128 it is on; and the screen's page of sixteen follows STEP.

## Thirty-seventh build (Nov 13): legends that stay on their plates, and RANDOM at every press

361. **You:** "Anywhere there's a button label that wraps, it projects off the faceplate. Hold
     clear on arrange view, for example."
362. **A legend takes two lines only where its plate has a second line's room.** The strips at
     the foot of the library, the macro designer and the arrangement have none: there a legend
     stays on one line, as HOLD: CLEAR, HOLD: DELETE and the rest now do. *Mine:* that it is
     settled by where the key is (the three foot strips) and not key by key, so a key added to
     one of those strips later cannot run off it.
363. **And it is checked, not looked for.** The stage's checks now hold every legend against the
     plate it is printed on, on the main screen, CONFIG, the library, the macro designer,
     POCKETS and the arrangement (514 legends). Put back as it was, the check fails on the
     arrangement's two.
364. **You:** "On the sequencer, pressing random should generate a new random sequence every
     time rather than just once."
365. **RANDOM writes a new pattern at every press, and stays lit.** It was a latch: the first
     press wrote, the second let go. Now each press sends SEED to another place and writes; it
     looks for a place whose rhythm is not the one playing (with no hits, or every step lit,
     there is only the one). The four knobs still say what plays: the same four write the same
     pattern again.
366. **SEED has 128 places where it had sixteen.** *Mine.* Sixteen patterns for a setting of
     HITS would come round again within a few presses; 128 is a pattern for every place a
     controller's knob has. The sixteen places it had keep the patterns they had, so a
     snapshot taken with RANDOM lit still says what it plays. On the screen it now moves three
     pixels a place; on the E16 every click is a pattern.
367. **A rhythm changed by hand is the hand's.** *Mine,* and it follows from 365: with RANDOM
     staying lit, the next turn of HITS would have written over a step just lit by hand. A step
     lit or put out, a velocity, a gate, a ratchet or a nudge puts the generator's key out and
     its knobs leave the pattern alone; a note, an octave or the length does not. To have the
     generator back, press its key. EUCLID is as it was: pressed lit, it lets go.
368. **You:** "I don't plan on ever using the flat non-3d view ever again. so if it helps for
     you to not have to draw it, there's no need to. I'm all in on the 3d virtual instruemnts,
     so much nicer to play."
369. **Nothing is taken out; new pages are made for the stage.** *Mine.* The lit skin is what
     the stage's parts and print are recorded from, what the checks press, and what stands in
     when OpenGL will not start, so it stays and stays right. What changes is where a page is
     designed: for the stage first, with no flat drawing kept in step with it (**L** still
     shows the old one, which gets nothing new).

370. **A print goes by its own time, not the wall's.** Found while checking this build: with
     the computer busy, two prints of one arrangement differed, and not by a little (the rumble
     28 dB under itself, the set 38 dB, one print 1,280 samples longer). The suite's modules
     tell each other where the kick is over a bus, and a report counts as current for as long
     by the wall clock as the music has moved on, plus a third of a second. That is right for
     a rig being heard. A print runs as fast as the computer lets it, and held up for longer
     than that between two blocks (a disk, another program) every lane ducking under the kick
     lost that kick's duck. So the bus now has a clock a render can drive: while a print runs
     it moves by exactly the samples rendered, and when the print ends it runs on with the
     wall from where it stands, so nothing on it ever sees time step back. The kicks reported
     before a print, and by it, are forgotten at each end: they were another rig's. *Mine:*
     this is an edit to SF-000's copy of the suite's bus (`suite/PORTING.md`); the suite in
     Live is not touched, and does not print this way.
371. **The check now holds a print up on purpose.** The second of the two prints is stopped six
     times for 450 ms on its way; the files must still be the same to the byte. On the wall
     clock that fails every time; it was passing before only because nothing here was busy.
     What this means for prints made before today: one made while the Mac was hard at work
     elsewhere may have a kick or two with no duck under it. Print again to be sure of it.

## The library as a bench (Nov 13): the web library's second build

The code is in the repo `snapshot-library`; what was decided is kept here with the rest.

372. **You:** "To me this is the real magic in the system: a world class instrument ... that
     easily captures the interesting sounds, then an online library and organizing tool that can
     take thousands of collected snapshots and give tools to both manually and automatically
     organize this information." "They feel like little chips of goodnes. So I'd like to keep
     that tactile look." "I love the idea of the web interface being a 3d space where you can
     organize and view these sound chips in 3d." And five things it has to do: names, tags and
     deleting; grouping into buckets ("stacks?"); making macros; "intelligent creation of
     dynamic stacks"; and "creating stacks to load into the instrument".
373. **Chip, stack, macro: the three things.** A chip is a snapshot as an object: its face the
     card the instrument draws, its edge its colour, so a pile's side shows what is in it. A
     stack is a named pile in an order. A macro is a stack with a job, exactly what a snapshot
     knob holds: chips in sweep order and a DEPTH. *Mine:* a chip can be in any number of
     stacks, and a stack only names its chips (by uid), so nothing is copied and a snapshot is
     never changed by being sorted.
374. **The bench: a rack, a table, a tray.** Pockets cut into the body at the back, each with a
     pile and a name plate; the open stack dealt out in front; on the right the tray, which
     stands for the instrument (sixteen knob bays and a row of pockets). *Mine:* the pockets
     are shallow, so three chips lie just under the rim and a fourth stands proud; a pile is
     never taller than about fifteen chips' worth, its chips drawn thinner past that, so a
     stack of three thousand still reads from its side; the table only ever makes the chips
     that show (40 of 3,000), so a library of thousands costs a screenful.
375. **What it is worked out from is what a snapshot already carries**, and the sums are plain.
     *Mine,* all of it: LIKE THIS is the instrument's own distance. A chip's *family* is the
     same colour whatever the energy, near the same tempo, and counts double if taken in the
     same sitting (45 minutes without a snapshot ends one). SONG FROM THIS spreads the family
     over in, build, drive, peak, break, build, peak, out, taking for each part the chip nearest
     the energy the part asks for, with bars (16, 32 on the peaks) and a way in. A mood is a
     place on the seven measures and a weight on each; MOOD FROM THEM takes the marked chips'
     middle and weights each measure by how far they agree on it. AUTO SORT is k-means from a
     fixed start. No model, nothing trained: the same library gives the same stacks.
376. **KEEP and FREEZE.** What one of those gives lies on the table and is not kept. KEEP makes
     it a stack that goes on filling itself; FREEZE an ordinary stack of what it holds now.
     *Mine:* so the rack does not fill with every question asked of it.
377. **The tray is what SF-000 loads**, and `GET /api/load` says it: the stacks put there, the
     macros that have a knob, each with the chips it holds now. *Mine:* a stack that fills
     itself is sent as what it held when the page last looked, so the instrument need not know
     the rules.
378. **A name has two hands on it.** A chip can be named on the bench. *Mine:* the last to
     rename it has it: a name SF-000 sends that is not the one it sent before is a rename there,
     and is taken; the same name sent again leaves the bench's alone.
379. **One row a stack; additions only; and the deploy was rehearsed.** The library's tables grow
     by `CREATE TABLE IF NOT EXISTS` and `ADD COLUMN`. The new code was run on a store exactly
     as the code deployed today left it (62 snapshots, clips, tags): the list came back the
     same field for field.
380. **You:** "I'll want a dark version that matches the instrument."
381. **The dark library is the dark instrument's own colours** (its plates, its ink, its paper,
     its case, its room, from `Screen.cpp` and the stage): plates dark, print light, lights and
     the yellow keys as they were. **D** switches, as there. *Mine:* the first time it is as the
     computer is set; after that as it was left, which the browser keeps.
382. **Putting chips away from the instrument is not built.** It waits on samples being kept
     online, since a snapshot can name one that is only on the Mac.

## Thirty-eighth build (Nov 13): LOAD

383. **You:** "A way of creating stacks to load into the instrument. over time, we can't keep
     all the chip in the instrument itself, so using the online tool to load stacks into the
     machine's library would make sense, I think."
384. **LOAD brings the tray; it never takes away and never writes over.** *Mine,* each part of
     it. A snapshot this library lacks is added whole, with its clip and its definitions; one
     it has is at most renamed. A macro goes on its knob only if the knob holds nothing: a knob
     you set up by hand is not something a press of LOAD should cost you. A song becomes the
     arrangement only if there is none. The line says what was left and how to take it (clear
     it, LOAD again). *proposed:* a held LOAD that does write over, if you find you want it.
385. **The stacks here are the tray's, as of the last LOAD.** *Mine.* Taken out of the tray
     there, a stack is gone from here at the next LOAD, and its snapshots stay: the tray is the
     one place that says which stacks the instrument carries, so the two cannot drift.
386. **STACK steps through them on the library screen.** *Mine,* and the least that makes them
     usable: one key, the stacks in turn, then every snapshot. They are not on the E16 yet (you
     once put it as "an E16 bucket knob just runs through a bucket list": that is the next
     step, a knob a stack on the FIND page), and the macro designer and the arrangement still
     list everything.
387. **A snapshot from the library keeps its uid and gets an id of its own here if its own is
     taken.** The uid is its name everywhere; the id is only this library's handle (FORMAT.md).
     What LOAD brought is marked as held by the library, so it is not sent straight back, and
     a name given there is not sent back as a rename from here.
388. **A snapshot can arrive without its sample.** The web library keeps the snapshot and its
     four bars, not the audio files a lane or an insert was holding. LOAD counts the snapshots
     it brought that name a file this computer has not got and says so. Today that can only
     happen with a snapshot deleted here and brought back, or taken on another computer.

## Thirty-ninth build (Nov 13): a knob that holds a stack

389. **You:** "I see macro creation as a separate process. Dragging chips onto a knob should be a
     way of creating curated categories that can be added to the instruments library which can
     then be scrolled though via knob to cue and select a chip to morph to. And creating a
     macro in the library is similar but it creates a new macro that can be added to the
     instrument onto a knob. So the snapshot knobs can contain macros or library stacks."
     And after: "there are three snapshot knob tyoes: snapshot macro, knob control macro, and
     snapshot cue stack."
390. **A snapshot knob can hold a stack, and then it chooses; it does not sweep.** A turn steps
     through the stack and plays the one it comes to in the headphones; a press recalls that
     one over GLIDE; the knob stays on it. *Mine:* it behaves as the E16's MAP and FIND knobs
     already do (a turn cues, a press recalls), so there is one way of choosing a snapshot by
     knob and not two; it is the knob's own position that says which (a stack of thirty is
     about four clicks a snapshot), so the screen, the keyboard, a sequencer and the E16 all
     work it alike; it is lit amber, and its plate names the snapshot while it is cued.
391. **STEPS, in the designer, makes any knob one.** *Mine:* so a cue stack does not have to
     come from the web library, and so the designer says which kind a knob is. It takes 128
     snapshots where a macro takes ten. DEPTH and MOTION leave it alone: there is nothing to
     scale or to swing.
392. **On the bench, chips dropped on a knob are a cue stack on it; a macro is made on purpose.**
     As you said it. LOAD is told which a knob is (`kind`), and a stack on a knob is among the
     instrument's stacks too, so the same list is under STACK on the library screen.
393. **You, on what the library is to become:** "more realistic looking 3d (3js?) and have the
     same look as the chips and equipment in the instrument. And creating macro creates a little
     module with the two knobs (knob and amount). And then have a model of the instrument that
     I can 'load' macros and chips onto by dragging them into the chassis ... A hands on
     tactile experience second only to real hardware." "I can have multiple snapshot modules
     (collections of macros) that can be dropped into the chassis as a whole module." "I'd like
     to be able to build all three types in the library. And have a library of premade macro,
     stack, and learn knobs to install in the instrument. The instrument then can access the
     library and I can install macros into the macro pockets in the instrument chassis."
394. **Not built yet; what I take it to mean, and what I would decide.** *proposed,* all of it:
     - **A knob is a thing in the library**, of one of three kinds (snapshot macro, knob macro,
       cue stack), shown as a small module with its two knobs. All three are made there. A knob
       macro needs the rig's knobs to choose from: the library has every definition a snapshot
       names, so it can list them by lane.
     - **The sixteen pockets of the chassis are the sixteen snapshot knobs.** A knob module is
       dropped into one. A **snapshot module** is sixteen of them as one thing; dropped into the
       chassis it replaces all sixteen at LOAD, and the sixteen it replaces are first kept in
       the library as a module, so nothing is lost. (A single knob still leaves a full pocket
       alone, as now.)
     - **The instrument reaches the library itself**: in MACROS, a list of the library's knobs
       to put in the picked pocket, bringing the snapshots it needs.
     - **A knob made on the instrument is saved to the library by itself.** You: "knob macros
       made on the instrument then saved to the cloud, almost la like a snapshot but a macro.
       Creating a 3d knob macro module in the library." So: a key in MACROS that sends the
       picked knob (whichever of the three kinds it is), and it arrives on the bench as a
       module. And the sixteen together, as a snapshot module, when asked.
     - **The look is the instrument's own, carried over.** You again: "match the scuffed
       faceplates, the rendering, etc of the app instrument as best you can in web 3d. The goal
       is that the whole ecosystem feels like interacting with tangible objects rather than
       abstracted data." The stage's finish is a shader (grain, mottling, worn edges, how the
       light falls) and a sheet of scratches, `assets/wear.png`, which is already in this repo
       (made from your photograph by `tools/wear.py`). Both can go to the web as they are: the
       same sheet, the same sums. The photograph itself and your 3D model stay out of the
       repos, as you asked.

## The library made as the instrument is made (Nov 13): the web library's third build

395. **The bench is drawn with the instrument's own sums.** The web library's `public/js/look.js`
     is `Source/Stage.cpp`'s drawing carried over line for line: the faceplates' finish (the
     blasted grain, the slow mottling, the scratch sheet, the specks rubbed through along the
     edges), the case's powder coat (its orange peel, speckle, scuffs, rubbed upper edge), the
     one sun from behind and to the left at 48 degrees with its colour, the sky and ground, the
     soft room that shines in everything, the room behind and the warm glow under the case, the
     glow round what is lit (six sizes down and back up with the same weights, the same gain
     and knee), the easing of the brightest parts and the grain. One of the bench's units is 66
     of the instrument's, so its parts are made from the instrument's figures: a plate 1.27 mm
     high with 7.5-unit corners and a 1.2-unit roll, a case 25.4 mm deep, a display's glass
     0.51 mm, the two knob caps by their outlines. The scratches are the same sheet
     (`assets/wear.png`, copied to the library's `public/wear.png`: 613 KB, read once).
396. **What each thing on the bench now is.** *Mine.* A powder-coated case in the dark room,
     wider than the window at its near edge so its far corners and the glow show. The rack is
     a faceplate with a cut-out for every stack; a pile stands on the case inside its cut-out
     (it is no longer sunk in a pocket). A chip is a plate: the snapshot's card printed on worn
     anodizing, each chip with its own wear (by its uid), its colour on its edge. The open
     stack lies on the case itself, as the cards in the instrument's LIBRARY do. The tray is a
     faceplate with cut-outs, until it becomes the chassis.
397. **A stack's name plate is one of the instrument's displays**, not a printed plate. *Mine,*
     and forced: the instrument's sun is behind, so a pile shades what lies in front of it, and
     print there could not be read. What a display shows is light.
398. **A pile stands 0.95 of a chip's depth at most and is drawn in at most 96 layers.** *Mine.*
     Standing on the case it would otherwise hide the name plate of the stack behind it; and
     the layers of a pile of thousands are too fine to see apart (it is also far less to draw).
399. **The rack's plate slides between two rails** (there was one, the lip). *Mine:* the case's
     far edge shows now, so the plate needs something to go under at that end too.
400. **The pale bench has the pale instrument's plain finishes**, as the instrument does (its
     wear and powder coat are the dark one's). *Mine.* The room is the same dark room for both.
401. **The page's own bars are parts too.** *Mine.* The top bar is the case, the two under it
     faceplates with the same scratch sheet; a key is a cap in a hole that goes down in 45 ms
     and comes back in 70 (the instrument's figures); what shows a name or a number is a
     display. They are still the page's own controls (a name is still typed in a field).
402. **`?plain` after the address draws the scene straight to the window**, eased the same way,
     without the glow. *Mine.* The bench takes this way by itself where the browser cannot keep
     a picture with more light in it than a screen shows; the address is there to try if a
     browser draws the deep way wrong.
403. **Made and not yet used: the knob caps, their bases, the sixteen lights of a ring, a
     lamp's lens** (`look.js`). They are for the knob modules and the chassis (394), which are
     next after the stems.

## Fortieth build (Nov 13): a snapshot keeps its lanes

404. **You:** "would it make sense to save the snapshot audio as stems, so that previewing a cue
     could be done with just the lanes that are selected for morph? So if I just wanted the high
     hat or the bass from a snapshot, I could [preview] what it would sound like with those lanes
     deselected?" Yes, and the first half is built.
405. **The engine keeps a tape of every stem beside the tape of its output**: the eleven lanes,
     the two returns and what the drum bus does, each as it goes into the mix (the same fourteen
     the recorder is handed). *Mine:* they are always kept, recording or not, which costs fourteen
     more tapes of sixteen seconds (about 117 MB at 48 kHz) and nothing that can be heard or
     measured in time: a copy of each buffer. A stem's bars are the master's delay earlier on its
     tape than the output's are, and are read from there.
406. **A snapshot's stems are one file beside its clip**, `clips/<id>.stems`: only the stems
     that sounded, each an Ogg Vorbis file (quality 5, a step under the clip's 6), behind two
     lines that say what is in it (`FORMAT.md`). *Mine.* One file because a snapshot's lanes are
     one thing to send, keep and delete; by key because a later rig may have other lanes.
407. **The scope's keys narrow the cue.** *Mine,* following your words: the row that says what
     RECALL takes also says what is heard. A key put out while the cue plays changes it at once
     (a 10 ms fade each way, as any change of cue). With every key lit the cue is the clip
     itself, through the master as it was heard; narrowed, it is the lit stems added together,
     which have not been through the master. KEY (the master's part of the scope) and NOTES
     have no sound of their own and change nothing in the headphones.
408. **A snapshot with no stems kept is cued whole**, whatever the scope. *Mine.* Silence would
     say the lanes are empty, which is not true.
409. **Not built, and what I would do next** (*proposed*): send the stems to the web library
     with the clip and bring them at LOAD (one more thing kept there per snapshot; the bench
     could then play some of a chip's lanes); and lanes for the snapshots taken before,
     rendered from the snapshot as PRINT renders. (I also proposed here the set itself in the
     headphones with the chosen lanes taken out and the snapshot's put in. It was built and
     taken out again: 415.)

## The forty-first build, taken out (Nov 13)

415. **IN SET was built, heard and removed the same evening.** It was a key at the end of the
     scope's row that put the rest of the live set (every stem the scope left out, before the
     master, set back by the master's delay) under a narrowed cue: my reading of "what it would
     sound like with those lanes deselected". **You**, of the fortieth: "the stemmed cueing
     snapshots work awesome." And of this: "I liked it best when I could hear exactly what was
     playing on the pa in the left headphone, and the cued snapshot [with] whatever parts were
     selected in the other. no need to [remove] those parts from the left phone." So it is
     gone, key and engine both (commit 4cfe8a8 has it, and its items 410 to 414, if it is ever
     wanted). What stands is the fortieth: the left ear is the set as the PA has it, the right
     is the cued snapshot's lit lanes and nothing else. A session saved while the key existed
     has `cueInSet` in it; this build lets go of it at the next save.
416. **What I take from it.** *Mine.* The ear does the comparing: the set in one ear and the
     candidate in the other is the preview, and mixing them for you takes that away. I will
     not put the set into the cue again unless you ask.

## Knob modules and the chassis (Nov 13): the web library's fourth build

417. **You:** "it looks great! let's keep building." (Of the library's new look, in your
     browser.) So the order stands: knob modules, the chassis, then the instrument's side.
418. **A knob is a module**: a plate 140 by 88 of the instrument's units with its name on a
     display, what kind it is and how much it holds printed on it, and the instrument's two
     small knobs, TURN and DEPTH, with their rings of lights. *Mine* (the size: the largest
     that lets sixteen stand four by four beside a table of four columns in your window).
     A macro is always a module and rides on the pile of chips it sweeps through; a stack is
     one once it is on a knob (a cue stack). A knob macro (LEARN's kind) is the third, and
     is next (394).
419. **The tray has become the chassis**: a model of SF-000 standing on the bench on a case
     of its own, sixteen pockets four by four as the instrument's snapshot knobs are, and
     under them the stacks LOAD brings. *Mine:* four by four, though two by eight would give
     larger modules, because where a knob is on the instrument is how you know it. A module
     in a pocket shows its knob's number, on teal if it sweeps and on amber if it steps, as
     the instrument lights them. What a drop means is as it was (a macro or a stack on a
     knob, chips on an empty one a cue stack, a stack under the knobs loads).
420. **The modules' knobs turn under the hand.** *Mine.* Drag up for more, 150 pixels for the
     whole turn. DEPTH is the macro's depth and is kept when the hand lets go. TURN goes
     through the chips the knob holds and plays each one it comes to: a cue stack's as the
     instrument's knob would cue them, a macro's as the places its sweep passes through (the
     clips, not the morph between them: the library has no rig to morph).
421. **A knob's pile is pressed beside its module** to deal it out; on the module are its
     knobs, which turn. *Mine.*

## Knob macros, in the library and through LOAD (Nov 13): forty-second build

422. **A knob macro is a thing in the library**: LEARN's kind of knob, which holds no
     snapshots but moves, each one of the rig's knobs and how far the whole turn takes it (as
     a knob's `links` are in the session file: `c` the knob's id, `d` a share of its travel).
     There it is a stack of kind `learn` with its moves beside it; nothing in the library's
     tables changed to hold it.
423. **It is built there from two chips.** *Mine.* Mark two and press KNOB MACRO: every knob
     that stands differently in the second than in the first is a move, by as much as it
     differs (the furthest first, sixty-four at most, a difference under half a hundredth
     left out). So "the change from this sound to that one" becomes a knob that makes that
     change from wherever the rig is, which a snapshot macro cannot (it goes to the snapshots
     themselves). Its moves lie on the table as plates; AMOUNT sets one, TAKE OUT takes one
     off. Not built: adding a move for a knob the two chips did not differ in.
424. **LOAD ties an empty knob to them.** The knob takes the name, the depth and the moves
     for the knobs this rig has; a move for a knob it has not is carried, as a file's is. A
     knob that holds anything is left, as with a macro (387). A macro may have moves as well
     as snapshots, and LOAD brings both. An SF-000 from before this build reads a knob macro
     as a stack of nothing and leaves the knob alone.
425. **Its module's TURN has nothing to play** in the library (there is no rig there to
     turn), and says so. Its DEPTH is kept, as a macro's is.

## Chips onto the chassis (Nov 14)

426. **You:** "I only want to load specific snapshots or macros, not the whole online
     library." LOAD has only ever brought what is on the chassis (383), but a single chip had
     no way onto it except by making a stack first or giving it a knob. Now: **a chip dropped
     on the chassis itself, under its knobs, is for SF-000.** *Mine* (how): it goes into one
     stack called TO SF-000, made the first time and added to after, which LOAD brings like
     any other. Taken out of that stack, or the stack taken off the chassis, it is no longer
     brought (what SF-000 already has, it keeps). The page now says "chassis" wherever it said
     "tray".

## Forty-third build (Nov 14): the chassis is what the instrument carries

427. **You:** "load doesn't bring in a macro I just made. So the idea is that i want to be
     able to load up the instrument with only good things for a specific show, then change
     those out for a different show. The library holds and organizes everything, and lets me
     stock the instrument with just what i want at that time. While the library holds
     everything." LOAD had only ever added: a knob that held something was left (387), which is
     almost certainly why the macro did not arrive. That rule is gone.
428. **LOAD makes the sixteen knobs what the chassis says.** A pocket's module goes onto its
     knob whatever the knob held; a module taken off the chassis comes off its knob.
429. **Nothing is lost by it.** *Mine,* and the one rule over the rest. Before a knob is
     replaced or emptied, the library must have what it held: if it has not, the knob is sent
     up first as a module of its own (off the chassis, in the rack). If that cannot be done
     (one of its snapshots has not reached the library, or the library does not answer) the
     knob is left exactly as it is, and the line says which.
430. **A knob made on the instrument that the library has never seen goes up into its own
     pocket.** *Mine.* So the first LOAD of this build puts everything your knobs hold onto the
     chassis, and from then on the chassis is the truth. Without this the first LOAD would
     have emptied every knob the chassis did not know of.
431. **The same module on both sides: here wins if it was changed here.** *Mine.* Changed on
     the instrument since the last LOAD (a snapshot added, DEPTH, its name), what is here goes
     up and stays. Not changed here, the library's comes down. Changed on both sides, the
     instrument's is kept and the library's change is lost: the instrument is where a knob is
     tuned by ear.
432. **How a knob knows.** Each knob keeps the id of its module in the library and a line of
     what it held when the two last agreed (`lib`, `libmark` in the session file). A knob
     whose line has not changed is as the library has it.
433. **Snapshots are not taken off the instrument.** *Mine, for now.* "Only good things for a
     show" is true of the knobs after this build, and of what LOAD brings; the library screen
     still lists every snapshot this Mac has. *Proposed next:* the library screen shows the
     show (what is on the chassis, and what has been taken since) unless asked for everything;
     and shows are kept by name in the web library, so one can be swapped for another there.
     Deleting snapshots from the Mac to match is a third step I would only take on your word.

## The snapshot module rethought (Nov 14)

434. **You** (in full, because everything after this follows from it): "Let's rethink this.
     The snapshot module (the 16-knob lane in the main instrument interface) is a performance
     tool that holds two types of things: A macro knob that is a knob that just drives other
     knobs (leaned), and stack knobs that hold an ordered list of snapshots. The goal of the web
     library is to be a useful tool for managing large amoutns of snapshots and creating stack
     knobs to load into the snapshot performance module. I'd divide the snapshot module into
     two fixed categories, four macro knob slots in knobs 1-4 and stack knob slots in knobs
     5-16. A knob stack can be used in two ways: 1)when the cue button is selected, it acts as
     a knob-scrollable preview of snapshots in the stack, you scroll throught the stack then
     press the knob to morph to that snapshot at whatever the glide time is. 2) when cue is
     deselected, it directly starts morphing from wherever the instrument is toward the
     snapshot in the direction the knob is turning, reaching that nexr snapshot, then
     continuing to morph to the next and so on. I use this mode a lot to find happy sweet spots
     in between snapshots. The trick here is that when a stack knob starts to turn, it
     shouldn't jump, it should begin by taking what's playing and morphining to the approah
     snapshot. The cue toggle should be on the panel of the snapshot module so I can sqitxh
     the stack knob mode while playing. Move the snapshot button into thew top bar of the
     instrument alongside record and run. The headphone mode for cue is still a setting inside
     library. Macro knobs created on the instrument also get saved to the online library. In
     the library you can load macro knobs into the four module slots, load stack knobs into
     the 12 slots, create nes stack knobs, and organize snashot chips into useful stacks. I
     likehow the stem system work for snapshots, but I think it would be even better if, in
     the library, the snapshot itself carries the buttons for stem selectins that way when
     constructing stack knobs, I can preconfigure what stems the snapshot will use. This is
     useful for performance. So in the library, the workflow id I preview chips, deselect
     stems I don't want, drag that onto stack knob column to the right of the surface where a
     stack has been dealt. In that column i car add, delete, or reorder the chips. I can
     select/deselct the stems for each chip (ideally on the chips themselves so its visible
     which lanes are active) and then, when I like the stack, i can press a button on the
     column that adds that stack to an available module slot. And then, on the instrument, the
     snapshot module has a load button that will load whatever the library's current module
     load looks like into itself."
435. **You**, four minutes on: "I do want to be able to add modulation tho the muldule knobs
     still." So MOTION stays on all sixteen (443).
436. **You**: "Maybe the top right knob (knob 4) in the snapshot module e16 page can toggle cue
     mode on/off with a press". Then: "in fact, can we standardize so that every e16 page has
     four potential toggle knobs (knob1=glide time, knob4= toggle a single module parameter
     (cue in snapshot's case), knob13=lane select, knob 16=configure." Then: "knob 4 could be
     mute on any sound producing module." (449 to 452.)
437. **You**: "curious if it's possible to have the instrument itself upload the e16 scenes to
     the device when it plugs in. if so, that would mean the instrument could build the scenes
     and pages more dynamically, even including names for things that change (like a named
     stack knob)." (453.)

### The instrument's module (forty-fourth build)

438. **Two kinds by place.** Knobs 1 to 4 are macro knobs: each is tied to other knobs
     (LEARN), has a DEPTH, and adds its push on top of the scene, as before. Knobs 5 to 16 are
     stack knobs: each holds snapshots in an order, one at each of even steps along its turn
     (one alone: at the top). A stack knob has no DEPTH and is tied to nothing; a macro knob
     holds no snapshots. LEARN is offered for 1 to 4 only, and a snapshot tapped in the
     designer goes onto 5 to 16 only. *Mine:* a stack holds 128 at most (the E16's knob has
     128 places).
439. **CUE out: the turn morphs the set, and never jumps.** *The rule is yours; the sums are
     mine.* For each bit of a turn, the scene under the macro knobs goes toward the next
     snapshot in the direction of the turn, by as much of what is left of the way as the knob
     went of what was left of its way to that snapshot's place. So from anywhere (a recall,
     knobs moved by hand, another stack knob) the first click moves the sound a little toward
     the snapshot ahead; at that snapshot's place the set is that snapshot; the turn carries
     on to the next. Turned back, it heads for the one behind. Nothing is remembered between
     turns, so there is nothing to jump from. Sound knobs travel over the GLIDE time, as under
     any hand. *Mine:* only what can morph moves this way (the knobs); a snapshot's notes,
     switches and effects come when the knob is **pressed**, which goes all the way to the
     nearest snapshot, as a recall. Macro knobs stay on top of whatever a stack knob does.
440. **CUE lit: the turn chooses, the press goes.** A turn goes through the stack in the
     headphones (the snapshot nearest the knob, from its lanes: 444) and the set is not
     touched. A press morphs the set to the one the knob is on, over the GLIDE time, and the
     knob stays there. *Mine:* putting CUE out without pressing puts each stack knob back
     where it stood (the set has not moved, so neither has its knob), and stops the cue.
441. **CUE is one key for the module**, on its panel where SNAPSHOT was, lit amber; it is in
     the designer's strip too, and it is knob 4's press (449). The stack knobs are lit teal
     while their turns move the set and amber while they choose; the macro knobs white. A
     stack knob standing on one of its snapshots shows that snapshot's name in place of its
     own. CUE is kept with the session.
442. **SNAPSHOT is in the top bar**, left of REC. **LOAD is on the module**, beside CUE: the
     same LOAD the library screen has. What it said is on the top bar for a few seconds.
     PHONES stays in the library screen, as you said.
443. **MOTION on a stack knob swings it along its own stack.** *Mine.* The wave moves the
     knob's place; the set follows the stack's path through its snapshots (how each differs
     from the first), on top of where the set is. With CUE lit the swing is about where the
     knob stood when CUE was lit, so a wave keeps running while you choose. A stack of one has
     no path and does not swing. A stack of more than sixteen is read at sixteen even places.
444. **Each snapshot of a stack takes only the parts of the rig chosen for it**: the lanes,
     the returns, the drum bus, the key, as RECALL TAKES has them. Turned to (CUE out),
     pressed, or heard in the headphones (CUE lit), a part that was put out stays as it is.
     They are chosen in the web library (458); the designer draws such a snapshot's number on
     amber. *Not built:* choosing them on the instrument.
445. **What your knobs become.** *Mine, and the one thing here that changes what you have.* A
     knob that is where its kind now lives stays. A knob with snapshots on 1 to 4 moves to the
     first free knob of 5 to 16; a knob tied to knobs on 5 to 16 moves to the first free of 1
     to 4; in the order they were in. A knob that had both becomes two. MOTION follows its
     knob, and so does where the knob stood. One that finds no knob of its kind is kept in the
     session file and sent to the library, off its module. **Your sweep macros become stack
     knobs**, which is not the same thing: a sweep macro added how far its snapshots were from
     its first one on top of the set, by its DEPTH, and several could be combined; a stack
     knob takes the set to its snapshots. What the sweep macros were adding when you last
     closed SF-000 is put into the set, so it opens sounding as you left it. The session file
     as it was is kept beside it as `session-before-two-kinds.json`, and is never overwritten.
     **One exception: the four the rig came with.** A knob on 1 to 4 that still holds exactly
     INIT and one of the rig's five pushes (ENERGY, BUILD, ACID and DIST as a session first
     had them) becomes a macro knob where it is, tied to every knob its push moves by as much
     as it moves it (a move under 0.4% of a turn is left out). It does what it did, adding its
     push to wherever the rig is, and it stays under the hand it was under. Moved to a stack
     knob it would have taken the whole set to the push instead, and knobs 1 to 4 would have
     opened empty.
446. **A new session's knobs.** *Mine.* ENERGY, BUILD, ACID and DIST are macro knobs now (tied
     to exactly what each used to sweep, so they sound as they did); knob 5 is a stack of the
     five snapshots the rig comes with, called TOUR.
447. **Knobs made here go to the library by themselves.** With ONLINE lit, a knob made or
     changed on the instrument (a macro knob taught, a stack changed, a name) is sent once it
     has stood unchanged for a pass, into its own place on the library's module if that place
     is free, else into the library off the module. A knob emptied here (CLEAR) comes off the
     library's module and stays in the library. LOAD does the same first, whatever ONLINE is.
     *Mine:* a knob is sent only when the library has every snapshot it holds, so the rig's
     own five (which never go to the library) keep TOUR at home.
448. **LOAD, by kind.** The library's module is places 1 to 4 for macro knobs and 5 to 16 for
     stack knobs, as here. LOAD takes each as before (428 to 432) and brings each snapshot of
     a stack with its parts. A place holding the other kind is left and said. A stack knob is
     no longer also a stack on the library screen unless it is marked for that there.

### The E16's four corners

449. **Every page, behind a press:** knob 1 GLIDE (the knob is the GLIDE time, its ring
     green, until pressed again); knob 4 the module's one switch; knob 13 the pointer's wheel
     (lane select); knob 16 CONFIGURE (OPEN; on CONFIG's pages SHUT).
450. **Knob 4, page by page.** *Yours: MUTE on anything that makes sound, CUE on the snapshot
     knobs. Mine, which mute:* a lane's page, that lane's MUTE; INS 1 to 3, that insert's
     MUTE; RETURNS, DUB's MUTE, and knob 8 SPACE's, since the page holds two; SEQ, the lane's
     own sequencer muted. MASTER and CONFIG have none.
451. **What moved to make room.** MID is behind LOW (knob 15) on a lane's page, where it was
     behind HIGH. OPEN is knob 16 everywhere, where it was 14. SEQ's nudges are 14 and 15;
     CONFIG's PAGE - and PAGE + are 14 and 15. FIND's GLIDE knob is 14.
452. **Where a press already plays.** *Mine.* On the snapshot page 13 and 16 are stack knobs:
     a short press is the stack's own (it must be: with CUE lit the press is how a choice is
     taken), and held for half a second they are the wheel and OPEN, as 13 and 14 were. Knob 4
     is a macro knob whose press is CUE, so its macro gives up the press that lands it on the
     nearer end of its turn (1 to 3 keep theirs). Knob 1 there is GLIDE behind its press, as
     it was. On the library's MAP, 13 and 16 held; its other presses recall. MAP's and FIND's
     knobs 1 and 4 stay the library's.
453. **Scenes are not sent to the E16 by SF-000.** I found the scene file's form (it is how
     SF-000 writes them) and nothing published on how the OXI App puts one on the device. Not
     built on a guess. **What it was wanted for is done another way:** the script in both
     scenes already takes names from SF-000, so the snapshot page is told its sixteen knobs'
     names as they are now (four characters each), its title says CUE while it is lit, and
     for four seconds after a stack knob is turned or pressed the title names the snapshot it
     is on. No scene needs loading for a name to change.
454. **Both scenes changed: load them onto the E16 again.**

### The web library's module (its sixth build)

455. **Its module is the instrument's**: places 1 to 4 take a macro knob (white), 5 to 16 a
     stack knob (teal). The library refuses the other kind in a place, and says which goes
     where. *Mine:* what was already on a place of the other kind is moved the way 445 moves
     your knobs, by the same rule, so the two still agree place for place.
456. **The column**, between the table and the module, is the stack knob being built: its
     chips one under another in the order the knob goes through them. Chips are dragged in
     (and onto a place among the others), dragged within it to reorder, and dragged out of it
     to take them out. At its head: its name, how many it holds, **ADD TO MODULE** (the first
     free stack place; pressed again, it comes off) and NEW (an empty column; the one that was
     there stays in the rack). The first chip put into an empty column begins a new stack
     knob. A stack dragged onto the column, or a stack knob pressed on the module, comes into
     it. STACK KNOB (where MACRO was) makes one of the marked chips.
457. **Every chip carries its keys**, along its foot: one for each lane (with how much of that
     lane the snapshot plays standing in it), then R, B and K for the returns, the drum bus
     and the key. Lit, the part comes with the snapshot; put out, it is left alone.
458. **Where a key's setting is kept.** In the column, and on the table when a stack of your
     own is dealt, the stack keeps it. On a chip lying anywhere else it is kept by the page
     until the chip goes into a stack, and goes in with it: preview, put out, drag. *Mine:* it
     is not kept across a reload until then.
459. **Heard with its keys.** A chip pressed with keys put out plays from its stems, each part
     on its own, the ones put out silent; a key pressed while it plays is heard at once. With
     all its keys lit it plays its clip, as before. ONLINE now sends each snapshot's stems
     (about a megabyte each). One whose stems are not in the library plays whole and says so;
     its keys still say what it takes.
460. **Macro knobs** are made on the instrument (LEARN) and arrive by 447, or here from two
     marked chips (KNOB MACRO), and are dragged onto places 1 to 4.

## What you found in it the same morning (Nov 14)

461. **You**, 11:29: "i bult a stack knob called test stack 1 in the library but it won't load
     into the chassis" (466.)
462. **You**, 11:34: "some other bugs: the e16 snapshot page doesn't have the select toggle
     (knob 13). The glide label doesn't update as you turn the knob." (467, 469.)
463. **You**, 11:37: "if need be we can not have snapshots on the four corner knobs to retain
     the cinsistent interface functionality. I'd rather have fewer macros/snapshots than
     inconsistent ui" (467.)
464. **You**, 11:40: "also, it might make sense to build the online lobrary functionality and
     ui right into the device itself, one that uses the locl snapshot repo. Wioth a duplicate
     online, and an option to load from the local library or the remote library." And: "so
     when clicking the library button on the instrument, it goes to a similar style macro
     designed/stacks page. thoughts?" To what I answered (470), at 11:49: "sounds good." and
     "afterr the fixes, start building the integrated library".
465. **You**, 11:49: "Also, when cued, sometimes the stack knob stays where I left it but
     sometimes it resets to zero when i turn it again. it should stay where it was left. And
     recalls should go to the interpolated position between the snapshots if thats where i
     pressed the recall." (468.)

### The forty-fifth build

466. **LOAD makes room.** *Mine, and it undoes a rule of mine that was wrong.* A stack knob
     that holds any of the rig's own snapshots (CLEAN SLATE, INIT and its five pushes) can
     never be given to the web library, which is never sent those; and LOAD would not replace
     a knob the library had not got. So such a knob (TOUR in a new session, WARP in yours)
     held its place against the library for good, and the library, which cannot see it, put
     your new stack knob on that very place. Now: a stack knob here that the library cannot be
     given, on a place its module has another knob for, goes to the first stack knob that is
     free both here and on the module, as it is (its snapshots and their parts, its name,
     where it stood, its MOTION), and the module's knob takes the place. LOAD says so first:
     "WHAT KNOB 5 HELD (WARP) IS ON KNOB 6 NOW". With no stack knob free it is left, and LOAD
     says why and what to do. A knob of the rig's own snapshots that nothing wants the place
     of is no longer reported at every LOAD. The knobs are listed in their own order.
467. **13 and 16 are no knob of the module.** *Yours (463); which knobs, mine.* The four
     corners are the same on every page of the E16, the snapshot page included: knob 1 is
     GLIDE behind its press, knob 4 pressed is CUE, **knob 13 is the lane wheel** (a press
     takes the pointer up at once, its turns are the pointer's, a press chooses) and **knob 16
     pressed is OPEN**. A stack knob needs its press for itself, so none is on 13 or 16: the
     stack knobs are **5 to 12, 14 and 15, ten of them**. *Mine:* the four macro knobs stay on
     1 to 4. A macro's own press is not needed (it was a shortcut), so 1 and 4 already work as
     the corners do everywhere, the knob's own turn with the corner behind its press; taking
     them off would cost two macro knobs and change nothing you can feel. This replaces 452's
     half-second holds on 13 and 16 (15 held is still MOTION).
     A session with a stack on 13 or 16 has it moved to the first free stack knob when it
     opens, by 445's rule; the web library does the same to its module, and refuses a knob on
     those two places. On the screen the two seats are bare (INFO names them), and their cards
     in the designer say what they are. The scenes you have still work: their 13 and 16 on
     that page are taken as the same corners. The new ones give them their names and colour.
468. **A press takes the set to where the knob is, and the knob stays there.** *Yours (465);
     the sums are mine.* With CUE lit or out: on a snapshot's place (to within a click of the
     E16's knob) the set goes to that snapshot, whole, for the parts of the rig it takes, as
     before. **Between two it goes that far between them**: every knob of the rig to as far
     between where the two snapshots have it as the knob stands between their places, over
     the GLIDE time. A part only one of the two takes goes between where that one has it and
     where it stands now. *Mine:* what is not a knob (the notes, the effects in the rack, a
     sample) belongs to a snapshot and comes only with a press on its place; between two it
     is left as it is. The headphones still play the nearer snapshot: a mix of two is not
     something there is a clip of. **What reset the knob:** a press used to put it on the
     nearest snapshot's place, which for anything short of half way to the second one is
     nought. And **putting CUE out no longer puts the stack knobs back** where they stood
     before it was lit (440): each stays where it was left, and its next turn goes on from
     there, from wherever the set is, as any turn with CUE out does.
469. **GLIDE's time is printed again in 3D.** On the 3D stage a knob that travels is only lit
     differently, and nothing is printed; but GLIDE's time is printed beside its knob, and it
     was left as it was. Seen now that knob 1 of every page turns it. A check holds a part
     printing against a whole one, and fails without the fix.
470. **The library, in the instrument.** *Yours (464); what I answered, and you agreed to.* A
     live instrument should be whole on its own: 466 came from the seam between two libraries,
     and a show without a network could not arrange its own knobs. To be built natively, in
     the instrument's own cards, as the designer is: LIBRARY opens the snapshots as cards with
     their keys, a column where a stack is built and put in order, and the places to put it
     on. Not the web page inside the app: that would be the mouse only, its previews would not
     be the headphones' cue, and the E16 could not drive it. The heavy sorting (AUTO SORT,
     moods, LIKE THIS, tags in bulk) stays on the web. The instrument's library is the
     original and ONLINE keeps the web's the same, stacks and module included; stacks made
     here go straight onto knobs, and LOAD is for what was arranged in the browser. *Not
     built in this build:* it is the next one.

### The library in the instrument: its first part (forty-sixth build)

471. **In stages.** *Mine.* (1) **The column, in the designer**: a stack knob built and kept
     on the instrument alone. This build. (2) LIBRARY and MACROS as one page: the snapshots,
     the column and the places together, with FIND. (3) Stacks that are on no knob: a rack of
     them here, as the web library has, to put on a knob and take off. (4) ONLINE both ways
     by itself, so the web's copy and this one stay the same without LOAD; LOAD then chooses
     which of the two a module is taken from. (5) The E16 on that page.
472. **The column.** With a stack knob chosen in the designer, its stack stands beside the
     library: one card for each snapshot, in the knob's order. A snapshot tapped in the
     library goes on at the end. On each card: **a key for every part of the rig** (the eleven
     lanes, R the returns, B the drum bus, K the key), black while that snapshot takes the
     part, an empty frame when it is put out; **UP** and **DN** move it earlier or later, its
     keys with it; **its number** takes it off; **the card itself** plays it in the
     headphones with the parts it takes, and a key pressed while it plays is heard at once.
     The wheel over the column moves it when it holds more than eleven. Until now a
     snapshot's parts could only be chosen in the web library (444). *Mine:* UP and DN rather
     than a drag, for now: a drag among cards that also scroll is its own piece of work.
473. **It needs no network, and the web has it anyway.** A stack knob is the session's own,
     kept in the session file. With ONLINE lit it goes to the web library by itself, as 447
     has it, each snapshot with its parts.

### Effects between snapshots (forty-seventh build)

474. **You**, 13:31: "Blends between snapshots should include the insert effects. Effects are
     a fundamental part of a sound. And continue building."
475. **The racks go between two snapshots as the sound knobs do.** *The rule is yours; how,
     mine.* This replaces the part of 439 and 468 that left a snapshot's effects for a press
     on its place. Pressed between two, or turned with CUE out:
     - **The same effect in a slot of both:** each of its knobs goes between the two.
     - **An effect in one and nothing in the other:** the effect, with its MIX as far up as the
       place is toward the snapshot that has it. Half way from dry to a reverb is half the
       reverb. Turned onto the dry snapshot's place, it is taken out.
     - **Two different effects in the same slot:** a slot holds one effect, so the way between
       them is through dry. Over the first half the first one's MIX comes down to nothing; at
       half way the other takes the slot with its MIX at nothing and comes up over the second
       half. *Mine, and the one place I could not give you a true mix of the two.*
     - A knob of an effect that is a menu or a switch (a delay's TIME, FREEZE), and its
       CONFIG, are the nearer snapshot's: they change once, at half way, and the effect is
       crossed over as a recall crosses one. THROW is never held by it.
     - A lane that a snapshot does not take keeps its rack.
     - Turned, it is worked out from what is in the rack now, like the sound knobs, so the
       first click of a turn never jumps. If the set got between two places some other way,
       with another effect sounding past half way, that effect's MIX comes down as the knob
       goes and the snapshot's own takes the slot at its place.
     *Still a snapshot's own, with a press on its place:* the notes, the lane's model and its
     samples. *Not in it:* MOTION on a stack knob swings the sound knobs and not the racks,
     and a macro knob is still not tied to an effect's knobs.

### The library in the instrument: hearing before adding (forty-eighth build)

476. **In the designer a snapshot is heard before it is put on a knob.** *Mine, from how you
     said you work: "preview chips, deselect stems, and then drag the chip onto a stack knob
     column" (434).* A card of the library pressed plays in the headphones, whole, its name
     green; pressed again it stops. Its **ADD** key puts it on the chosen stack knob, where a
     press on the card used to. In the column a card is heard with only the parts it takes.

477. **The rack, on the instrument.** *The third stage of 471; the shape is mine.* A stack
     can be kept off the knobs and put back on one, with no network.
     - **KEEP** (the designer's strip, with a stack knob chosen; where LEARN is for a macro
       knob) keeps that knob's stack in the rack under the knob's name: a copy, each snapshot
       with the parts it takes. The knob goes on holding it. Kept again under the same name,
       it is the same one, as it is now.
     - Beside the column are two plates, **SNAPSHOTS** and **STACKS**. STACKS shows the rack:
       the stacks kept here and those LOAD brought for the library screen. A stack's **ADD**
       puts its snapshots, with their parts, onto the chosen knob after what the knob holds
       (an empty, unnamed knob takes the stack's name). Its number takes it out of the rack.
     - This is **loading from the local library**. LOAD is still the web library's module.
     - LOAD leaves a stack kept here. The session file keeps them (`here`, and `lanes` where a
       snapshot leaves a part out).
     *A copy, not the same object:* changing the knob's stack afterwards does not change the
     one in the rack until KEEP is pressed again. *Not built:* a stack kept here does not go
     to the web library yet (a stack **knob** still does, by 447). That is the fourth stage,
     with the web's copy and this one kept the same both ways.

### What you found that afternoon (forty-ninth build)

478. **You**, 14:34: "bug: when navigating to a new lane but without closing a config panel
     first, you get unexpected lane selection until you go back and close the config panel.
     maybe we automatically close the config whenever navigating to another lane?"
     **Done as you said, and a little wider.** CONFIG shuts when another lane comes to the
     front, by any hand. *Mine:* and when another module does (an insert, the returns, the
     master) by a key or a controller; under the mouse it still stays where it was put for
     those. The same lane brought again leaves it. It used to go along to the new lane, which
     kept the E16 on CONFIG's page when the lane's own page had been asked for.
479. **You**, 14:35: "also, a way to take snapshots from the E16 on any page would be nice.
     Would a double tap on glide work, or maybe pressing 1 and 13 together?"
     **Knobs 13 and 16 pressed together.** *Mine, because knob 1 cannot be used:* its press
     swaps the knob to GLIDE inside the E16 and sends SF-000 nothing, so neither a double tap
     on it nor 1 with 13 can be seen. 13 and 16 are the two bottom corners, their presses
     reach SF-000 on every page, and the scenes you have already send them. So that the two
     can be told from one, each now acts as it is let go (or after half a second held), not
     as it goes down. The E16's display says SNAPSHOT TAKEN. Not on the library's map, where
     13 and 16 are held.
480. **You**, 14:44: "Bugs: the macro button on the library page takes you backl to the
     instrument main, not macros. I made a knob macro that turned some filtersdown and one
     up., It worked, but when i made some direct adjustments to the knob that turned up, the
     other filters refused to return to the state with the macro turned down." And at 15:00:
     "I have been unable to reproduce the knob macro bug so unless you've found it, we can
     hold on that until it happens again."
     **MACROS to MAIN:** found one way it happens, and mended it. With the library opened on
     the screen and the E16 still in its first scene, MACROS sends the E16 to the snapshot
     page; it says "the first scene's twelfth", SF-000 took it to be in the second, read that
     as a hand's turn of the page, and put MAIN up. The snapshot page is the twelfth of both
     scenes: being on either is being there. *I could make that happen here; I do not know
     that it is what happened on yours.*
     **The macro:** not found, and held, as you said. The sequence you described is now a
     check, and passes (the others come back). *One thing does exactly what you saw, by
     design:* a macro knob **pressed** (a double-click on the screen; knobs 2 and 3 on the
     E16) past half way keeps what it has pushed and sends the knob home, so the knob reads
     nothing and the filters stay where they were pushed. Left as it is until you say.
481. **You**, 14:59: "stack knobs work greate and as expected in non cue mode but in cue mode
     I don't hear anything in the headphones change from pa. i suspect because we don't have
     the phones option as the default." And at 15:01: "ignore the cue message. I turned
     phones on and it works just fine." **Nothing changed in how a cue is played.** *Mine,
     small:* CUE lit with nowhere for a cue to play (no outputs 3 + 4 open, PHONES out) now
     says so on the top bar and on the E16's snapshot page, and a stack knob come to a
     snapshot with no sound kept says that. PHONES is still off each time SF-000 opens, so
     it cannot be left on for a PA.

## New notes in the key (Nov 14)

482. **You**, 15:30: "On the sequencer, default newly placed notes to the key if set." And
     at 15:31: "then continue library work."
     **What was there.** A new step on BASS, LEAD, CHORD, PAD or VOICE already *sounded* the
     key's note: parts are written in C and a lane with KEY FOLLOW on (every lane, as it
     comes) plays them moved by the key (283). But the step *said* what was written, so with
     KEY at A a new step read `C3` while an A played. What you saw was the label.
     **Now a step says the note that sounds.** With KEY at A a new step on LEAD reads `A2` and
     is A2; one you wrote a minor third above the root reads `C3`. Change KEY and every step of
     a lane that follows reads its new note, as it plays it. NOTE (the key on the screen, the
     E16's knob 6 on a SEQ page) goes a semitone at a time up from the key's note, so the
     knob's bottom is the root. The E16's title says the same (`BAS SEQ 7 A0`). With KEY OFF
     nothing reads differently from before. Nothing stored changes: steps are kept as written,
     as they always were, and no snapshot or session is touched.
     *Mine:* the key moves a part up as far as F and down from F# (the suite's law, unchanged),
     so the octave a step says is the one it sounds in: BASS in A reads `A0`, in D `D1`.
483. **A lane with KEY FOLLOW off is written at the key's note.** *Mine.* Nothing moves that
     lane's steps, so "default to the key" has to be done in the writing: a new step there is
     written at the key's note, the same pitch a following lane would sound (in A, LEAD gets
     A2). So is a step a generator lights that held nothing (a step that kept a note under the
     generator keeps it). Changing KEY afterwards does not move them: that is what KEY FOLLOW
     off means.
484. **Found on the way: KEY set by anything but the hand left the top bar's KEY as it was**
     until something else drew the bar again. The bar is now drawn again when KEY changes, and
     so are the steps (their notes read differently). In practice a recall already drew the
     whole screen, so I do not think you have seen this one.

## The rack, the same here and on the web (Nov 14): fiftieth build

485. **The fourth stage of 471: ONLINE both ways, for the rack.** *Yours in 464 and 470 ("With
     a duplicate online"); the rules are mine.* The stacks in the designer's rack and the
     stacks the web library has **lit for the instrument** (its TO SF-000 lamp) are one set,
     kept the same by ONLINE without LOAD:
     - kept here, it goes there (lit, off the module, each snapshot with its parts);
     - made or lit there, it comes here, in the library's order (a snapshot it names that is
       not here is brought first; until then the stack is left alone on both sides);
     - changed on one side, it changes on the other;
     - **changed on both since they last agreed: the instrument's stands.** *Mine.* The
       instrument is the original (470). The other way round is one line: say.
     - unlit or removed there, it leaves here (changed here since, it stays and goes back);
     - its number here takes it out there: one made there is unlit (the library keeps it), one
       made here is removed there. Removed there is marked, not erased: the library can put it
       back, but its page has no key for that yet, so it would be me doing it. Say if you
       would rather a stack made here were only unlit too.
     The library is asked for its stacks at once when the rack here changes, and otherwise
     every quarter of a minute while ONLINE is lit. That is new traffic: until now an idle
     ONLINE asked nothing.
486. **Why the lamp and not every stack.** *Mine.* The library's rack also holds piles you are
     only sorting with (and 470 keeps the heavy sorting on the web). Its lamp already meant
     "this one is for the instrument", so that is the set kept the same: nothing arrives
     that you did not light, and a first ONLINE does not pull the whole library's stacks and
     their snapshots down. A stack on a knob is still the knob's (447): it is not in the rack
     unless it is also lit, or kept.
487. **What LOAD is now.** *Mine; it answers "an option to load from the local library or the
     remote library" (464) as far as I can see how.* Two ways to fill a knob: a stack's ADD in
     the rack (local: no network, and with ONLINE lit the rack is the web's too), or LOAD (the
     whole module as arranged in the browser). LOAD sees to the rack first, lit or not. I did
     not add a LOAD LOCAL key: the knobs here *are* the local module, so it would have nothing
     to bring. If you meant something else by it (a second module kept on the instrument to
     swap in, say), tell me.
488. **Nothing was changed in the web library.** It already answers everything this needs
     (its stacks, a stack made, changed, unlit, removed). No deploy.
489. **Safety of your library.** *Mine.* A stack an earlier build's LOAD brought has no record
     of what the two agreed, and no parts: it counts as the library's, and the library's is
     taken over it. Nothing is sent from it. (Sent as it stood here, it would have wiped the
     parts set on its chips there.) A stack is never taken with a snapshot missing, so it is
     never sent back shorter than it was. A song is only ever taken.
     *Known edge:* a snapshot deleted here that a song lit there still names comes back with
     the song, as it did at LOAD: now within a quarter of a minute instead of at the next LOAD.
     Unlight the song there, or take the part out of it, to be rid of the snapshot here.

## A push that turns the knob (Nov 14, 17:00)

490. **You**, 17:00: "when attemptiong to open a lanes effect configurtion via the e16 ir
     doesn't work. and navigates to a different lane instead."
     **Mine, from this afternoon (479).** So that 13 and 16 together could be SNAPSHOT, each
     was made to act as it is *let go*. A push on an encoder turns it a click as often as
     not, and between going down and coming up that click was now counted:
     - knob 13 pushed to choose the effect the pointer was on: the click moved the pointer on
       to the next stop first, and with one effect on the lane the next stop is the next lane.
       That is the lane you were sent to;
     - knob 16 pushed for OPEN with the pointer up: on a lane's page knob 16 is HIGH, the click
       was "the hand has gone to another knob", the pointer was put down, and OPEN then opened
       the lane's CONFIG and not the effect's;
     - and either click nudged SEND A or HIGH.
     **Now a knob that is being pressed is not being turned.** What 13 or 16 says of its turn
     while it is down is noted (the knob is told its own value again) and moves nothing. A
     click of knob 16 a moment *before* its press (the push on its way down) no longer puts the
     pointer down either: the pointer waits a quarter of a second for the press. Knob 16 turned
     with no press after it puts the pointer down as before.
     *I reproduced this with a made-up E16 whose pushes turn the knob, not with yours.* It is
     the fault that fits what you said, and the check fails without the change and passes with
     it. If it still happens on the E16, tell me which knob you pressed and what the E16's
     display said just before (`> 6.1 DELAY`, say): then it is something else.

## OPEN on an effect, and a snapshot that sounds as its clip (Nov 14, 18:20)

491. **You**, 18:19: "still the same selection problem. when I scroll to an insert effect and
     selct it it works. but if I hit the configure button on the e16, it starts to open and
     then closes and then jumps to a different lane selected (in this case from pad to chord)."
     **So 490 was not it** (it was a real fault, and stays fixed). What "pad to chord" says:
     CONFIG's first page is the ninth page of the E16's second scene, and CHORD's lane is the
     ninth page of its first. OPEN on an effect asks for that page by a Program Change alone.
     If the E16 answers "page 9" in the *first* scene's numbers, SF-000 took it for your hand
     turning the E16 to CHORD: CHORD came to the front, and since this afternoon (478) another
     lane coming to the front shuts CONFIG. That is the open, the shut and the jump, in that
     order.
     **Two rules now.** (1) The page asked for, said back in the other scene's numbers within
     two and a half seconds, is the E16 having been in the other scene than SF-000 took it to
     be in. Nothing follows it; it is sent again with the scene said. (2) Any page the E16
     says in the two and a half seconds after OPEN opened CONFIG is its way there, not a
     hand's turn: CONFIG stays, nothing comes to the front, it is sent to CONFIG's page once
     more.
     *What I do not know: why your E16 answers in the first scene's numbers there.* With a
     made-up E16 that does, the old build does exactly what you describe and this one does
     not. So that the next one is not a guess:
492. **`e16.log`.** *Mine.* Every page SF-000 asks of the E16 and every page the E16 says, with
     the milliseconds between, and what SF-000 made of it (`OPEN PRESSED, WITH 10.1 DELAY IN
     FRONT`, `SENT TO SF-FX CFG 1 (PROGRAM 8 ALONE)`, `E16 SAYS 9 (SF-000 PAGE 9), 381 ms
     AFTER...`, `TAKEN AS A HAND'S TURN...`). In `~/Library/Application Support/SF-000/`, kept
     small. If the E16 goes somewhere it should not, the last twenty lines of it say why.
493. **You**, 18:21: "also, something is a bit off with the snapshots. If I select a snapshot and
     all the lanes, i want it to sound exactly like it does in the ausio preview. But sometimes
     the snapshots are a bit different. Is there something we're not capturing?"
     **One thing was wrong, and it was the macro knobs.** A snapshot holds each knob as it
     *sounded* (so does its clip), with whatever the macro knobs were adding in it. Put back,
     it was written to the scene *under* the macro knobs, and they added to it again. So a
     snapshot recalled with ENERGY still up was brighter than its clip, one recalled with it
     down was right, and one taken and recalled at once was pushed twice. That is "sometimes".
     **Now a snapshot put back is what sounds**, as a knob under your hand is: recalled, come to
     on a stack knob, or pressed to between two, each knob is where the clip heard it, whatever
     the macro knobs add at that moment. The macro knobs stay where they are and are still
     yours to turn: let down from there, ENERGY takes away what it was adding (the knob goes
     under the snapshot).
     *This changes 439's "macro knobs stay on top of whatever a stack knob does":* on a stack knob's snapshot the set
     used to be that snapshot plus what the macro knobs added. *Mine. The other way to have had
     it exact:* send the macro knobs home on a recall, so rest always means "the snapshot".
     Say if you would rather that.
494. **What a snapshot does not hold, on purpose, and so can differ from its clip:**
     - **MUTE and SOLO** (lanes, effects, returns): 39. The clip has them as they were; a
       recall leaves them as they are now. (A lane's *sequencer* mute is the one exception,
       with NOTES lit: 289.)
     - **TEMPO.** **The master's TAPE, COMP and LIMIT** (281). **GLIDE** itself.
     - With **NOTES** unlit, no steps. With only the lanes' keys lit and not R, B and K: not
       the returns, the drum bus or the key.
     - With **GLIDE** on, the first bars are the road there.
     - What cannot be held: a delay's or reverb's tail in the air, where a pattern of an odd
       length is in its round, chance (DENSITY, ECHO PROB), the ONE still playing a lane.
     *Not changed.* MUTE is the one I would put in if you want the clip exactly: say, and a
     recall sets the mutes of the parts it takes.

## Three objects: chips, knobs, modules (Nov 14, 19:30)

495. **You**, 19:30: "I think of the snapshot module as a collection that can be loaded into the
     chassis. Both library and macro Uis should allow me to create multiple snapshot modules
     that can be loaded into the current lane. that way i can create modules for different
     sets, etc. So the library would contain collections of premade modules that can be loaded
     into the active chassis. Tghe library does a few things: 1. it l;ets me rename, tag,  and
     delete snapshots. it lets me organize snapshot chips into library stacks to be able to
     find good sounds later when building module racks (this is the dynamic stacks and manual
     stack idea). 2. it lets me build macros (macro and stack knobs) which can also be saved in
     a collection. 3. it les me build module racks that are made up of macro and stack knobs.
     So we have three conceptual objects: Soundchips organized in stacks, macros, and modules.
     The load function simply puts a module that has been saved, into the active lane on the
     instrument. Soes that mae sense? So eventually I'll have a library of organized chips,
     premade macro and stack knobs, and modules."
     **The model, as I read it.** Three kinds of thing, each kept in the library, on the
     instrument and on the web alike:
     - **Chips** (snapshots), put into **library stacks** to be found again: by hand, or filled
       by a rule. A library stack is for finding, and is never played.
     - **Knobs**: a macro knob (tied to the rig's knobs) or a stack knob (chips in an order,
       each with its parts). Built in the designer or in the library, and kept in a collection
       of their own.
     - **Modules**: a named set of knobs in their places (four macro knobs, ten stack knobs).
       Many of them, one for each set. **LOAD puts a kept module into the snapshot module's
       place on the instrument.**
     **Where what is built differs** (nothing built for this yet):
     - There is one module, not many: the instrument's sixteen knobs, and the web library's
       one model of them. LOAD brings that one.
     - One object, a "stack", does three jobs on the web: a pile for finding chips, a stack
       knob once it is on the module, a macro knob when it holds ties. The rack kept the same
       both ways today (485) is that object with its lamp lit.
     *Proposed, to be agreed:* a module holds its own knobs (a knob put into a module from the
     collection is a copy, so a module is whole by itself and changing one never changes
     another); the module on the instrument is always one of the kept modules, changed as you
     play, so LOAD of another never loses it; modules and knobs are kept the same here and on
     the web by ONLINE, as the rack is, so LOAD lists them with no network.
     *Asked:* "into the current lane" and "the active lane": read as the snapshot module's own
     place in the chassis, not a module for each lane.
496. **You**, 19:32: "chips is a term thats interchangeable with snapshot. A chip is the snapshot,
     a stacks is a collection of chips."
     **The words, then.** A **chip** is a snapshot: one thing, two names. A **stack** is a
     collection of chips. A **knob** is a macro knob or a stack knob. A **module** is a set of
     knobs in their places. (495's "library stacks" are just stacks.)
497. **You**, 19:36: "Explain the last question? There's only one lane for the snapshot module.
     so the rig loads one module at a time, choosing from a library of modules. But it loads an
     entire module (4 macro knobs, 10 stack knobs)"
     **Settled.** One module at a time for the whole rig, chosen from a library of modules, and
     LOAD brings all of it: its four macro knobs and its ten stack knobs. (I had asked because
     "loaded into the current lane" could also have meant a module of its own for each lane.
     It did not: the lane is the snapshot module's own place in the chassis.)
     *Next, not built:* modules kept under names on the instrument (the one in the chassis is
     always one of them), and LOAD choosing among them; then the same on the web, kept the
     same by ONLINE; then the knob collection. The three proposals of 495 stand as proposed.
498. **You**, 19:39: "We can hold off on web for now and wait till we get the instrument library
     working perfectly, then we'll work on the web version which is really about the ability to
     do more complex analysis off of the instrument. Give me a list of what I need to test now."
     **The instrument first.** Modules, the knob collection and the one page are built on the
     instrument alone, with no network, until they are right. The web library is left as it
     is: nothing more is built there, and nothing in it is changed. What it is for, when its
     turn comes: analysis too heavy for the instrument. (What is already built still runs with
     ONLINE lit: snapshots sent, the knobs mirrored, the rack kept the same. With ONLINE out,
     none of it does.)

## What the test found, and CONFIG as the E16 has it (Nov 14, 19:50)

499. **You**, 19:49: "1. No notifiction when snapshot taken but it did take a snapshot." and
     "other than that, it works."
     **So the rest of the list is as it should be on your Mac and E16**: OPEN on an effect, the
     pointer's choice, 13 and 16 together, CONFIG shutting, a snapshot as its clip, new notes in
     the key, the column, the rack, MACROS from the library, LOAD.
     **A snapshot taken is now said on the top bar, with its name** (`SNAPSHOT TAKEN: SNAP 012`),
     for a few seconds, by whatever hand took it: the SNAPSHOT key, S, the E16's 13 and 16.
     Until now the only sign on the screen was the small LAST time on the snapshot module. The
     E16's own display is sent SNAPSHOT TAKEN as before; it shows it only if its firmware shows
     the names SF-000 sends, which I cannot see from here.
500. **You**, 19:55: "Request: on the config pages, can we make the layout for the knobs the e16
     can control match the 4x4 layout. The helps me with muscle memory."
     **CONFIG's panel is four across now, in the E16's own order.** Its first twelve controls
     are the three top rows of the E16's first CONFIG page, place for place; the next twelve
     its second page's; and so on. Under each twelve stands the E16's bottom row there: SEL,
     PAGE -, PAGE +, SHUT, with the page's number (lit while the E16 is on that page). The panel
     goes to the page the E16 turns to, and the wheel over it moves a page at a time.
     *Mine:* the boxes that say what a run of controls belongs to are kept, and now break at a
     row's end (a box of six is four and two). The order of the controls is unchanged: it was
     already the E16's. *Not done:* the order within a module is still the module's own, so a
     box may straddle two pages; say if a module's CONFIG should be regrouped to fit pages.
501. **You**, 20:05: "and a button to reset the instrument to a base starting point with no midi
     notes and knobs centered for starting a new track."
     **HOLD: NEW**, on the top bar beside the SF-000 flag. Held for most of a second (a tap does
     nothing: it is not a key to brush past in a set), the rig is as it starts:
     - no steps in any lane, and every generator let go;
     - no effects in the racks;
     - every knob where the rig starts it (that is CLEAN SLATE, which was already in the
       library: this is that, whole and at once, whatever NOTES and GLIDE say), KEY off;
     - every MUTE and SOLO out, the macro knobs at rest, nothing cued;
     - the notes heard from outside forgotten, so the next snapshot does not carry the last
       track's.
     **It leaves:** TEMPO, GLIDE, the master (TAPE, COMP, LIMIT), the snapshot module's knobs,
     the library, the arrangement, and each lane's sample.
     *Mine:* "knobs centered" read as where the rig starts each knob, not the middle of its
     turn (a LEVEL in the middle of its turn is not a starting point). It takes no snapshot
     first: take one if what is playing is worth keeping. The key is not there while a knob is
     being taught or INFO is up (their words stand in that place). Nothing on the E16 for it
     yet: say if it should have one.

## Modules, kept on the instrument (Nov 14, 21:30): fifty-first build

502. **You**, 21:32: "carry on"
     **The first of 497's three: modules kept under names, on the instrument alone.** No
     network is asked, and nothing was built on the web (498).
     - **A module** is the snapshot module's knobs as a set (its four macro knobs, its ten
       stack knobs, and MOTION as it is set on them), under a name.
     - **The one in the chassis is always one of the kept ones.** What you change while playing
       changes that module. A session from before this has one: MODULE 1, its knobs as they are.
     - **MODULES**, on the designer's strip (right of MOTION, with the module's name), shows
       them where the column and the library stand: each a card with its number, its name and
       what it holds; the one in the chassis says so. A card pressed is the chosen one.
     - **LOAD** puts the chosen one in the chassis, whole. The one that was there is kept as it
       stood. **COPY** keeps the one in the chassis again under another name, and the chassis
       holds the copy from then on (change it for another set: the first stays as it was).
       **NEW** is one with nothing on its knobs. **RENAME**. **HOLD: OUT** takes the chosen one
       out of those kept; never the one in the chassis.
     - **LOAD on the snapshot module's own panel now opens that list.** It was the web
       library's LOAD; that one is **WEB LOAD**, in the library, and does what it did.
503. **Nothing is heard to change when a module is loaded.** *Mine.* The macro knobs that
     leave may have been adding something: that stays in the sound (it becomes the scene), and
     the new module's knobs start at rest. A stack knob of the new module starts at the bottom
     of its turn with the set wherever it is; its first turn goes from there, as always (439).
504. **A copy is its own.** *Mine, the first proposal of 495.* A module holds its knobs
     outright. COPY's knobs are not the web library's modules that the first one's knobs are:
     with ONLINE lit they go up as new ones. (The web still has one model of the module: what
     is in the chassis. Loading another module here changes that model, as changing sixteen
     knobs by hand would. That is as far as the web goes until its turn comes.)
505. **The paragraph on the designer's strip is gone**, to make room: the one that said what
     each kind of knob does. Each key still says what it does when the pointer rests on it.
     *Not built:* the knob collection (a macro or stack knob kept by itself, to put into a
     module); LIBRARY and MACROS as one page; anything for modules on the E16.

## Knobs, kept by themselves (Nov 14, 22:00): fifty-second build

506. **You**, 22:02: "continue"
     **The second of 497's three: a knob kept by itself, to put into any module.** On the
     instrument alone; no network is asked and nothing was built on the web (498).
     - **KNOBS**, a third plate beside the chosen knob in the designer (with SNAPSHOTS, and
       STACKS for a stack knob), shows the knobs kept: each a card with its number, its name
       and what it is (MACRO KNOB, TIED TO 13 KNOBS; STACK KNOB, 5 SNAPSHOTS: their names).
     - **KEEP KNOB n**, at the head of that list, keeps the chosen knob under its name, as it
       is: a macro knob with every knob it is tied to and how far it takes each, a stack knob
       with its snapshots and the parts each takes. Kept again under the same name, it is the
       same one as it is now. A knob that holds nothing is not kept; one with no name is kept
       as MACRO 3 or STACK 7.
     - **PUT**, on a kept knob's card, puts it on the chosen place of whatever module is in
       the chassis. It shows only where the kind fits: a macro knob on 1 to 4, a stack knob on
       5 to 12, 14 or 15. Its number takes it out of those kept.
     - In the session file (`keptKnobs`), so a knob kept while one module is in the chassis is
       there for every other.
507. **PUT only goes onto a place that holds nothing.** *Mine.* Onto one that holds something
     it says KNOB 5 HOLDS SOMETHING: CLEAR IT FIRST, and changes nothing. A press that wiped
     out a knob you built would be the worst thing this page could do; CLEAR is one key away
     and is your own doing. The knob put starts at rest and nothing is heard to change.
508. **A kept knob is a copy, and so is the one put.** *Mine, as 504.* What you change on the
     place changes the module's knob, not the kept one (KEEP again to make it so). Taking it
     out of those kept leaves every module that has it alone.
509. **KEEP is at the head of the list it goes into; the strip's KEEP is gone.** *Mine.* The
     strip had KEEP for a stack knob only (it kept the knob's snapshots as a stack), in the
     place LEARN has for a macro knob, and there was no room there for a second one. Now both
     lists start the same way: KNOBS with KEEP KNOB 5, STACKS with KEEP KNOB 5'S STACK. One
     keeps the knob to put on a place; the other keeps its snapshots in their order, to add to
     any stack knob (and, with ONLINE lit, they go to the web library's rack as before).
     STACKS and their sync are otherwise as they were.
510. **Beside a macro knob the kept knobs are two across**, as the snapshots are there; beside
     a stack knob's column, one. The snapshots beside a macro knob now start under the plates
     (eleven rows of two, where there were twelve).
     *Not built:* stacks made in the library without a knob; LIBRARY and MACROS as one page
     with find; anything for modules or kept knobs on the E16; dragging in the column.

## Stacks of their own, and FIND (Nov 14, 23:20): fifty-third build

511. **You**, 23:22: "continue"
     **The next two in the order: stacks split from knobs, and find.** On the instrument
     alone (498). 495's first job of the library, as far as it goes without tags: "it lets me
     organize snapshot chips into library stacks to be able to find good sounds later".
512. **A stack is made and changed with no knob.**
     - **NEW**, at the head of STACKS (beside KEEP), makes an empty stack, keeps it and
       **opens** it.
     - **An open stack is in the column**, where a stack knob's own stack stands: the same
       cards, the same keys for the parts, UP and DN, a number to take one out, a tap to hear
       one. **ADD**, on a snapshot, puts it into what is open: the chosen stack knob, or the
       open stack. No place is ringed while a stack is open, the strip says STACK, and RENAME
       names it. **Choosing a place puts the stack away.**
     - **A stack's card** now has: its number (out of those kept), **OPEN** (into the column,
       to be changed), **ADD** (all of it into what is open: a stack knob, or another stack),
       and **the card itself: its snapshots are the ones listed under SNAPSHOTS**, to hear and
       to add from. That is the finding: a stack is a shelf you can look along.
     - STACKS is beside a macro knob too now (its stacks have no ADD there: a macro knob takes
       no snapshots).
513. **The head of the snapshots**, wherever a page lists them (the library, the designer, the
     arrangement): one row, two where it is narrow.
     - **Whose are listed**: ALL, or a stack's name, and how many. Pressed: the next stack,
       then all again. (It replaces the library's STACK key, which did the same from the
       strip.)
     - **RECENT, FITS NOW, MORE, LESS**: the E16's four finders (132), on the screen. RECENT
       is the newest first. FITS NOW is nearest in sound to what is playing, first. MORE and
       LESS are the twelve a step up or down in energy, as the E16's knobs hold. The set is
       listened to when one is pressed and the list stays put; pressed again, they are as kept.
     - **FIND**: part of a name, typed. Pressed while lit: all of them again.
     The three work together: a stack's snapshots, nearest what is playing first, whose names
     hold DUB.
514. **One list for three pages.** *Mine.* Whose are listed, the finder and FIND are the
     same on the library, the designer and the arrangement: a stack chosen on one is the one
     listed on the others. The library page gave up a row of cards to its head (66 where there
     were 72), and the arrangement's list one too.
515. **An empty stack is the instrument's alone.** *Mine.* The web library is told nothing of
     a stack until it holds something (it would put an empty one out of the rack, and the
     instrument would then lose it). A stack made here is kept in the session file empty or
     not. A stack that is in the column and changed there goes to the web library's rack as
     any change to a kept stack already did, with ONLINE lit; nothing new is asked of the web.
516. **Which gesture is which.** *Mine, and the one to tell me if it is wrong.* A stack's card
     pressed lists its snapshots (finding is what a stack is for); changing it is a key
     (OPEN). The other way round was the alternative.
     *Not built:* tags on the instrument; stacks that fill themselves by a rule; the plates
     (STACKS, KNOBS, MODULES) on the wide library page, which is what is left of "one page";
     the E16 on any of it; dragging in the column.

## In hand, and carried (Nov 15, 00:50): fifty-fourth build

517. **You**, 00:48: "continnue"
     **Your own workflow for building a stack (434), on the instrument.** You had said it for
     the web library: "I preview chips, deselect stems I don't want, drag that onto stack knob
     column ... In that column i car add, delete, or reorder the chips. I can select/deselct
     the stems for each chip (ideally on the chips themselves so its visible which lanes are
     active) and then, when I like the stack, i can press a button on the column that adds
     that stack to an available module slot." Build 53 made the column hold a stack that is
     on no knob; this is the rest of it. I put it ahead of the wide library page because it is
     yours and that one is mine.
518. **The snapshot in hand carries its keys.** Beside the knob, the snapshot last pressed is
     the one in hand: its card is ringed, and where the others draw their sound it has a key
     for every part of the rig (the eleven lanes, R, B, K). A part put out is not heard (at
     once, while it plays in the headphones) and does not go with it when it is added. The
     next snapshot pressed is in hand, whole.
519. **Cards are carried.** Pressed and moved, a card goes with the pointer (its name on a
     yellow plate), and is put where it is let go. Where it would land shows first: a yellow
     bar between two cards of the column, or a yellow ring on a place.
     - **A snapshot of the list**: into the column, between the cards where it is let go; or
       onto a stack knob's place, at the end of that knob's stack. With the parts lit on it.
     - **A card of the column**: to another place in the column, its parts with it. **UP and
       DN are gone.** Onto another stack knob's place: a copy goes there, and the column keeps
       its own. Off the column: taken out (its number still does that too).
     - **A stack's card**: onto a stack knob's place, or into the column, whole.
     - **A kept knob's card**: onto a place of its kind that holds nothing.
     A press that does not move is what it was: a snapshot is heard, a stack's snapshots are
     listed. ADD and PUT stay, for one press.
520. **ON A KNOB**, at the head of the column while a stack of the shelf is open: the stack
     goes onto the first stack knob that holds nothing ("an available module slot"), that
     place becomes the chosen one, and the stack is put away, still kept. With none free it
     says so.
521. *Mine:* a card carried off the column is taken out wherever it is let go that is neither
     the column nor a place. If that proves too easy to do by accident, it is one line to make
     it only the number.
     *Not built:* tags on the instrument; stacks that fill themselves by a rule; the plates on
     the wide library page; the E16 on any of this page; carrying on the library page and the
     arrangement (their cards are still only pressed).

## A chip pressed plays, and carries its keys (Nov 15, 08:00): fifty-fifth build

522. **You**, 07:55: "I need to be able to preview the snapshots on thr macro  page"
     **You**, 08:05, after I said what might be the cause: "that was it. headphones was off
     and no button."
     **The preview was there and could not be heard.** A snapshot pressed in MACROS played in
     the headphones, but PHONES is off every time SF-000 opens, its key was only on the
     library's strip, and nothing said so.
     - **PHONES is on the MACROS strip** (its right end), the same key as the library's.
     - **A press that will not be heard says why**, in the bar: the headphones have nowhere to
       play (press PHONES or H, or tick outputs 3 + 4), or no sound was kept with that
       snapshot.
     - *My fault, found on the way:* a stem key lit again on the snapshot in hand (build 54)
       left the headphones silent until the snapshot was pressed twice. The checks had only
       used snapshots with no sound kept, which pass on silence. There is now a check with a
       real snapshot and its sound, listened to at the device's outputs.
523. **You**, 07:59: "anywhere the interface shows sound chips, those should be playable and stel
     selectable."
     **One chip, on every page.** Pressed, it plays in the headphones; it is then the one in
     hand, ringed, with a key for every part of the rig on its own card; what is put out is not
     heard and does not go with it in what is done next.
     - **MACROS**: the list and the column already did this (518). **The small numbered chips
       on a stack knob's card play on a press** now, with the parts they take there, and the
       place is chosen so their keys are in the column. *A press there used to take the chip
       off the stack, with no warning: that is gone.* Taking one off is in the column.
     - **The library**: a press plays the chip and puts its keys on it. **RECALL** takes the
       one in hand to the set with the parts lit; two quick presses on the card do the same.
       **The CUE key and the row of RECALL TAKES keys on the strip are gone**: the keys are on
       the chip, and a press never changes the set. *This changes "tap recalls unless CUE is
       lit", which was yours (the first library). I took your sentence to outrank it; say if
       one-press recall should come back as a switch.*
     - **The arrangement**: the chips at the right play on a press and carry their keys; their
       **ADD** makes a part (a press used to). **A part has the keys on its own card**, and
       takes only those parts of its snapshot when the arrangement comes to it, in PRINT too. A
       part pressed is chosen and plays with them. The part cards are taller for the keys:
       seven rows on the page where there were eight.
     - What something else puts in the headphones (a stack knob with CUE lit, a card of the
       column) is the one in hand on whatever page lists it.
     - Leaving a page stops a snapshot that was pressed on it.
524. **You**, 08:48: "in the last build, the setup button became phones again"
     **You**, 08:50: "figured it out. setup button changed to phones when i use the H key
     command."
     **SETUP stays SETUP.** That was mine (187: the key turned green and read PHONES while
     PHONES was on), and a key that changes its name reads as a fault. Now the SETUP key is
     white and says SETUP whatever is lit, and **PHONES is a small plate of its own under
     SETUP's name, on every page**: putty while it is out, green while it is lit, and pressed
     it turns PHONES on or off. So there is a PHONES key wherever a snapshot can be heard,
     MAIN and the arrangement included. The keys on the library's strip and the MACROS strip
     stay. PHONES is still off each time SF-000 opens.
     *Not built:* a song taken from the web library does not bring per-part stems; the E16 on
     any of this; stems on the snapshot module's knobs on MAIN (they show names, not chips).

## A sample as it was recorded (Nov 15, 09:20): fifty-sixth build

525. **You**, 09:19: "for samples, sometimes they seem really quiet even though they play fine
     on other apps. They also sound quite different. What settings to get a sample to sound
     exactly as it does in the waveform?" 09:20, with a file (a four-second bass, two notes,
     nearly all under 120 Hz, its loudest point at -9.6 dB): "this one for instance". 09:21:
     "in the bass module it lacks a lot of the low end and complexity when played back vs the
     original sample"
     **Measured, with your file in the BASS lane as the rig opens** (the file is not in the
     repo). Five things stand between the file and the lane's sound:
     - the sample's own lows are crossed out and a sine put in their place (SPLIT at LAYER
       LOWS 100 %, SPLIT HZ 120, LAYER MODEL SINE SUB): 20 to 60 Hz is 54 dB down, and SPLIT
       does this with the LAYER knob at nought too;
     - the steps are C1 and the file's root reads as D#0, so it is nine semitones up;
     - GATE at 40 and steps half a step long: about 0.2 s of a 4 s note is heard;
     - CUTOFF 30, RES 35, ENV AMT 45: 23 to 48 dB down above 250 Hz;
     - LEVEL 56 is 15 dB under the file (100 is 2 dB under), the steps' velocities take 2 to 4
       more, and nothing normalises a sample.
     With each put where it does nothing, the lane is level with the file in every band to a
     tenth of a decibel.
526. **PLAIN**, beside LOAD in the CONFIG of a lane that plays a sample: one press sets the lane
     to play it as it was recorded, and says what it did. *The key and its name are mine.*
     What it sets is in the README, lane by lane, and each set was found by measuring that lane
     against the file, not from the code: BASS, LEAD, CHORD, PAD; KICK, the hats, CLAP; VOICE.
     On the lanes where the note is the pitch it sets TUNE so that the lane's first lit step
     plays the sample at its own pitch. It leaves LEVEL, the sends and the inserts, and names
     the inserts that still colour the lane.
527. **CONFIG says what the file is**: PEAK (its loudest point) and ROOT, beside its name.
528. **`--samplecheck`**: the tool the measuring was done with, kept. It plays a file through
     a lane and writes the lane and the outputs as WAVs with every setting of the lane printed.
529. *Left as they are, and yours to say:*
     - **SPLIT takes a model's lows out with LAYER at nought.** It is the suite's rule (the
       crossover says who owns the lows, whatever the layer's level), and the BASS lane opens
       with it on so that its synth models sit on a clean sine. For a sample it is the worst of
       the five. I did not change the suite's code; PLAIN turns SPLIT off.
     - **A sample cannot be made louder than its file**, and on the synth lanes cannot quite
       reach it at the lane (2 dB short at LEVEL 100). A gain or a NORMALISE for a lane's
       sample would be a new control in the suite's modules.
     - **A sample on BASS, LEAD, CHORD or PAD sounds only while its note is held** (it fades
       when the note is let go). KICK, the hats, CLAP and VOICE play it through to its end from
       the shortest step, so today a loop that should simply play belongs on VOICE, in mono. A
       switch to let a synth lane's sample play through would be another new control.
     - RUMBLE has no PLAIN: it plays a sample for one beat's gate and shapes it.

## A sample's own controls (Nov 15, 10:20): fifty-seventh build

530. **You**, 10:20: "definitels a sample gain and normalize. And is there anything else we
     can add to make samples more "playable" and reactive to the module standard controls"
     10:21: "wasn't there a wavetable model?"
531. **GAIN and NORMALISE**, in CONFIG's SAMPLE box, on every lane that plays a sample (BASS,
     LEAD, CHORD, PAD, KICK, the hats, CLAP, RUMBLE, VOICE). GAIN is 24 dB either way, before
     DRIVE and the filter. NORMALISE brings the sample's loudest point to full scale. *Peak,
     not loudness, is mine: it is what the word means in every editor, and it never clips.*
     Measured with your file: GAIN +6 is +6.0 dB in every band, NORMALISE +9.7 (its PEAK reads
     -9.6). Both open where they change nothing; PLAIN leaves them, as it leaves LEVEL.
     A snapshot holds all four of a sample's controls. One taken before this build says nothing
     of them and means them as they open, so recalling it puts them back there (as is done for
     MOTION): an old snapshot does not come back louder because NORMALISE was on when you
     recalled it.
532. **What was asleep.** On BASS, LEAD, CHORD and PAD a sample ignored four of the lane's
     standard controls: DECAY and ENV AMT (the SAMPLE model sets its envelope flat and its
     filter envelope to none), and DETUNE and COLOR (nothing in a sample reads them). And it
     stopped when its note did (529).
533. **PLAY: HELD or THROUGH** (the four synth lanes). *Mine.* THROUGH is the switch 529 said
     was missing: a note plays the sample to its END however short the note. **PLAIN now sets
     THROUGH**, so a step plays the whole sample on these lanes as it does on a drum's.
534. **ENVELOPE** (the four synth lanes). *Mine.* On, DECAY and ENV AMT act on a sample as on a
     synth model; DECAY at full is no decay. It opens off, because the lanes open with DECAY
     40 and ENV AMT 45 and a sample loaded would at once be a pluck through a moving filter;
     and off is what every sample in a snapshot of yours was made with.
535. **Yes: WAVETABLE is a model of PAD's**, and so is GRANULAR; both use the loaded file
     (WAVETABLE with TABLE at SAMPLE: eight waves from it, COLOR across them). Checked by
     playing your file through them. BASS, LEAD and CHORD do not have them.
     *Not built, and yours to say:* the two models on BASS, LEAD and CHORD (each lane's models
     are a list with a loudness figure for each, and a snapshot keeps a model as how far down
     the list it is, as a fraction: a longer list moves every model already saved on that lane
     unless the snapshots are converted, so it is a change to make on purpose); DETUNE and COLOR
     on a sample (a second, detuned reading; COLOR as where in the sample a note starts); a
     sample that loops between START and END while its note is held; ROOT set by hand.

## WAVETABLE on every synth lane; every main knob on a sample (Nov 15, 11:49): fifty-eighth build

536. **You**, 11:49: "i'd add wavetable to the other lanes, and I'd make sure that every main
     knob does something to the sound. Also, I nned a way to play things at transitions.
     What's a good way to do that (like a stab or riser when hihats come in, etc.)"
537. **WAVETABLE is a model of BASS, LEAD and CHORD** as it is of PAD (TABLE: its own waves, or
     eight from the loaded sample; COLOR across them). It is second to last in each list, so
     the first and the last are who they were, and **a model kept in a snapshot from before is
     the same model**: a kept place falls on the shorter list's steps, which but for the two
     ends are never the longer list's, and is read as that entry. Nothing saved is rewritten.
     Its loudness on each lane is set by rms beside the lane's other models over its own
     pattern (BASS -17.4 dB among -16.5 to -18.3), *not by LUFS as the suite's figures are*.
     One voice on BASS. GRANULAR stays PAD's alone: you asked for WAVETABLE.
538. **Every main knob, with a sample.** Measured by `--audit` (each of a lane's twelve knobs
     down and up, the difference against the lane's sound; over about -35 dB is plain to hear),
     with a made tone as the sample. Asleep before: DECAY and ENV AMT on BASS; COLOR, DECAY and
     ENV AMT on LEAD; DETUNE, DECAY, ENV AMT and SPACE on CHORD; DETUNE, COLOR, DECAY and SPACE
     on PAD; TONE on the hats and CLAP; TONE and SIGNAL on VOICE; SIZE and WIDTH on RUMBLE.
     Now, on the four synth lanes, **KNOBS** (the switch that was ENVELOPE, 534) opens **on**
     and wakes all of them: DECAY (to silence; the top fifth of the knob raises the sustain to
     all of it, so there is no jump at full), ENV AMT, COLOR (*a tilt, darker or brighter about
     the centre: mine; it could have been where in the sample a note starts*), DETUNE (a second
     reading up to 30 cents off) and SPACE (the ensemble), the last two not on BASS; and with
     PLAY at THROUGH, GATE is how much of the sample plays. TONE tilts a sample on the hats,
     CLAP and VOICE. PLAIN leaves KNOBS on and puts each knob where it does nothing, and after
     it each lane is still level with the file in every band.
     After: BASS, LEAD, CHORD, PAD, the hats and KICK have no knob asleep with a sample but
     GLIDE on BASS, which needs two different notes to slide between. With WAVETABLE: none on
     BASS (but GLIDE), LEAD or CHORD; on PAD, DENSITY reads -36 (its echoes of long notes).
539. *Still asleep with a sample, and yours to say what they should do:* **SIZE and WIDTH on
     RUMBLE** (a sample there is one mono thump into DARK, GRIT and PUMP: SIZE could be a room
     behind it, WIDTH the sample's own stereo) and **SIGNAL on VOICE** with MODEL at SAMPLE
     (SIGNAL is how clean RADIO and the vocoders are; the phrase as it is has nothing to
     clean). Each opens away from nought (SIZE 60, SIGNAL 60), so whatever they did would
     change every RUMBLE and VOICE sample you have saved.
540. **What this changes in sounds you may have.** A snapshot taken before KNOBS existed
     recalls with it off, and a synth lane's sample sounds as it did. Not kept: **PAD's
     WAVETABLE** (DETUNE now thickens it, and it settles to 0.7 of its level so that DECAY is
     heard); **a hat, clap or VOICE sample with TONE away from 50**; and **the rig as it was
     left** (not a snapshot) if a synth lane was playing a sample: it opens with KNOBS on.
541. **Transitions: proposed, not built.** See the reply of 11:49. In short: a TRANSITION lane
     in one of the three empty places, holding a few one-shots that are each a LEAD-IN (ends on
     the boundary: a riser, a reversed hit, a swell) or a HIT (starts on it: a stab, a crash, an
     impact); fired by one key, and timed by the machine to the next boundary (4, 8 or 16 bars),
     so the lead-in ends and the hit lands exactly where the change does; and able to be armed
     on a recall or an un-mute, so that the change itself waits for the boundary.

## TRANSITION (Nov 15, 13:16): fifty-ninth build

542. **You**, 13:16: "build transition how you think best and then audit." 13:57: "for the
     wavetable, how do i move through thr table?" 13:58: "table position i mean"
     (Answered then: COLOR is the place in the table, a main knob on LEAD and PAD and in CONFIG
     on BASS and CHORD; each note also wanders 15 % about it; a MOTION can move it.)
543. **TRANSITION, as built.** *All of it mine, by 536 and 542.* Not a lane: two one-shots of
     the engine's own, a LEAD-IN that ends on a bar line and a HIT that starts on it, into the
     mix before the master and by two amounts into the returns. **FIRE** (T) sets them off for
     the next line, every 4, 8 or 16 bars (under a beat from one: the one after); stopped, it
     is the hit at once. **ARM** (G): the next recall, mute or solo sets the transition off and
     waits for its line, with whatever else is asked for meanwhile, and ARM goes out. A recall
     is made inside the beat before the line, as the arrangement makes its own, and travels by
     GLIDE from the line; a mute or a solo is made a moment (0.06 of a beat) before it, so that
     the line's own first hit is heard. Built in: a RISER (noise in a band that climbs from
     180 Hz to 9 kHz, growing as the square of the way) and an IMPACT (a sine falling from 120
     Hz to 42 under a burst of dark noise). Or a file each, dropped on its plate.
     It is in the bar beside HOLD: NEW on the main and the macro pages. *A note in the bar now
     stops at FIRE and ARM (it may cover the plates beside them) and a longer one is cut short.*
     Measured: the hit lands 181 samples after its line at the outputs whichever line and
     whenever pressed, and a lead-in's file ends 181 after it too; that is the master's own
     delay, the same for everything in the mix.
544. *Not built:* FIRE and ARM on the E16 or a pad of the OXI; a file chosen without the mouse;
     more than one lead-in and one hit; ARM for a stack knob or a macro knob turned; a lead-in
     stretched to the time there is (a file plays at its own speed).

## Every model audited; an outside audit's findings (Nov 15, 14:53): sixtieth build

545. **You**, 14:53, with a file (SF-000-audit-52d5284.zip, a report and its probes, written by
     ChatGPT against build fifty-nine): "while that's running, heres a chatgpt audit."
546. **The audit of every model (542).** Each lane's twelve knobs, down and up, for every model
     the lane has: 85 lane-models (OPEN HAT and CLAP are CLOSED HAT's module and were not run
     again). It listens to the two outputs added together, so **a knob that only moves the
     sides reads as asleep**: RUMBLE's WIDTH on REVERB, DELAY, DISTORTED and DUB BOOM (-63 to
     -73 dB, where a knob that does nothing at all reads -170 or under) is that, not a fault.
     Nothing asleep: KICK (7 models), PERC (6). What is asleep, by its cause:
     - **A model with no filter**: RES, ENV AMT and CUTOFF on ORGAN, FM BELL and FM 4-OP (LEAD
       and CHORD), on PAD's FM 4-OP, and with DECAY too on BASS's SINE SUB.
     - **A model with no ensemble**: SPACE on CHORD's ACID, MONO 101, MS20, FM BELL, SYNC and
       FM 4-OP, and on PAD's GRANULAR, TAPE LOOP and NOISE BED.
     - **A model that reads no DETUNE or COLOR**: DETUNE on CHORD's ACID, ORGAN, SYNC and FM
       4-OP and on PAD's NOISE BED and FM 4-OP; COLOR on HOOVER and on PAD's STRING MACHINE and
       TAPE LOOP.
     - **A model that holds its level**: DECAY on PAD's GRANULAR, TAPE LOOP and NOISE BED.
     - Singly: TONE on the hats' RIM (and faint on TOM 808), TUNE on SHAKER; SIZE, WIDTH,
       PREDELAY and DENSITY on RUMBLE's SUB DRONE, SIZE and WIDTH on its SAMPLE; SIGNAL on
       VOICE's SAMPLE and STUTTER, TONE on its RADIO; DENSITY on PAD, faint (-36 to -40).
     - GLIDE on BASS, every model: its steps are one note.
     *None of these is changed in this build.* Each is a model's own design, and waking it
     changes how that model sounds in what is saved: yours to say which.
547. **The outside audit, checked against the code.** Its findings are sound. Made right here:
     - *F01, NORMALISE measured the two channels added together*: a one-sided file came up
       twice too far and two opposed channels a hundred times. It and PEAK now go by the
       loudest point of either channel.
     - *F04, a MUTE or SOLO by MIDI went past ARM*: it was made at once from the MIDI thread.
       With ARM lit it now goes to the session, and waits.
     - *F05, ARM's mutes and solos depended on the screen's thread*: they are now made by the
       audio thread, a block or two before the line, with that thread doing nothing. A recall
       is still made by the screen's thread, now 0.9 of a beat before its line and not 0.5; a
       stall longer than that still makes it late. **543's "never after" was wrong** of both.
     - *F07, a transition's file played at the wrong speed after the device's rate changed.*
     *Agreed, and not done:* F02 (the root is the strongest repeat, which can be a multiple of
     the true period: 110 Hz read as 36.7; a better estimate would retune samples already
     placed, so it wants a ROOT you can set and a rule for old ones); F03 (a stereo voice's
     sides pass the filter and DRIVE by); F05 for recalls (a whole change put on the audio
     clock); F06 (a macro knob's MOTION is taken once a block, so a fast one changes with the
     buffer); F08 (TRANSITION is in no stem); F09 and F10 (its files are not copied into the
     library, and every one loaded is held until the app closes); F11 (a stem file's lengths
     are trusted); F12 (a sample held only in memory is lost to a new voice at 96 kHz).

## Every knob heard, on every model (Nov 15, 16:18): sixty-first build

548. **You**, 16:18: "Ideally wake every knob. Even if  it changes some saves sounds, nothing
     important has been made yet, so now is the time to do it. And I think having dead knobs
     makes the instrument harder to play and less appealing. Being able to hear every knob
     makes the instrument feel more tangible and less like a computer (I love computers btw,
     but they’re not always intuitive ).  So I’d say let’s do that first then audit fixes.
     Color can stay in config because I’ll use it to find a sweet spot in the wave table to
     modular back and forth to get motion. So once a sweet spot idle found, I wouldn’t move it
     during a performance in a. On automated way." 16:38: "When i use the Plain button, that
     doesn't disable the knobs ability to change the sound, correct?" (Answered: correct;
     PLAIN puts each knob where it does nothing and leaves every one of them live.)
     COLOR stays in CONFIG on BASS and CHORD.
549. **What 546 found asleep, woken.** Each by what its model lacked, in SF-000's copy of the
     suite:
     - *No filter*: ORGAN, FM BELL and FM 4-OP (LEAD, CHORD, PAD) and BASS's SINE SUB now have
       the state-variable filter the other models have, so CUTOFF, RES and ENV AMT act. SINE
       SUB also settles to 0.7 of its level so DECAY is heard.
     - *No ensemble*: SPACE now acts on ACID, MONO 101, MS20, FM BELL, SYNC and FM 4-OP of the
       LEAD list and on PAD's GRANULAR, TAPE LOOP and NOISE BED.
     - *No DETUNE*: a second 303 beside the first; a second sync; the organ's drawbars each a
       few cents from true; FM 4-OP's operators a few cents apart; NOISE BED a second band up
       to a fifth over the first. None of it on BASS, which stays one voice in the middle.
     - *No COLOR*: on HOOVER, STRING MACHINE and TAPE LOOP it is a tilt, darker under the
       centre and brighter over it (nothing at the centre, where it opens).
     - *Held level*: PAD's STRING MACHINE, GRANULAR, TAPE LOOP and NOISE BED settle to 0.7 or
       0.75 so DECAY is heard; a slow model's echoes come up in a third of its attack, so
       DENSITY is heard on PAD.
     - *The hats*: TONE on RIM, TUNE on SHAKER, TONE louder on TOM 808.
     - *RUMBLE*: on SUB DRONE, SIZE is an octave under it, WIDTH sets its two sides beating
       apart, PREDELAY moves its swell in the bar and DENSITY makes it swell 1, 2, 4 or 8 times
       a bar; with a sample, SIZE is a room behind it and WIDTH that room's width.
     - *VOICE*: SIGNAL on the phrase as it is and on STUTTER is how much of its band is left
       (all of it at full); TONE tilts RADIO. PLAIN now also puts SIGNAL at full.
     **Audited again**, all twenty-five lane-and-model pairs that were changed, and the pad's
     others: every knob is heard on every model, but GLIDE on BASS while its steps are one
     note. The audit now listens to both sides, so WIDTH is heard where it was only missed.
550. **What this changes.** The LEAD lane opens as it did (its DETUNE and SPACE, in CONFIG, now
     open at nought: one dry 303). The rig as it opens is not quite the sound it was: the
     bass's sine layer now passes a filter, and the reference render moved by 0.01 dB in level
     (its difference from the old one is 30 dB under it, the filter's own delay). Saved sounds
     that use any model above are changed, as you said they could be.

## The outside audit's findings, second part: sixty-second build

551. **Made right** (548: "let's do that first then audit fixes"), each named as in 547:
     - *F02, the root*: it was the strongest repeat, which can be any number of periods (110 Hz
       read as 36.7, 440 as 40). It is now the first repeat that is a peak and nearly as strong
       as the strongest, read between the samples. Tones of 55, 110, 220, 261.6, 440, 880 and
       1000 Hz each read as themselves to half a per cent. Your bass file reads D#0, as it did.
       *No ROOT set by hand was added: TUNE moves a sample's pitch, and PLAIN sets it.*
     - *F08, TRANSITION in no stem*: it has a stem of its own, the last of them, kept with a
       snapshot's sound under the name "transition", and a track of its own in a take (not
       kept when silent, as any track).
     - *F09, its files not the library's*: a file given to it is copied into the library as a
       lane's sample is, written by its place there, and kept from the sweep of unused copies.
       One that cannot be found when the set opens plays the built-in, and its name is kept
       and written back.
     - *F10*: the files a slot has had are held six deep, not for ever.
     - *F11*: a stem's length is checked against what is left of the file before it is used.
     - *F12*: a new voice is made ready at the engine's rate before it asks for its sample.
       *Not checked by a test here: it shows only at 96 kHz with the sample's file gone.*
552. *Still not done:* **F03** (a stereo voice's sides pass the filter and DRIVE by), **F05 for
     recalls** (a whole recall put on the audio clock: it is still made by the screen's thread,
     0.9 of a beat before its line), **F06** (a macro knob's MOTION is taken once a block).

## TRANSITION in the arrangement; a part's own notes key (Nov 15, 18:47): sixty-third build

553. **You**, 18:47: "how would I add the transitrion to an arrangement?" 18:55: "the arrange
     doesn't seem to be bringing in the sequencer notes for a snapshot." 18:56: "I ahd to
     toggle notes in the library. Wonder if that should be a toggle on the chip itself."
     18:59: "I'd like to be able to add a unique riser and hit sample for each transition in
     the arrangement."
554. **A part of the arrangement is come into with the TRANSITION**, by itself, as the
     arrangement plays. Each part's card has two plates, **IN** and **HIT**: a lead-in that
     ends on the part's bar line, a hit that lands with the part. A press steps a plate: off,
     lit with what the bar's plates say (amber), and, once a file has been dropped on it, lit
     with **a file of the part's own** (green). The lead-in is set off the TRANSITION's BARS
     before the part's line, or when the part before begins if that one is shorter. The files
     are the library's own copies, read before the arrangement plays, and kept with the set.
     A printed arrangement has them too. *The two plates, and their three states, are mine.*
555. **A part brings its notes by its own key, N**, the fifteenth on its card, lit as a part
     starts. The library's NOTES no longer decides what an arrangement brings: it is for a
     recall made by hand. *That switch, on another page, had emptied every part of its notes
     without a sign: the fault was the design's.* **Parts already in your arrangement bring
     their notes now**, whatever NOTES says.
     *Not done: N on a chip in the library, on a stack knob's snapshots and on a chip in hand.
     There the fourteen keys are a number kept with each place, and a fifteenth wants every
     place that keeps one changed together. Yours to say.*
556. **ARM does not hold the arrangement's own recalls**: only a recall made by hand waits.
     (As 543 was built, a part reached with ARM lit would have waited for the transition's
     line instead of its own.)

557. **You**, 21:17: "does print from arrangement bypass the drum bus?" **No; measured**
     (`--printbus`, kept as a check): a part whose snapshot was taken with the bus at work,
     printed from a rig whose own bus is at rest, prints a set 1.9 LU louder than the same
     with the bus at rest (-9.0 against -10.9 LUFS), and the bus's own file is there (peak
     0.29) where otherwise it is silent and not kept. What a print does do: **the numbered
     drum files are the lanes as they go into the bus**, and what the bus did to them
     together is its own file, DRUM BUS. The set has it; the drum files alone do not. And a
     part whose **B** key is empty leaves the bus as the part before left it.

## FINISH: a finisher's work, native, on the master (Nov 15, 22:11): sixty-fourth build

558. **You**, 22:11: "would it be possible to make a slot for a vst plugin in the master? I've
     been taking the stems and using The God Particle plugin from Cradle and I like it, so
     unless we can achieve similar results, I was thinking it might work to use the plugin
     itesl omn the instrument. Thoughts?" 22:14: "native would be better. Would a pre and post
     mix of a track i made in SF-000 do the trick?" 22:44: a test signal of mine
     (`tools/master_probe.py`) bounced through the plugin, the set as SF-000 printed it, and
     the same set through the plugin.
559. **FINISH is a stage of the master, before its limiter** (`suite/modules/Finish.cpp`,
     SF-000's own). The mix in three bands, under 150 Hz, between, and over 2.5 kHz, of its
     middle and of its sides. Each band is held by a compressor of its own and given a little
     of its own third harmonic; the middle band is lifted 1 dB and the top eased 1.6; the
     sides are brought up 3 dB where they are bright. No delay is added. **Its control is a
     plate on the MASTER panel, FINISH, stepped by a press: OFF, 25, 50, 75, 100.** At OFF the
     samples pass untouched (the reference render is the same to the bit). It is kept with the
     set, and a print has it. *The plate and its five places are mine: the master's three
     knobs and its page on the E16 are full, and a fourth knob wants both laid out again.
     Yours to say.*
560. **What it does at 100 was set by measuring the plugin** with the test signal and fitting
     this stage until, measured the same way, it gave the same figures. Each is against its
     own gain on a quiet low tone (the plugin's is 8.2 dB, which goes into its own limiter;
     this stage's is its 1.75 dB of make-up, 562). Plugin, then FINISH:
     - a tone swept, by its level (dBFS), at 63 Hz, 250 Hz, 1 kHz, 4 kHz, 16 kHz:
       at -30: 0.0 0.8 0.7 -1.2 -1.6, and 0.0 0.8 0.7 -1.2 -1.6;
       at -18: -0.3 -0.7 -1.3 -1.3 -1.6, and -0.3 -0.8 -1.3 -1.4 -1.6;
       at -12: -1.0 -2.0 -2.8 -1.6 -2.2, and -1.0 -2.0 -2.7 -1.8 -2.4;
       at -6: -2.4 -3.9 -4.7 -4.4 -5.5, and -2.3 -3.6 -4.4 -4.4 -5.6.
     - pink noise at -18 dBFS RMS, in five bands from the lows up: -0.6 -0.8 -0.8 -1.4 -1.7,
       and -0.5 -0.8 -1.0 -1.4 -1.6.
     - a 1 kHz tone stepped from -30 to -10 dBFS, 2, 20, 100 and 300 ms after: -0.5 -1.4 -2.5
       -3.2, and -0.4 -1.2 -2.4 -3.2; stepped back, 50, 200 and 700 ms after: -2.5 -1.0 0.6,
       and -2.4 -1.0 0.5. At 100 Hz it is held 1 dB and lets go over seconds; at 6 kHz 1.7 dB
       (here 1.5) within 20 ms.
     - harmonics of a tone at -10 dBFS: odd only, the third 38 to 40 dB under it at 100 Hz,
       1 kHz and 6 kHz, and here 38, 40 and 41. A low tone leaves no mark on a high one
       played with it, in either.
     - the sides against the middle, of wide noise: unchanged under 200 Hz in both; up 0.6 dB
       to 4 kHz, here 0.7; up 3.4 dB above, here 2.9 (562).
     The middle band works by the band's mean level, 1.28 to 1 over -28 dBFS with a knee 16 dB
     wide, a quarter of it in 3 ms and the rest in 80, 250 ms to let go. The low band takes a
     twelfth of what its peaks are over -21 dBFS and lets go in 100 ms and 3.5 s. The high band
     is 1.9 to 1 over -13 dBFS, 3 ms and 40.
561. **Your set through both.** The plugin's bounce peaks at +5.4 dBFS with a flat top there,
     11 % of its samples over full scale: something after the plugin adds 5.5 dB, and I do not
     know what. With that taken off, and its quiet passages set beside the test signal, the
     plugin was working on your set 3 dB lower than the file's own level. FINISH is set to
     work there. By octave, what the plugin did to the set's middle, against the mean of it:
     +0.1 -0.4 -1.0 -0.7 +0.5 +0.2 0.0 +0.4 +0.4 +0.2 (31 Hz to 16 kHz); this stage with a flat
     cut after it, as the plugin's own last stage makes: +0.3 -0.1 -0.8 -0.3 +0.4 +0.3 -0.1
     +0.2 +0.1 +0.1. The sides of your set came up 2.1 to 2.5 dB above 3 kHz through the
     plugin, less than the test signal's 3.4: *I set 3, between the two.*
562. **It finds its own level, and gives back what it takes.** The figures in 560 are levels as
     the plugin was given them; the mix reaches this stage at whatever level the set is.
     So it listens (also while OFF): the loudest three seconds of the last minute are put
     12 dB under full scale in loudness (-12 LUFS) for the bands to work on, and taken back
     out after. Slowly, only while there is music, and a breakdown is held through. With the
     rig as it opens the mix reaches it at -14.9 LUFS and it gives itself 2.9 dB. **And it
     makes up 1.75 dB at 100**: it took 1.6 LU from the rig as it opens and 1.9 from your set,
     all but the same from one passage to the next. So a press of the plate changes the sound
     and not the level. Measured through the whole master: -9.5 LUFS with it off, -9.4 at 100;
     the peaks stand 0.3 dB nearer the loudness (a crest of 9.4 dB against 9.7). *Both are
     mine.* **It does not make the set louder. The plugin did, by 2.4 dB, in its own last
     stage, which cuts peaks flat. Here that is LIMIT's knob: 3 dB up with FINISH at 100 came
     out 2.5 LU louder on the rig as it opens (-6.9 LUFS against -9.4), which is the plugin's
     bounce as near as I can measure it.**
563. **The limiter's own trim no longer remembers a reading of less than three seconds**
     (`suite/modules/Master.cpp`, marked). Its loudness meter, just prepared, says what it has
     heard so far: the set's first hit alone, to begin with. That reading was kept for a minute
     as the loudest, and held the trim under where the set's loudest three seconds put it.
     **A set, and every print of one, came up 0.9 LU a minute in** (-10.3 LUFS, then -9.4,
     with the rig as it opens); with FINISH in, by 1.9. Now it is at its loudness six seconds
     in and stays. The first three seconds are trimmed as before, so a set that starts too
     loud is still brought down at once. The test's reference render is the same to the bit.
     *I found this measuring FINISH and mended it unasked, in SF-000's copy only. The suite's
     own LIMIT in SF-Live-Suite has the same, untouched; in Live it is the first minute after
     the device loads.*
564. Tools kept: `--finishcheck IN OUT` (a file through the stage alone, with `--auto`,
     `--input`, `--amount`, and `--limit` for the limiter after it) and `--finishrig DIR` (the
     rig rendered with it off and on; `--bare` takes the limiter out of the way).
     *Not done: FINISH on the E16 or as a knob; its level shown on the screen (the plate is
     lit, no more); the plugin's last stage as its own thing. The lane files of a print do not
     have it: it is in the master, so in the set's file.*

## A print for a mastering engineer; a chip put down; a part's card (Nov 16, 01:12): sixty-fifth build

565. **You**, 01:12: "For arrangement, I like how it's working so far. there's a small bug where
     a chip thats playing its preview doesn't stop automatically when you play the arrangement
     and the sounds overlap. And I'd like to be able to create an arrangement, and then be able
     to select a chip to edit its sounds (in the main view) but not actually alter the original
     chip. So my thought was that clicking an edit button on a chip in an arrangement opens the
     main view, plays the chip as it would be rendered in the arrangement (meaning it has to
     know what came before it) and then save that edit into a new chip that gets swapped for
     the one in the arrangement. That way the edited chip is updated and the original chip is
     unaltered, and the new chip becomes part of the library for future use outside of the
     arrangement. Thoughts? And my plan is to create tracks in arrange, then take those chips
     and make a stack on a knob for performing live versions of my tracks." 01:20: "and the
     output prints of the arranger should produce stems that a profrsional mastering engineer
     could use."
566. **A chip being listened to is put down when the arrangement is played** (PLAY and FROM
     HERE). One pressed while it plays is still heard, as one pressed while the set plays is.
567. **A print is laid out for a mastering engineer.** What was there before: the set after
     the limiter, and beside it every lane, the two returns and a file of what the drum bus
     *did* (its output less its input). The files were already 32-bit float, one length and in
     step. What was wrong for someone downstream:
     - **there was no mix without the limiter**, which is what mastering starts from. Now
       `MIX BEFORE THE LIMITER.wav` (the master's low end, tape, compressor and FINISH on it)
       and `MIX BEFORE THE MASTER.wav` (none of it), beside `00 SET.wav`;
     - **the bus's file could only be added back to the drums untouched**: change the kick and
       it no longer fits. Now the lanes that play through the bus are **one stem as the bus
       leaves them**, `STEMS/14 DRUM BUS.wav`, and are kept one by one, as they go in, in
       `INTO THE DRUM BUS/`. If the bus did nothing throughout, each lane is a stem of its own
       and there is neither;
     - **nothing said what the files were.** `PRINT.txt` now lists each with what it is, its
       highest sample, the three mixes' loudness, and says if a mix goes over full scale.
     **The stems add up to the mix before the master to the sample**: measured in three
     prints (the bus at work, at rest, and a lane leaving the bus part way), what is left is
     141 to 144 dB under the mix. `--printbus` is that check. A lane on the bus for part of
     an arrangement is in both places, each for its part. *The layout, the names and leaving
     the bus's difference out of a print are mine. A take is as it was.* **A print's lanes are
     no longer beside the set: they are in `STEMS/`.**
568. **A part's card: its two plates sat on its length** (IN and HIT, the sixty-third build's,
     drawn over "FOR 0:15": I had not seen the page then). When it starts and how long it
     lasts are now one line, the plates under it.
569. **EDIT on a part: not built, thought about** (your 01:12). What I would build, and where
     I would do other than you said, is in the message of this build; nothing of it is in the
     code.

## EDIT on a part; what came before; TO STACK (Nov 16, 02:22): sixty-sixth build

570. **You**, 02:22, to the four things I asked to settle first (what came before has to be
     built, and FROM HERE should do the same; the new snapshot is the whole rig as heard; the
     part keeps its keys and gains those of what was changed; an edit made again replaces the
     snapshot the last one made, if nothing else uses it): "1. agree 2. agree 3. agree
     4. agree  build it, and the make it a stack button."
571. **What came before a part** is worked out, not played: the rig as it stands, with each
     part's share of its snapshot laid over it in turn (the parts of the rig its keys take, its
     notes or not), up to the part. Checked against the three recalls made one after another:
     the same. **FROM HERE plays the chosen part with it**, from stopped at once, and with the
     clock running on the next bar line over the part's own morph. *Until now FROM HERE recalled
     the one part, and a part with a key out sounded as the rig happened to stand.*
572. **EDIT** (a key on the arrangement's page, for the chosen part; *you said a button on the
     chip: the card has no room for one beside IN and HIT, and FROM HERE, EARLIER, LATER and
     REMOVE already act on the chosen part from there. Say if you want it on the card.*) The
     arrangement and the clock stop, the rig is made what the arrangement has at that part, and
     it plays, in the main view. The bar there says EDIT and the part, with KEEP and DROP.
     - **KEEP** (or Return): a new snapshot, taken as SNAPSHOT takes one (so with its sound
       kept, once it has played four bars), named after the part's: ACID OPEN 2, then 3. It is
       the whole rig as heard. It takes the part's place; **the snapshot the part had is not
       touched**. The part's keys are what they were and those of whatever was changed since
       the rig was brought there (what sounds is compared, part of the rig by part; a lane's
       steps changed lights its key and N). The page says which keys it lit.
     - **Edited again**: the snapshot the last edit made for the part goes, and the new one has
       its name, unless another part, a knob, a stack, or a knob or module kept aside holds it.
     - **DROP** keeps nothing. PLAY, FROM HERE, PRINT, EARLIER and LATER drop an edit too.
     *Mine: the part being edited is amber on its card; the rig is left as it is after DROP;
     a turn of a snapshot knob during an edit counts as a change to what it moves.*
573. **TO STACK** (a key on the page): the arrangement as a stack in the designer's rack, named
     TRACK: its parts' snapshots in order, each with the keys its part has; a snapshot that is
     in two parts is in the stack twice. It is not put on a knob: that is yours to do there.
     **A part with a key out leans on what came before it, on a knob as in the arrangement**,
     and going back along the knob does not put back what a later part changed. The page says
     how many parts have one. *To make such a part stand by itself: EDIT it, KEEP, and light
     its keys: the snapshot KEEP made is whole. I did not make TO STACK do that for you: it
     would put snapshots in the library that you never heard.*

## A crackle in the arrangement's morphs; GRANULAR's cost (Nov 16, 08:00): sixty-seventh build

574. **You**, 08:00: "some morphs in the arrangement are now producing a lot of glitch crackle
     static (possibley speaker damaging). what do you neeed to diagnose?" Then the session file
     (08:08) and its samples, and: (08:10) "it happens between snapshot 94 and 96." (08:11)
     "same place (snapshot at least) every time. i believe after the very latest build.happens
     on play from any position before the affected snapshots." (08:11, to whether a part there
     had been edited) "one part was but it happened before and after i edited it." (08:12)
     "finish is on . i didn't notice the lamp, but wasn't looking." (08:22) "it's not in the
     prunted mix".
575. **What was found**, on your session and samples, here (Linux, one slow core: every figure
     is a share of real time on this machine, for comparing with each other, not your Mac's):
     - **The sound itself is right.** Your arrangement printed, and played the way the
       instrument plays it live (the device's callback, the timer bringing each part; on one
       thread and on two): nothing but finite samples, no step out of place, no fault said.
       What is loud and bright between SNAP 094 and SNAP 096 is what you put there: the
       TRANSITION's own riser into SNAP 095 2 (bars 61 to 64) and its hit, and your "Guerro"
       into SNAP 096 (bar 80). Both are in the print.
     - **The PAD lane, in SNAP 088 to 095 and 098, took 149 to 159 % of a core by itself**; no
       other lane's instrument took more than 10 %, no effect more than 5 %. The whole rig:
       190 to 224 % in those parts, 48 to 69 % in the others (SNAP 017, 096, 018), 46 % as the
       instrument comes. The pad there is GRANULAR with a GRANULAR layer on "DFAM Boom", CHORD
       MIN7: sixteen notes sounding, four grains each, at twice the rate, and each grain read
       the sample 5.2 times faster than it was recorded (the sample's root is far under the
       notes played). All of it was in the read (576).
     - **The morphs are the dearest moments.** Played the live way, SNAP 094's own bars took
       204 % of a buffer's time on average and the four bars into SNAP 095 2 (65 to 68) 226 %;
       SNAP 095 2's own 212 %, and the four into SNAP 017 (73 to 76) 233 %, the dearest of
       the arrangement: for the length of a morph the lanes whose make-up changes play twice,
       the one leaving and the one arriving, and so do the effects.
     - *My reading, not proven:* on your Mac the rig in those parts ran close to all the time
       a buffer has, and in those two morphs over it. A buffer that is not ready in time is a
       gap in the sound; a run of them is a crackle at the sound's full level. A print is not
       made in real time, so it has none. This fits "same place every time" and "not in the
       printed mix". **It does not explain "after the very latest build"**: FINISH (build 64)
       costs 2 % here, and nothing in builds 65 and 66 is in the sound's path while the
       arrangement plays. The snapshots are newer than those builds; whether the pad in them
       is, I cannot see. The Mac has not been measured: 577 is how it will be.
576. **GRANULAR's read** (`suite/modules/Synth.cpp`, SF-000's own). A grain played above its
     source's pitch read the source (or the octave-down copy on which it advanced one to two
     samples a step) through `sampleAt`'s low-pass: 18 to 36 points for each channel, each with
     a table look-up, and a cosine for its window, every sample. Now, settled once as the
     grain starts: it reads the copy an octave further down, on which it advances a sample a
     step at the most, with **sixteen points from a table** (a windowed sinc cut at the copy's
     own Nyquist, 256 fractions of a sample, the two nearest blended), both channels in one
     pass; its window turns a step a sample. A grain at or below the source's pitch reads the
     source as it did.
     - **Cost**: the PAD lane in those snapshots 157 % to 31 %; the rig in those parts 202 to
       218 % to 82 to 107 %; played the live way, SNAP 094's bars 204 % to 83 %, the morph
       into SNAP 095 2 226 % to 95 %, the morph into SNAP 017 233 % to 119 %. (Of the pad's
       31 %: 12 the reads, 19 the rest of sixteen voices at twice the rate.)
     - **Sound**: your pad's stem over parts 7 and 8, printed before and after: the difference
       is 74 dB under the pad; the whole set's, 91 dB under it. On tones, each read on a copy
       at 0.51 to 1 sample a step and halved as a voice's sum is: level within 0.01 dB to
       18 kHz and 0.3 dB at 20 kHz, and everything else under 20 kHz more than 78 dB below the
       tone (the read it replaces: 0.00 dB, 89 dB).
     - *Tried and not kept:* four points on the same copy, as a sample at its own pitch is
       read. Five points of a core cheaper, and the same on your pad to 73 dB; but a bright
       source on some notes came out 1.1 dB down at 15 kHz, with what the read mirrored 37 to
       44 dB under the top it came from.
     - **Checked** in the engine test: a sample of two tones (220 Hz, and 7040 Hz 12 dB under
       it), GRANULAR an octave and a semitone over its root: the tones where the note puts
       them, everything else under 20 kHz 109 dB under the lower tone. (With four points:
       70 dB, and the check fails.) `--graincheck` runs it alone.
577. **A buffer that ran late is said.** *Mine.* The engine counts the buffers that took longer
     to make than they last, since the audio started, and keeps the most a buffer took of its
     own time lately. **The FAULT lamp lights for three seconds after one** (it lit only for a
     module's own fault, which a late buffer is not: the lamp you did not see would not have
     shown this). SET UP's ENGINE line has a second row: "n % AT THE MOST LATELY", then "NO
     BUFFER HAS RUN LATE" or "RUN LATE (EACH A GAP IN THE SOUND): n", and what the interface
     itself counted as dropped, if it counts.
578. **Tools** (README): `--printsession SESSION.json DIR` opens a session file as the
     instrument does (a copy of it) and prints its arrangement; `--load` instead says what each
     part asks of the processor, lane by lane; `--time`, bar by bar. `--livesession` plays it
     as it is played live and writes what came out, with how long each buffer took.
579. *Seen on the way, not changed:* the TRANSITION bar's two files in your session are where
     you chose them ("/Library/Audio/Presets/Xfer Records/Serum 2 Presets/…": S-Sound.wav,
     626 Cymbal Crash.flac), not copies in the library: files given to the bar before the
     library kept its own copy of them. Both slots are on their built-in sound, so nothing
     plays them now; on another computer they would not be found. A lane with nothing playing
     costs 4 to 6 % of a core here, the returns 10 %, the master 8 %: the rig at rest is 46 %.
     That is where the next saving is, if one is wanted.
580. **You**, 09:03, for after this: "The stack knob should also change notes if the chips have
     n selected. we can handle it similar to the way arrangement handles it. And in the main
     view, when moving a stack knob, it would be useful if the popup hud showed a progress bar
     to the next snapshot so i could know when i hit a complete snapshot state and how far i
     have to go. that way I can do a glide to a specifc snapshot or a point in between but know
     what i'm aiming at when i trun the knob. Also need a way to delete knobs and stacks and in
     the macro interface."

## Not verified

- **The crackle (574 to 577) has not been heard here, and your Mac has not been measured.** That
  the rig ran out of time in those morphs is my reading of what the parts cost; the cost is
  measured, on another machine. With this build: play the arrangement through SNAP 094 to 096
  and watch the FAULT lamp; afterwards SET UP's ENGINE line says the most a buffer took and
  how many ran late. **If it still crackles and none ran late, it is not this**, and the line
  says so. GRANULAR's new read is measured on tones and against your pad as it was; no one has
  listened to a bright granular sound through it.

- **EDIT (572) and TO STACK (573)** are checked on the session and the drawn screens, with the
  snapshots the instrument comes with. **Not on your Mac, not on an arrangement of yours, and
  not by ear**: that what EDIT plays is what the arrangement plays at that part is checked on
  the knobs, the steps and the make-up of each lane, not on the sound. Not tried: an EDIT
  while the ONE leads the clock; KEEP with ONLINE lit; a TRACK stack on a knob.

- **A print's files (567)** are checked by measurement on Linux: lengths, the first hit, the
  stems against the mix. **No mastering engineer has had them**, and no print of an
  arrangement of yours has been made with this build. The first one you make: look in
  `PRINT.txt` at the highest sample of `MIX BEFORE THE LIMITER.wav`.

- **FINISH (559 to 562) is a match of figures, not of sound.** Measured with a test signal and
  with one set of yours, on Linux; **not heard by anyone, and not on your Mac.** The plugin's
  settings were whatever they were in your two bounces, and I could not tell what added
  5.5 dB after it in the set's. Set the two side by side on a set you know: FINISH 100 with
  LIMIT up 3 dB, against the plugin on the same print. Where they part, tell me in what.
  25, 50 and 75 are that much of every part of it at once, which no measurement was made of.
- **The limiter's trim at a start (563)** is measured on the rig as it opens. Not on a set of
  yours, and not in a print of an arrangement.

- **TRANSITION in the arrangement (554)** and **N (555)**: checked in the engine (a part's own
  hit file lands on its line; its notes come with NOTES out), **the plates and the fifteenth
  key not seen on a screen by me**: the card's keys are a little narrower to make room, and I
  have no picture of an arrangement with parts. Dropping a file on a plate is not exercised by
  a check.
- **Every knob heard (549)**: measured as a difference in the lane's sound, **not judged by
  ear**: that a knob moves the sound does not say it moves it well. The amounts (a filter on an
  FM bell, the organ's cents, the drone's octave, SIGNAL's band) are first settings to be
  tuned by listening.
- **TRANSITION (543)**: timed and measured in the checks, not heard, and not on your Mac. The
  riser and the impact are first guesses at sounds. ARM's waiting is driven by a timer of 15
  ms (as first built; see 547: a mute or a solo is now the audio thread's, a recall is not). Not tried with the clock led
  from outside, with the tempo moved while a transition is on its way, or while the
  arrangement plays.
- **WAVETABLE on BASS, LEAD and CHORD, and KNOBS (537, 538)**: measured, not heard. The audit
  was run with SAMPLE and with WAVETABLE on the lanes that have them, and as the lanes open;
  **not for every other model of every lane** (about a hundred more, an hour and a half of
  rendering). COLOR's tilt, DETUNE's 30 cents and the hats' TONE are first guesses at amounts.
  The conversion of older model places is checked on made-up snapshots, not on your library.
- **A sample's own controls (531 to 534)** are measured at the lane with your one file and
  with made-up tones: not by ear, not on your Mac. GAIN moves in steps of a block while it is
  turned on a held note (it may be heard as fine grain on a pure tone; not listened for).
  ENVELOPE's decay times (0.15 to 2.4 s) are a first guess. THROUGH on LEAD, CHORD and PAD lets
  notes of a long sample pile up until they end or the lane runs out of voices.
- **PLAIN (526)** is measured against one file of yours and against made-up tones, at the
  lane (before the master), with one note held. Not by ear, not on your Mac, and not with a
  drum one-shot, a vocal phrase or a stereo-wide sample: KICK, the hats, CLAP and VOICE come
  out mono, as they did before. The root is the sampler's own guess; a sample with no clear
  pitch reads as C3.
- **A chip pressed plays (522, 523)**: checked with a real snapshot and its sound at the
  device's outputs for MACROS, a stack knob's card and the library, with PHONES on one pair of
  outputs. **Not checked with outputs 3 + 4 open** (the checks have two outputs), and not by
  ear. An arrangement part's stems are checked by what the rig is set to when the part comes
  in, not by listening to a print.
- **In hand and carried (518 to 520)** are done in the drawn screen with made-up presses and
  moves, and read back from the session. **Nobody has carried a card with a hand**: how far
  the pointer has to move before a press becomes a carry (8 points here), whether the bar and
  the ring are easy to see, and whether carrying while the column is scrolled lands where you
  mean, are yours to say. In 3D the page is drawn again at every move of a carry; I have not
  seen how that feels.
- **Stacks of their own and the finders (512 to 515)** are pressed in the drawn screen and
  read back from the session file; the rule for an empty stack is checked against a made-up
  library. **FITS NOW, MORE and LESS on the screen are checked only with nothing playing**
  (they list none); with a set playing they use the E16 finders' own code, which has its
  checks, but nobody has looked at what they list on a screen. FIND's typing is not pressed
  in the checks (its filter is). Not on your Mac.
- **The knobs kept (506 to 510)** are pressed in the drawn screen and read back from the
  session file: kept, put on a place of each kind, refused where the place holds something or
  is the wrong kind, kept again, put into another module. Not on your Mac, and not with your
  own knobs. **Modules (502 to 505)** the same.
- **The module (438 to 448) is checked on the session, the drawn screens and a made-up E16**:
  the turn that never jumps (from a place that is none of its snapshots, onto one, between
  two, back, on to the next), a part put out, CUE lit and out, knob 4's press, MOTION on a
  stack knob, LOAD and the mirror against the web library's code running here. **Not heard.**
  Whether a turn through your own snapshots finds the places between them that you use it for
  is the whole point and only you can say.
- **Your session's knobs brought over (445)** are checked on the session file of Nov 11 that
  the tests keep (its four that hold the rig's own pushes become macro knobs where they are,
  its fifth stays a stack knob, and a knob of your own snapshots put on knob 2 goes to knob 6
  with its place, its MOTION and its sound) and on made-up ones. Not on yours.
  `session-before-two-kinds.json` is beside your session if anything is not as you expect.
- **The E16's corners (449 to 452, 467)** are checked against the scene files object by object
  and with made-up messages, the scenes of the build before included. Not on your E16.
- **The column (472)** is pressed in the drawn screen: built, keyed, moved, taken off, kept in
  the file. Not seen on your Mac: whether the keys are big enough to hit, and the headphones.
- **Hearing before adding, and the rack (476, 477)** are pressed in the drawn screen and read
  back from the session file. Not on your Mac.
- **The forty-ninth build (478 to 481)** is checked with made-up messages from an E16: CONFIG
  shutting, 13 and 16 together, MACROS from the library with the E16 in the other scene. Not
  on your E16: whether its two presses reach SF-000 close enough together to count as one, and
  whether 13 and 16 acting as they are let go feels any slower.
- **LOAD making room (466)** is checked against the web library's code running here, with a
  new session's TOUR on knob 5 and a module that has another knob for 5. I took your failure
  to be this one from how the code behaves; I have not seen your LOAD line. If TEST STACK 1
  still does not come, the line LOAD prints (in full on the library screen) says why.
- **The press between two snapshots (468)** is checked in numbers (half way between two is
  half way for a knob both have). Not heard.
- **Effects between snapshots (475)** are checked in numbers on one slot (dry, a DELAY, the
  DELAY again, a REVERB): what is in the rack and where its MIX and FEEDBACK stand, pressed and
  turned. **Not heard**, and that is what matters here: whether a reverb coming up under a
  turn sounds like a blend, and whether an effect crossed over at half way (a delay's TIME
  differing, or another effect taking the slot) is clean with your GLIDE.
- **The snapshot page's names (453)** go by the same message the insert and CONFIG pages
  use, checked against a mock of the script. Not seen on the E16's display.
- **The column and the keys (456 to 459)** are pressed and dragged in a browser with no
  graphics card, and looked at as pictures. The keys are small (about 14 pixels wide at your
  window's size): whether they are easy to hit is yours to say. **Stems in the page have not
  been heard at all**: they are checked to arrive whole and to be read; whether your browser
  decodes Ogg (Safari did not for a long time) decides whether a chip with keys out plays its
  parts or falls back to its whole clip.

- **The new LOAD (428 to 432) is checked against the web library's code running here**: a knob
  sent up into its pocket, kept when changed here, brought down when changed there, swapped
  for another with what it held kept in the library, emptied when taken off the chassis, and
  left alone when the library has not got its snapshots. Not run against your library through
  Cloudflare Access, and not on your session: the first LOAD there will send up every knob
  you have, and I have not seen that happen with real ones.
- **Knob macros (422 to 425) are checked on made-up snapshots here and on the rig's own
  knobs through LOAD** (the bass's cutoff taken 0.4 of its travel, times the depth). Not
  tried with two of your snapshots: how many knobs differ between two real ones (it may be
  dozens, most of them small), and whether the knob that results is musical or needs its
  small moves taken off.
- **The knob modules and the chassis (418 to 421) are checked by pressing and dragging in a
  browser with no graphics card**, and looked at as pictures. Not seen or handled on the Mac:
  whether a module's name is large enough four by four, and whether 150 pixels for a knob's
  whole turn feels right under your hand.
- **A snapshot's lanes (fortieth): heard on the Mac, and they work** (415). Still yours to
  say: whether a lane without its echo (which is in RET) is what you expect to hear.
- **The library's new look (395 to 402) has been seen only as pictures a browser with no
  graphics card drew here**, beside pictures of the dark instrument drawn the same way. Not
  seen: your browser's own drawing of it on the Mac, at two pixels to the point (the picture it
  keeps is then large: if the bench is slow or comes up black, add `?plain` to the address and
  tell me which browser), how the glow and the scratches read at that sharpness, and whether
  the piles' edges in the shade are still easy to tell apart. The sizes of all of it are
  numbers at the top of `bench.js` and in `look.js`.
- **The cue stack (thirty-ninth) is checked in the session and through LOAD**, with a clip
  really playing as its cue. Not felt: how a stack of thirty or a hundred sits under an E16
  knob, and whether the cue should stop by itself when the knob is left alone (it plays until
  the press, or until something else is cued, as the MAP page's does).
- **LOAD (thirty-eighth) is checked end to end against the web library's own code running on
  this machine**: a snapshot brought whole with its clip to the byte, stacks, a macro onto an
  empty knob and not onto a full one, a song as the arrangement, nothing doubled or sent back,
  no answer handled. Not checked: https from the Mac through Cloudflare Access with your
  service token, and the reading of a clip there (on the Mac the request goes through the
  system's client, here through a plain socket).
- **The bench has been run only here**, in a browser with no window and no graphics card
  (Chrome drawing in software). Its checks press and drag as a hand does and all pass, but: it
  has not been opened in your browser on the Mac, behind Cloudflare Access, with your
  snapshots. How it feels to drag, whether a chip's name is big enough on your screen, and
  whether LIKE, SONG and the moods find what your ears would, on real profiles and not made-up
  ones: all yours to say. The flat page is still at `/flat.html`.
- **The thirty-seventh build's two fixes are checked by measurement**: every legend against its
  plate, and RANDOM by the patterns twelve presses write. Not seen on a Mac. One thing to tell
  me: whether a step edited by hand putting RANDOM's key out is what you want, or whether the
  key should stay lit and only the knobs let go.

- **The SEQ page (thirty-sixth) is checked against the scene file and a made-up E16.** Not on
  the E16: whether STEP's eight clicks to a step feel right at 16 steps and usable at 64,
  whether the title is readable as it changes, and whether the knobs settle when STEP comes to
  a step that holds something else (they are told its values 150 ms after the hand is off them).
- **The arrangement (thirty-fifth) is checked by where the knobs are at each bar line, and a
  print by what is in its files**: the kick's hits on the beats they belong to, bar 1 on the
  first sample, two prints the same to the byte. Not heard, and not made on a Mac: how long a
  print of a real set takes there, whether the audio comes back cleanly on the Babyface when
  it is done, where its folder lands. And the thing only you can say: whether a morph that
  begins on a part's first bar is how you think of a transition, or whether it should arrive
  there instead.
- **MOTION on the snapshot knobs (thirty-fourth) is checked by where the knobs are**, block by
  block, against the knob turned by hand. Not heard. Things for your ears: fast waves (a
  sixteenth, a SQUARE) on knobs whose modules smooth a change over 30 ms, which rounds them;
  KICK on a morph; whether +100 on a knob at nought is what you expect. The held knob 15 is
  checked against a made-up E16.
- **The keys' travel (thirty-third) is checked by where each key stands**, frame by frame, on a
  software OpenGL here. Not seen moving on a Mac: whether 45 and 70 ms look like a key or like
  a flicker at your display's rate, and whether latched reads at a glance from where you sit.
- **BANDS (thirty-second) is checked by the response to one click**, band by band, rendered and
  live. Not heard. Whether a hall over a room sounds like one thing or two is for your ears and
  your impulse responses; mine were noise dying away.
- **POCKETS (thirty-first) has not been heard by anyone.** Every claim about it is a
  measurement on made-up tones or on the rig's stems: that the right thing is cut, where, by how
  much and for how long. Whether a mix carved this way sounds better, whether 8 ms is soon
  enough under your kick, whether a bell moving at SPEED is heard as movement, whether the
  pockets I started each lane with suit your sounds: all yours to say. Start with AMOUNT half
  way and the page open. Its page has been drawn here and not seen on a Mac; its E16 pages have
  met only the made-up E16.

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
- ONLINE (thirteenth) is checked here end to end against the web library's code running on this
  machine: sending, clips to the byte, waiting with no answer, catching up, renames, being turned
  away. Not checked: https from the Mac through Cloudflare Access with your service token, and
  the SET UP window itself (it is built and compiles; nothing here can press its keys).
- The crossings (fifteenth) are checked for where they stand in time, that both sides run and are
  let go, and that the lane's sound has no step or gap in it. How a crossing sounds, between
  which effects and models it is musical and between which it is merely smooth, needs your ears.
- The sixteenth build's E16 changes are checked against made-up messages and a mock E16, not the
  thing itself: whether a knob's first turn still jumps, and whether the press on knob 1 swaps to
  GLIDE as the scene file says, need the hardware. Both scenes must be loaded onto the E16 again.
  ONLINE's https from the Mac has since been reported working (a snapshot arrived).
- The nineteenth build's MIDI is checked with made-up messages: no ONE has sent SF-000 a control
  change, and what I say of its modulation lanes is from OXI's descriptions (whether a lane can
  send on another channel than its sequencer's, I could not find). The E16's stepping knobs are
  checked against a mock E16 that moves its knob when told a value, as the real one is believed
  to. PHONES is checked on the engine's buffers, not by ear.
- The lit skin (seventeenth) and its dark instrument (eighteenth) have been looked at here only
  as pictures the app drew to a file (the main screen, a glide, CONFIG open, the library, the
  macro designer), and their checks run in software. Not seen: the Mac's own drawing of it (how it looks on the display, what it costs
  with each changed part painted on its own), a window changing size, and how it feels to play.
- **The 3D stage (twentieth) has never run on a Mac.** Everything about it is checked here on a
  software OpenGL driver (Mesa llvmpipe): that it starts, what it draws (looked at as pictures),
  that the hand reaches every control through the eye, the view, following, the tags, the dark
  instrument, and falling back. Not checked, because nothing here can check it: Apple's OpenGL
  taking these shaders (they are written to the oldest version it offers, and to its stricter
  rules as I know them); the frame rate on your display; the picture on a display with two pixels
  to the point; the mouse over the OpenGL view; the small typing window; full screen and moving
  the window between displays. If it does not start, or starts and draws a blank picture, the
  lit skin comes up and `3d.log` says why: send me that line. The study did run on your Mac at 120 frames a second in a browser, with more
  triangles than this draws.
- **The audit's fixes (twenty-first) are checked here, each with a check of its own**, and none
  on the Mac or on hardware. To try: SEND switched on while it runs (the ONE should stay
  stopped and join at the next RUN, with no burst); a MUTE and a THROW sequenced from the ONE
  (tight, not late); quitting with ONLINE on and the network pulled (it should simply quit); a
  recall that changes VOICE's model with a long sample in it (no pause of the screen).
- **The faceplates' finish and the pulse have been looked at only as a software driver draws
  them**, at a fraction of your display's sharpness. Whether the grain is too fine to see or the
  wear too much is for your eye; each is one number in `Stage.cpp`'s surface shader. The same
  goes for the photograph's scratches (240): how strong they are is `scratch * 0.62` there, how
  large a piece of the sheet a plate takes is `kWearSpan`. Not run on a Mac: reading the sheet
  (JUCE's PNG reader into a one-channel texture of 4096 by 2440), which the stage does without
  if it fails (the plates are then unscratched, and `--stagetest` says so).
- **The twenty-second build is unseen on the Mac**: leaving full screen (the fault was read in
  JUCE's source, not reproduced; the fix uses a call I can only run on Linux), the SETUP window's
  new row, the icon in the Dock (the Finder may show the old one until it forgets it), and how
  MOTION sounds on each wave, which is the suite's code unchanged but has not been heard here.
- **What your E16 has shown** (twenty-third and twenty-fourth builds): its knobs, presses and
  the wheel reach SF-000, and a turn sent with a Bank Select ends on the scene's first page.
  Still only checked with made-up messages and a mock written from the E16's manual for OS
  1.3.0: how quick a change of scene is (a lane to one of its inserts is one); whether the
  script hears of a page turned by Program Change (it looks every 50 ms in case not); the press
  of a knob whose turn is off; the names SF-000 sends showing in place of the scene's.
  `controllers/e16/README.md` ends with what to do about each.
- **The twenty-fifth build's way of turning the E16 has not been on it**: a Program Change by
  itself for a page (with PC Mode on Page), and the page said again after a scene loads. It is
  checked against a made-up E16 that comes up on a scene's first page, one that says nothing,
  one that says another page, and one whose answer comes late. If the real one still ends on
  the wrong page, SETUP's E16 line says what it asked for and what it was told (264).
- **The twenty-fourth build's wheel is checked against a made-up E16 that does and does not take
  the numbers it is sent.** Whether the real one sticks or leaps again is for your hands (259).
  The scenes and the script are unchanged: nothing needs loading onto the E16 again.
- **LEARN (twenty-sixth) is checked on the session and on the drawn screens here**, not on the Mac
  and not by ear: whether the pushes it makes are musical is yours to say.
- Following is checked with made-up moves marked as the E16's or as another port's. The real
  thing to try: a knob on the E16 (the eye should go), the ONE's modulation of a knob (it should
  not), a recall from each.
- **Folders and samples (thirtieth) are checked on Linux, in folders the test makes.** Not on a
  Mac: the folder chooser, a real external drive (macOS asks once before an app may read a
  removable volume: allow it), the Trash on another volume, a library of your size being copied
  at a start (it is done before the window opens: a large one will make the start slow, once).
  Your own session is brought over the first time this build opens it: `backups/` has it as it
  was.
- **Notes from outside (twenty-ninth) are checked with notes fed in by the test at made-up
  times** (2 ms late, as a sequencer's might be): a two-bar bass line, a hat with a ratchet, a
  part that never repeats. Not with the ONE: its swing, its ratchets as it sends them, what it
  does at a pattern change, notes arriving while SF-000 follows its clock with ON BEAT lit. The
  first snapshot you take with the ONE playing is the real check: recall it with NOTES lit and
  the ONE's tracks muted, and look at what is in each lane's steps.
- **The drum bus (twenty-eighth) has not been heard by anyone.** Every figure in 277 to 280 is a
  measurement on the kick or the clap alone. How it sounds on your drums together, whether the
  four knobs' ranges are right, whether BOOM at 50 Hz sits with your kick: yours to say. Its
  page on the E16 is checked against the scene file and a made-up E16 only.
- **KEY (283)**: checked that the bass moves and the boom tunes. Not heard; and I cannot see
  whether any snapshot of yours has KEY set.
- **Modules (502 to 505)** are checked on the session and the drawn screens: kept, copied,
  loaded (the knobs, their ties and stacks, MOTION, nothing moving in the sound), renamed,
  taken out, written to the file and read back, and a session from before them. Not on the
  Mac, and not with a set's worth of your own knobs.
- **CONFIG four across (500) and HOLD: NEW (501)** are checked on the drawn screens and the
  session. Whether the panel going to the E16's page as you press PAGE + feels right, and
  whether NEW leaves what you want left, are yours to say.
- **OPEN on an effect (491)** is checked with a made-up E16 that answers in the first scene's
  numbers, late. Yours has not been seen doing it: `e16.log` (492) is how it will be.
- **A snapshot as its clip (493)** is checked on the knobs' values (taken with a macro knob up,
  recalled with it up, half up, at rest; a part not chosen; a stack knob on a place and between
  two). Not by ear, and not against a clip.
- **A push that turns the knob (490)** is checked with made-up clicks, one before, during or
  after the press. How many clicks a push on your E16 really makes, and whether a click more
  than a quarter of a second before the press ever comes, only the E16 can say.
- **The rack both ways (485 to 489)** is checked on the session with a made-up library (every
  rule above), and against the web library's own code running here: kept here and sent, changed
  each way, made there with a snapshot this library lacked, unlit, put out by its number.
  **Not against snapshots.drosaka.com, and not on the Mac.** The first time ONLINE is lit on
  this build, watch the rack: what is lit TO SF-000 there should appear, and nothing there
  should change. If a stack you did not expect appears or leaves, tell me which.
- **Notes in the key (482 to 484)** are checked on the session and the drawn screens: what a new
  step holds and says with KEY OFF, at A and at D, with KEY FOLLOW on and off, lit by the
  mouse, by the E16's STEP and VEL, and by a generator. Not on the Mac, and I have not heard
  that what a step says is the note that plays: the suite's code does the moving, unchanged.
- The mix is balanced by measurement: levels, loudness, peak. It needs ears.
