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
     says it only when it changes). The scene is controller 32 with the slot less one, the page
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
     has to be consistent)"). *Mine, all of the how:*
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
     module when it's activated in the same way it does when it gets midi input"). A page turned
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

## Not verified
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
- **The twenty-third build has not met an E16, and nothing here can stand in for one.** SF-000's
  side is checked with made-up messages and the script against a mock written from the E16's
  manual for OS 1.3.0. What only the hardware can say: that Bank Select (controller 32, the slot
  less one) and Program Change (the page less one) on channel 12 turn it to the scene and page
  meant; how quick a change of scene is (a lane to one of its inserts is one); whether it loads
  a scene again when told the one it is in; whether the script hears of a page turned that way
  (it looks every 50 ms in case not); the press of a knob whose turn is off; the names SF-000
  sends showing in place of the scene's. `controllers/e16/README.md` ends with what to do about
  each. Both scenes must be loaded onto the E16 again (SF-FX in SF-LIB's place), and its PC
  Receive Channel set to 12.
- Following is checked with made-up moves marked as the E16's or as another port's. The real
  thing to try: a knob on the E16 (the eye should go), the ONE's modulation of a knob (it should
  not), a recall from each.
- The mix is balanced by measurement: levels, loudness, peak. It needs ears.
