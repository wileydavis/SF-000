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

## Not verified

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
- The mix is balanced by measurement: levels, loudness, peak. It needs ears.
