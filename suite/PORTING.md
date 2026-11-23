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
- **Ninetieth build, marked `(SF-000)`. None of it is in the suite.** `modules/Master.cpp`:
  `GlueModule` is written again in place, with the suite's id (GLUE) and other controls
  (threshold, attack and release as continuous times, eight ratios, makeup, knee, auto, start):
  it is not the suite's GLUE and a preset or a state of the suite's does not fit it.
  `modules/Presets.cpp`: `glue()` is its three places to start. `modules/FxDrums.cpp`: COMP is
  crossed over 20 ms and its envelope kept going while off; held on or off it is sample for
  sample what it was. See DECISIONS 742 to 744.
- **Eighty-seventh build, all marked `(SF-000)`. None of it is in the suite.** One wavetable:
  `core/Wavetable.h`: `WavetableSet` has twelve tables, CLASSIC the first (the four waves of
  `WaveSet::frame`, as 64 frames at the levels they had). `modules/Synth.cpp`: the oscillator
  reads `wtTab` and nothing else (the four-wave reading and TABLE SAMPLE's eight `Mipmap`
  frames, `framesFrom`, are out); TABLE opens on BASIC; `wt_from` is LEVELS, two choices; what
  cannot be had plays BASIC. `WaveSet::frame` is still built (the suite's file is not edited
  for it) and `frame[0]` is still the sine FM reads. See DECISIONS 737.
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

