# The suite's sound code in SF-000

`suite/core`, `suite/modules` and `suite/third_party` are copied from
[wileydavis/SF-Live-Suite](https://github.com/wileydavis/SF-Live-Suite) `main` at `4eb9405`
(Nov 10, 2026). SF-000 never writes to that repo.

Changes made to the copy:

- `core/Bus.cpp`: the shared-memory name is `/sf_000_bus_v1`, so SF-000 never shares a bus with
  the suite's plugins running in Live on the same Mac.

- `core/Bus.cpp`, `core/Bus.h`: the bus's clock can be a render's own (`renderClockBegin`,
  `renderClockAdvance`, `renderClockEnd`, `forgetKicks`), and `nowMs` is the same clock as
  `nowSec`. A PRINT runs the rig faster than it is heard, and held up for a third of a second
  it lost the duck under a kick (a kick's report is "current" by the wall). Marked `(SF-000)`.
  See DECISIONS 370.
- `core/Pocket.h`: `kLatency` is `SF_POCKET_LATENCY` if that is defined, 512 if not. SF-000's
  build defines it 0 (`SF000_POCKET_LATENCY` in `CMakeLists.txt`): its engine plays the kick's lane
  first in every buffer, so no module has to wait for the kick's reports. See DECISIONS 100-103.
- `modules/Terroir.cpp`: the check that the convolver's latency is the Pocket's allows a Pocket
  latency of 0. The dry signal is then not delayed, and the wet path's 512 samples are pre-delay.

Nothing else is edited. SF-000 hosts the suite's own `SFProcessor` for every lane, insert, return
and master stage (see `Source/Rig.h` and `Source/Engine.cpp`), so the sound, the 64-sample chunking
and the 30 ms smoothing are the suite's.

To take newer suite code: copy the three folders again and re-apply the changes above.
- `modules/FxColor.cpp` (RADIO) and `modules/Voice.cpp` (the RADIO model): the hiss, and RADIO's
  dropouts and squeals, ride on the level of the signal (all of it from -26 dBFS up, none with
  nothing; up in 5 ms, down over about a third of a second). In Live a silent track's devices are put to sleep; here everything always runs, and a
  lane that was not playing hissed and chirped on its own. Marked `[SF-000]`.
- `core/Processor.h`, `core/Processor.cpp`: `SFProcessor::keyFromHost`. The suite's modules take
  the key only while SF CONTROL is running; SF-000 has none and sets the key itself (its KEY), so
  with this set the key on the bus is in force. Nothing else a CONTROL says is taken as said.
  Marked `(SF-000)`. See DECISIONS 283.
- `modules/FxDrums.cpp` is SF-000's own (the DRUMBUS module: the master's drum bus), with a line
  for it in `core/Registry.h` and `core/Registry.cpp`, marked `(SF-000)`. It is not in the suite.
- `modules/Terroir.cpp`: BANDS (three places across the spectrum), marked `(SF-000)`. Fifteen
  parameters added after IMPORT (`bands`, `bx_low`, `bx_high`, and `b1_`, `b2_`, `b3_` `place`,
  `level`, `decay`, `size`). The first half of `build` is now `shapePlace` (one place made ready:
  the same arithmetic, in the same order), `bandOf` cuts a response to a band, and with BANDS on
  `build` adds three shaped, banded places into the one response the engine runs. A `Key` carries
  the bands (its tenth figure is 0 with BANDS off, so an older state's key is what it was), the
  request to the worker carries them, and the display marks each place with its bands. With
  BANDS off nothing is different. See DECISIONS, thirty-second build.
- **A sample's own controls**, marked `(SF-000)`. `core/Sample.h`: `SampleRegion` carries `level`
  (1 unless set) and `gain()` multiplies by it, so every read of a sample takes it with the
  fades'. `modules/Kit.h`: `withSampleGain` appends `smp_gain` (GAIN, 24 dB either way) and
  `smp_norm` (NORMALISE: the sample's loudest point to full scale) last in a module's list, and
  `regionFrom (c, first, s, gainAt)` sets the region's level from them. In `Synth.cpp`,
  `Drums.cpp` (KICK, the hats and clap, RUMBLE) and `Voice.cpp`: the two parameters, last, and
  that call. `Synth.cpp` also has `smp_play` (HELD, THROUGH: a voice of the SAMPLE model that is
  `through` is not released by its note's end or by GATE) and `smp_env` (a voice that is `shaped`
  takes its amp decay from DECAY and its filter envelope's amount from ENV AMT, which the SAMPLE
  model otherwise sets to none). With all four as they open, nothing is different: the reference
  render is the same file. See DECISIONS, fifty-seventh build.
- **WAVETABLE on BASS and LEAD**, and **every lane knob on a sample**, marked `(SF-000)`.
  `Synth.cpp`: a WAVETABLE model second to last in `bassModels` and `leadModels` (one voice on
  BASS), with a figure each in `Calibration.h` (set by rms beside their neighbours, not LUFS).
  WAVETABLE reads a second, detuned wave where its model has a `detune` (the two new ones and
  PAD's, which also has a sustain of 0.7 where it had 1, so DECAY is heard: PAD's WAVETABLE is
  not the sound it was). `smp_env` is now KNOBS and opens on: a `shaped` sample voice takes
  DECAY (and a sustain from its top fifth), ENV AMT, COLOR (a tilt), DETUNE (a second reading)
  and SPACE (the ensemble), the last two not on a mono model, and in PLAY THROUGH GATE ends it
  part way. `Drums.cpp` (hats, clap) and `Voice.cpp` (MODEL SAMPLE): TONE tilts a sample.
  SF-000's `Engine` gives a model's place kept from before the lists grew as the same model.
  See DECISIONS, fifty-eighth build.
- `core/Sample.h`, `core/Sample.cpp`: `chanPeak`, the loudest point of either channel, beside
  `peak` (which is of the two added together); NORMALISE (`Kit.h`) goes by it. `(SF-000)`.
- **Every lane knob heard on every model**, marked `(SF-000)`. `Synth.cpp`: the model tables
  (a state-variable filter where a model had none; an ensemble amount where it had none; a
  sustain under full on SINE SUB and four of PAD's; `tilt` on three models: COLOR as a tilt);
  DETUNE in the 303, the organ (each drawbar its own phase), FM 4-OP, sync (`sync2`) and noise
  (`nbp2`) engines, never on BASS; a slow model's echoes at a third of its attack. `Drums.cpp`:
  RIM's TONE, SHAKER's TUNE, TOM 808's burst; RUMBLE's SUB DRONE and SAMPLE take SIZE, WIDTH,
  PREDELAY and DENSITY. `Voice.cpp`: SIGNAL's band on SAMPLE and STUTTER, TONE on RADIO.
  **The suite's own sound is changed by these**, the rig's opening sound included (SINE SUB is
  the bass's layer). See DECISIONS, sixty-first build.
- `core/Sample.cpp`, the root: the first lag that is a peak within 3 % of the strongest, with a
  parabola through its neighbours, in place of the strongest lag. `(SF-000)`.
- `modules/Finish.cpp` is SF-000's own (the FINISH module: the master's stage before its
  limiter), with a line for it in `core/Registry.h` (and `FinishSays`, what it says of itself
  to whoever holds it) and `core/Registry.cpp`, marked `(SF-000)`. It is not in the suite. See
  DECISIONS, sixty-fourth build.
- `modules/Master.cpp`, LIMIT's AUTO: only a short-term reading of a whole three seconds is
  remembered as a second's loudest (`fed`), marked `(SF-000)`. The meter, just prepared, says
  what it has heard so far, and the set's first hit, kept a minute as the loudest, held the
  trim low until it left the memory: the set came up 0.9 LU a minute in. The first three
  seconds are still trimmed by what the meter says. **The suite's own LIMIT has this still.**
  See DECISIONS 563.
- `modules/Synth.cpp`, GRANULAR's read, marked `(SF-000)`: `GrainRead` (sixteen points from a
  table, both channels in one pass) and, in `Voice::Grain`, the copy a grain reads and its
  window's turn, settled as the grain starts. A grain above the source's pitch reads the
  octave-down copy on which it advances a sample a step at the most; it read the one above
  through `sampleAt` (18 to 36 points a channel, a look-up each) with a cosine a sample for
  its window, and sixteen notes of it took a core and a half. A grain at or below the source's
  pitch is read as before. **The suite's own GRANULAR has the old read still**: the sound is
  the same to 74 dB under a pad measured both ways. See DECISIONS 576.
- `core/Bus.h`, `core/Bus.cpp`, marked `(SF-000)`: `Bus::processLocal`. Set before the bus is
  first asked for, it makes this process's bus its own memory and not the block shared by
  name. It is for the cue's rig, a second SF-000 process that runs a second rig beside the
  first (`Source/CueRig.h`): on the shared block it ducked under the first rig's kick, and
  would have written its key and its lanes' energies where the first reads them. The suite
  has no such process and nothing there changes. See DECISIONS 598.
- **Eighty-fifth build, all marked `(SF-000)`. None of it is in the suite.** `core/Wavetable.h`,
  new: `Wavetable` (many frames, each band-limited a level an octave as `Mipmap` is, a level
  only as long as its harmonics need) and `WavetableSet` (eleven tables of 64 frames, made
  once for a rate). `modules/Synth.cpp`: TABLE's list is CLASSIC, the eleven, FILE, SAMPLE
  (it was BUILT-IN, SAMPLE: the first and last have not moved); `wt_twist`, `wt_twist_as`,
  `wt_env`, `wt_wander`, `wt_from`, appended; `fineFrom` (64 frames from a recording, with its
  level), `fileFrom` (a wavetable file read from the sample's own copy of it), `tablesFrom`
  (what a sample makes, kept once for every holder of it); the oscillator reads `wtTab` where
  there is one and what it always read where there is not; the display draws the table. The
  suite's WAVETABLE has the four waves and eight frames from a sample. See DECISIONS 727 to 731.
- **Eighty-fourth build, all marked `(SF-000)`. None of it is in the suite.** `core/Module.h`:
  `meter (int)`, a figure a module reports to its host. `modules/Master.cpp` (LIMIT): `meter`
  (0 the limiter's gain reduction, 1 what the clipper takes off the block's loudest sample, 2
  the automatic trim, 3 the true peak, 4 the trim the last minute's loudest asks for); `hold`
  and `held`, appended: the trim held at a figure. `modules/FxTime.cpp` (REVERB) and
  `modules/Terroir.cpp`: `duck_for`, appended: DUCK for the kick and the source, the kick, or
  the source. `modules/Synth.cpp`, `Voice.cpp`, `Drums.cpp` (RUMBLE): `pocket_post`, appended:
  the instrument leaves its pocket to the host (depth 0 given to `RoleOutput::process`), which
  makes it after the lane's effects with the same `Pocket`. `core/Pocket.h`: `idle()`. The
  pocket's own arithmetic is untouched. See DECISIONS 722 to 726.
- **Seventy-ninth build, all marked `(SF-000)`. None of it is in the suite.**
  `modules/Synth.cpp`: GRANULAR's own parameters (`gr_pos` ... `gr_sync`, appended to the enum
  and the list) and its block rewritten around them (the same sound where they start; the
  same order of chance); GRANULAR appended last to BASS's and LEAD's models; SAMPLE's
  `smp_notes`, `smp_loop`, `smp_xfade`, `smp_vel`, `smp_rand` (appended) and its voice start
  and read; a file read to five minutes. `modules/Kit.h`: `sliceSpan`, `sliceCount`,
  `sliceRegion`, `withSlices`, `hitRegion`. `core/Sample.h`, `Sample.cpp`: `SampleData::onsets`,
  every onset found (`slices` still the first 32, as VOICE reads them). `modules/Drums.cpp`:
  `smp_slice`, `smp_cycle` on KICK, PERC and RUMBLE (appended); KICK FORGE, the kick's eighth
  model after SAMPLE (`ForgeShape`, `kForge`, `forgeAt`, `kForgeTrim`, `Ground`, `forgeStart`,
  `forgeVoice`, `fg_char` ... `fg_tone` appended; `kKickSample` names SAMPLE's number where
  "the last model" was meant). **Brought into the suite, a model list longer by one moves the
  place of every model after the new one in presets that hold a place and not a name**: here
  the new ones are last, or after SAMPLE, and SF-000 converts its own snapshots (DECISIONS 707).
- **Eighty-third build, all marked `(SF-000)`. None of it is in the suite.** Slice markers:
  `modules/Kit.h`: `kCuts`, `Cuts`, `withCuts` (`smp_cuts` and `smp_cut01` ... `smp_cut32`,
  appended last on KICK, PERC, STRIKE, RUMBLE, the synths and VOICE), `cutsFrom`, `cutsOf`,
  `handStarts`, `autoStarts`; `sliceCount`, `sliceRegion` and `hitRegion` take them;
  `SampleStrip` draws them, moves them (`mouse`, `mouseKit`) and puts them in and takes them out
  (`doubleClick`, `doubleKit`), and takes START or END only from a press near one where there
  are markers. `core/Module.h`: `displayDouble`. `modules/Voice.cpp`: `cutCount`, `cutStart`,
  `startsNow`. The pitch wheel: `core/Module.h`: `Ctx::bend`; `core/Processor.h`, `.cpp`:
  `bendSt`; each instrument adds `c.bend` where it reads TUNE. `core/Ui.h`: `kWords`, and
  `frame`'s title in a colour given; the sample displays' wording uses it. See DECISIONS 719, 720.
- **Eighty-first build, all marked `(SF-000)`. None of it is in the suite.** Samples in stereo:
  `core/Sample.h`, `Sample.cpp`: `SampleData::wide` (the channels differ). `modules/Synth.cpp`:
  `GrainRead::both` and the grains' read (left and right apart); `Voice::svfS`, the filter
  again for a stereo file's sides on SAMPLE and GRANULAR (the other engines' `sideK` as it
  was). `modules/Drums.cpp`: KICK's SAMPLE and KICK FORGE's impact carry the left less the
  right beside the kick (`sideIn`, `decS`, `decSB`, `LayerSplit::widen` in `Kit.h`; `fgWide`,
  `resS`); PERC's SAMPLE (`Voice::side`); RUMBLE's SAMPLE (`pre[1]`). `modules/Voice.cpp`:
  SAMPLE and STUTTER (`sideBuf`, the oversampler's second channel). A file of one channel, or
  the same in both, takes the old path in each. **LOOP**: `modules/Kit.h`: `withLoop`
  (`smp_bars`, appended last), `loopBars`, `SampleLoop`; `sampleGateSeconds` and
  `sampleGateGain` moved there from `Drums.cpp`; PERC uses it. `modules/Strike.cpp`: **SAMPLE,
  a seventh model after the six objects** (`kStrikeSample`), a sample slot, the region, gain,
  slice and loop parameters (appended), `Shot` voices, the loop, DRIVE in FRACTURE's
  oversampler, the sample under its display. The suite's STRIKE has six objects and no file.
  See DECISIONS 715, 716.
- `core/Pocket.h`, marked `(SF-000)`, seventy-ninth build: **the pocket is another thing here.**
  The hole is the kick's own envelope (its reported decay), turned over and squared, on the
  whole lane's level; DEPTH is its depth and SHAPE its length (a quarter to the whole of the
  kick's). The suite's is as it was: a notch at the kick's pitch, through the lows, to a pump,
  on a decay held to a part of a beat and shortened when kicks come thick. The crossover and
  the bell are still in the file and do nothing here. See DECISIONS 711.