- **Airwindows** (the ninety-first build), which is not the suite's. `suite/airwindows/` holds
  seven effects from [airwindows/airwindows](https://github.com/airwindows/airwindows) at
  `143a86d` (Nov 14, 2026): `plugins/LinuxVST/src/<Name>/<Name>.h`, `<Name>.cpp` and
  `<Name>Proc.cpp` for Weave, DeRez5, Dattorro, TakeCare, ToTape9, ADClip9 and Galactic3, **not
  edited**, and his `LICENSE` (MIT, Copyright (c) 2018 Chris Johnson), which goes wherever they
  go. `suite/airwindows/audioeffectx.h` is SF-000's: a stand-in for the VST 2 header those
  files include, with just enough of `AudioEffectX` for a kernel to be made, told its rate,
  given its controls and run. Nothing of Steinberg's is in the repo.
  `suite/modules/AirKernels.cpp` is the one file that compiles them, each inside a namespace
  of its own (every one declares the same names at file scope), with `rand` given as a small
  generator of that file's own so that a kernel made twice is the same twice.
  `suite/modules/AirKernels.h` is the interface (`fresh`, `set`, `run`), and
  `suite/modules/Airwindows.cpp` the adapter that makes seven SF-000 modules of them
  (`AIRWEAVE`, `AIRDEREZ`, `AIRFILTER`, `AIRENSEMBLE`, `AIRTAPE`, `AIRCLIP`, `AIRGALAXY`), with
  their lines in `core/Registry.h` and `core/Registry.cpp` marked `(SF-000)`. To take a newer
  Airwindows file: copy it over the old one; if its controls have changed, the table at the top
  of `Airwindows.cpp` is where they are named. To add another: its three files, a namespace
  block and a `Kind` in `AirKernels.*`, a row in that table, a `make`, the two Registry lines,
  an `FxDef` in `Source/Rig.h`, and its lines in `Source/Help.h`. See DECISIONS 746 to 750.
- **The ninety-ninth build, KICK FORGE finished**: `modules/Drums.cpp` appends `FGHANG` ...
  `FGGWIDTH` (`fg_hang`, `fg_curve`, `fg_hold`, `fg_hdec`, `fg_phase`, `fg_flip`, `fg_ilen`,
  `fg_ipitch`, `fg_icut`, `fg_iwidth`, `fg_vary`, `fg_solo` (momentary), `fg_gdark`,
  `fg_gduck`, `fg_grec`, `fg_gwidth`); `forgeFall` (the pitch's fall with HANG and CURVE);
  `Ground::shade` (DARK's cut-offs and tilt, DUCK, RECOVER, WIDTH's side through four one-pole
  high-passes), `Ground::tick` gives a side; `groundShade`; a hit's VARIATION, its own `Noise`
  when locked, IMPACT WIDTH's side (`fgDly`). The module opens its stereo path for either WIDTH.
  At rest the kick is the same to the sample. All marked `(SF-000, the ninety-ninth build)`.
- **The ninety-eighth build, the parameter contract**: `core/Module.h` has `enum class Reach
  { Live, Eased, Next }`, `ParamSpec::reach` (LIVE unless said), `eased()`, `next()`,
  `liveOn (model)` and `reachIn (model)`; every module's parameters that a sounding note does
  not follow say `.next()` (KICK's, PERC's, STRIKE's, RUMBLE's, VOICE's, the synth's DENSITY,
  the echo's two, KEY FOLLOW, the sample's own in `modules/Kit.h`), LAYER MODEL, CHORD and MONO
  `.eased()`. `modules/Voice.cpp`: a sounding phrase follows GATE (`Play::age`, `relMul`).
  `modules/Drums.cpp`, KICK FORGE: `forgeFollow` takes TONE, DRIVE, BODY and LOW PRESERVE at
  the hit and every block after; `fg_glen` and `fg_low` appended (`FGGLEN`, `FGLOW`); the
  ground's length and level followed while it rings (`Ground::follow`, `groundLen`,
  `groundFor`, a feed into it), and LOW PRESERVE takes the body's fundamental round the drive
  and the clipper. At rest the kick is the same to the sample. All marked `(SF-000)`. The
  suite's own modules have none of it: the contract is worth taking there (DECISIONS 775).
- **The ninety-fourth build**: `core/Module.h` has `holdsNotes()` (true), which
  `modules/Synth.cpp` answers from its model (false on SAMPLE with LOOP off: `holds`), for
  SF-000's sequencer; `core/Dsp.h` has `levelGain` and `modules/Kit.h`'s `levelDb` uses it,
  so an instrument's LEVEL at -36 dB is nothing and its last 3 dB fall away to that (the
  suite's own is -36 dB there: worth taking, DECISIONS 761). Marked `(SF-000, the
  ninety-fourth build)`.
- **`modules/Synth.cpp` and `modules/Calibration.h`, the ninety-third build**: the engines more
  than one of BASS, LEAD and PAD has (SAMPLE, WAVETABLE, FM 4-OP, GRANULAR, ACID 303-TYPE,
  MONO 101-TYPE) are one definition each (`sharedModel`, `SynthModel::shared`), LEAD's as it
  was, with its level; their places in `cal::bass`, `cal::lead` and `cal::pad` are nought. Two
  parameters at the end of the list: `release` (every model's release times a tenth to ten,
  one in the middle: `setEnvelopes`, followed by a sounding note in `updateControls`) and
  `mono` (on for BASS). `monoOf` and `centred` stand where `m.mono` and the role were read:
  for a shared engine MONO says whether it is one voice and whether the ensemble, FM 4-OP's
  spread, WAVETABLE's and the sample's second reading and DETUNE's second voice play; for a
  lane's own model, as before. All marked `(SF-000, the ninety-third build)`. LEAD's output
  is the same to the bit; BASS's and PAD's six are not, by design (DECISIONS 755). The suite's
  own Synth still has three versions of each.
- **`modules/Synth.cpp`, the ninety-second build**: a sounding note follows LAYER, LAYER MODEL,
  CHORD, ATTACK, DECAY, GATE and ACCENT (each was read when the note began and no later). The
  note's gain before LAYER's share is kept (`gain0`) and the share worked out every sub-block
  (`layerGain`, `updateControls`); the envelopes' times are made in one place
  (`setEnvelopes`) from where a note starts and from `updateControls`; `process` plays the
  keys down again on another CHORD and starts or lets go the layer when LAYER crosses nothing
  or LAYER MODEL changes. All marked `(SF-000, the ninety-second build)`. With no knob moving
  the output is the same to the bit. The suite's own Synth has the same seven read at the
  note's start: worth taking there too (DECISIONS 751).
- **The hundredth build: SAMPLE, one engine** (`modules/SampleEngine.h`, `.cpp`,
  `SampleDisplay.cpp`, new and SF-000's own). Every lane module that had a SAMPLE model (KICK,
  PERC, STRIKE, RUMBLE in `modules/Drums.cpp` and `Strike.cpp`, the synths in `Synth.cpp`, VOICE)
  appends its `sp_` parameters last and sends that model's notes to it; their old SAMPLE code
  is taken out (the `smp_` parameters stay for GRANULAR, TABLE SAMPLE and VOICE's phrases).
  `core/Sample.h`, `.cpp`: `SampleData::hits` (transients with a strength) and `::view` (the
  display's bands, 256 samples a block), made as a file is read. `core/Module.h`:
  `displayWheel`, `StepsView` and `stepsOf` (the lane's steps, for the display),
  `ParamSpec::nextOn` and `quick` (`quickly()`: taken as set, with no 30 ms ease;
  `core/Processor.cpp` honours it). All marked `(SF-000, the hundredth build)`. Nothing of it is
  in the suite, and it would need SF-000's engine for its clock and steps (DECISIONS 780).
- **The hundred and second build: SYNTH and KICK FORGE on every lane.** `modules/SynthEngine.h`,
  `.cpp`, `SynthDisplay.cpp` (new): the SYNTH engine, appended last (`sy_` ids, `syn::with`) to
  every lane module, its notes sent to it as SAMPLE's are. `modules/Forge.h` (new): KICK FORGE's
  sound, moved out of `Drums.cpp`'s KICK unchanged (the kick's `Voice` is a `forge::Hit` now, its
  ground a `forge::GroundUnit`; `--forgeprint` the same to the bit), and `modules/ForgeEngine.h`,
  `.cpp` (new): the Forge for every other lane (`fg_` ids, `frg::with`). `core/SampleTables.h`
  (new): WAVETABLE's frames from a recording, moved out of `Synth.cpp` unchanged, with `scan`
  (a frame a hundredth of a second, for SYNTH) beside them. `modules/Strike.cpp`, `Drums.cpp`
  (KICK, PERC, RUMBLE), `Synth.cpp`, `Voice.cpp`: SYNTH and KICK FORGE last on each list, wired as
  SAMPLE is. `modules/SampleDisplay.cpp`: the view zooms only by the wheel. All marked
  `(SF-000, the hundred and second build)`; none of it is in the suite (DECISIONS 784).

- **The hundred and third build: every lane one instrument.** `modules/Lane.cpp` (new): the
  instrument of all eleven lanes, one `LaneModule` keyed by its role (whether it plays notes,
  ducks under the kick, follows the kick's hits, is the kick), its models SAMPLE, SYNTH and KICK
  FORGE and nothing else. `modules/Drums.cpp`, `Strike.cpp`, `Synth.cpp` and `Voice.cpp` are
  gone from SF-000's copy, and with them every model of their own; `modules/Presets.cpp` has no
  instrument presets. `modules/ForgeEngine.h`, `.cpp`: `begin` takes the key's root (a drum
  lane's KEY FOLLOW), `shapeOf` says the kick a hit makes for the bus, `hzOf` where it lands.
  `modules/SynthEngine.h`, `.cpp` and `modules/SampleEngine.h`, `.cpp`: `gateNow` and
  `keySounding` (GATE turned as notes sound, and a key still held played again), and
  `releaseScale` (GATE's lower half shortens RELEASE, its upper half lengthens it). All marked
  `(SF-000, the hundred and third build)`. The suite keeps its own instruments: nothing of this
  goes back to it (DECISIONS 785).

- **The hundred and fourth build: what the manual found.** `core/Module.h`: `ParamSpec::lo`, `hi`
  and `ends()`, a "BIP" knob's two sides by name; `core/Processor.cpp` says them (and "-" / "+"
  a signed depth) where it said LP and HP. `modules/ForgeEngine.cpp`: its eight bipolar knobs
  named. `modules/SampleEngine.h`, `.cpp`: GRAIN's POSITION heard at once (`posAt`); STRETCH
  loops only what will be let go; SCAN named. `modules/SynthEngine.h`, `.cpp`: SYNC, BURSTS and
  BURST GAP kept by the note; W VEL and W KEY live and signed; DIODE hears MOD ATTACK and KEY
  TRACK (ACID 303's KEY TRACK nought); ACCENT at nought is nothing; CLEAN BP and HP filter the
  side (`svfS`). `modules/Lane.cpp`: a lane holds its notes only where SAMPLE or SYNTH sounds.
  All marked `(SF-000, the hundred and fourth build)` (DECISIONS 786).

- **The hundred and eighth to tenth builds: three inserts.** `modules/Resonator.cpp`: its chord
  tables made in `prepare` (they were first made on the audio thread); SHIFT a list of whole
  semitones (`shiftNames`); a string the new chord has not fades over 30 ms (`fade`). New,
  SF-000's own: `modules/FxMove.cpp`, PHASER (PHASER and FLANGER) and SHIFT (a Hilbert-pair
  frequency shifter and a ring modulator), registered in `core/Registry.cpp` and `.h`. All
  marked `(SF-000 ...)` (DECISIONS 790 to 792).

- **The hundred and eleventh build: what the Build 110 manual found.** `modules/FxMove.cpp`: SHIFT's
  TIME from 2 ms, its repeats never read from inside the chunk. `modules/SynthEngine.cpp`: a MONO
  voice begun again takes SYNC, BURSTS, BURST GAP and ACCENT's guard (a new voice where SYNC wants
  4x); DIODE's KEY TRACK a factor on the whole cutoff. `modules/SampleEngine.cpp`, `.h`: GRAIN on
  SYNC levelled by SYNC's pace (`grainsPerSecond`); a note played THROUGH holds nothing.
  `modules/SampleDisplay.cpp`: a double click with WARP off turns it on with its markers. All
  marked `(SF-000, the hundred and eleventh build)` (DECISIONS 793).

- **The hundred and twelfth build: every lane plays alike.** `modules/Lane.cpp`: PLAYS (HITS, NOTES) on every lane, read
  each block, standing for what `LaneRole::pitched` decided (it is now only where each lane opens); the ninth knob read
  as DECAY or GATE by it. `modules/SynthEngine.h`, `SampleEngine.h`, `ForgeEngine.h`: `pitched` and `ends` atomic, set
  by `plays()` as the lane turns. `modules/SynthDisplay.cpp`: the wave and SHAPE's strip always drawn, NOISE under it at
  its level. All marked `(SF-000, the hundred and twelfth build)` (DECISIONS 794).
