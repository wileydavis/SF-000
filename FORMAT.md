# The session file

`session.json` holds the library of snapshots, the snapshot knobs and the rig as it was left. This
page is the contract for it: what is in it, and the rules that let it grow without an old file ever
being lost or misread. The same rules hold for anything else that comes to read or write
snapshots: a later SF-000, a web library, a script.

## The rules

1. **Additions only.** A key, once written, keeps its name and its meaning for good. New things
   are new keys. The format number (`"sf000": 1`) stays at 1 for every change of that kind, which
   should be every change.
2. **What you do not know, you carry.** A reader ignores any key it does not know. A writer puts
   it back. SF-000 writes a library snapshot back exactly as it was read (whole, at any depth), so
   a newer build's fields and another tool's notes survive an older build opening and saving the
   file. It also carries keys of the file itself, values for controls it has not got, and what it
   does not know in a snapshot knob.
3. **A snapshot never changes.** Once taken, only its name can change. To improve what is known
   about one, add to it (see `ext`, and the profile's `v`); do not rewrite what is there.
4. **Stored values are raw; what they mean is kept beside them.** A value is a knob's position
   from 0 to 1. Every snapshot says which definitions it was made with (`made`), and the file
   keeps those definitions in full (`defs`). Anything worked out from a snapshot (its profile,
   tags, a place on a map) is marked with what worked it out, so it can be worked out again better.
5. **By name, with the order written down.** Values are keyed by control id. Where a list is in
   lane order (`patterns`, `inserts`, `gen`, `setup.lanes`, the profile's `lanes`), `made.slots`
   says which lane each place is. A snapshot from before `made` was kept has the eleven lanes in
   this order: `kick closed_hat open_hat clap perc bass rumble lead chord pad voice`.
6. **A format this build cannot read is left alone.** If `sf000` is higher than the build knows,
   the file is not read and not written: the app says so, runs on a fresh rig and saves nothing
   that time. The number rises only for a change an older build could not read past; so far there
   has been none.
7. **Nothing is thrown away quietly.** A file that will not parse is copied to
   `session-unreadable-<date>.json` before a fresh one replaces it. Each launch copies the session
   as found into `backups/` (the last twenty).

Builds from before 11 November 2026 (eleventh build and earlier) do not keep rule 2: opening a
newer file with one and saving drops `uid`, `made`, `defs` and anything else they did not know.
The uid comes back the same (see below); `made` does not.

## The file

```
{
  "sf000": 1,
  "wrote":     { "app": "0.1.0 5e2fe06", "at": 1794442207135 },     what saved it last, and when (ms)
  "libraryId": "8f39febfd29e418b",
  "base":      { a snapshot: the rig as it was left },
  "baseId":    the snapshot last recalled or taken,
  "controls":  { "g.tempo": 0.51, ... },      what a snapshot leaves alone: tempo, glide, master, mutes
  "activeLane", "files", "space", "insertFiles":   the rig's own state (samples, impulse responses)
  "recallNotes": true,                        NOTES: whether a recall brings a snapshot's notes (absent: true)
  "transition": { "bars": 8, "level": 3, "dub": 0, "space": 2, "lead": 1, "hit": 1, "leadFile", "hitFile" },   TRANSITION (fifty-ninth build on; absent: as it opens). "bars": 4, 8 or 16; "level", "dub", "space": a place of six, 0 to 5; "lead", "hit": 0 off, 1 the built-in, 2 the file; the files as "files" are written
  "finish":    0,                             FINISH on the master: how much of it, in hundredths (0, 25, 50, 75, 100; another number is read as the nearest; absent: 0, off) (sixty-fourth build on)
  "samples":   [ { "file", "from", "sha256", "bytes" } ],   the copies of audio files this library has made
  "library":   [ snapshot, ... ],
  "knobs":     [ { "name", "depth", "snaps": [ids], "lanes"?: [masks], "links": [ { "c": id, "d": amount } ], "at"? } x 16 ],   "lanes": for a stack knob, what each snapshot takes, a bit each: the eleven lanes, then the returns, the drum bus, the key; and bit 14, N: it brings its notes (absent, as in every file before the sixty-eighth build: it does not); and bit 15, M: a knob turned toward it blends each lane's model and sample to its own (absent, as in every file before the seventy-eighth build: the lanes keep theirs). The same in "stacks" and in a kept knob. A build from before reads the parts and drops what it does not know (N, M). Neither N nor M is said to the web library: they are kept here, on the snapshots still on the knob.
  "module":    3,                             the snapshot module's knobs: 1 to 4 macro; 5 to 12, 14, 15 stack; 13 and 16 none (2: 13 and 16 were stack knobs too. Absent: a session from before the two kinds)
  "stackCue":  true,                          CUE is lit (absent: out)
  "knobsAside"?, "knobsUnseated"?:            knobs waiting to be told to the web library (below)
  "arrangement": { "parts": [ { "snap": id, "bars": 8, "morph": 0, "lanes": n } ] },   snapshots in a line (absent: none). "lanes": the parts of the rig the part takes from its snapshot, one bit each as on a stack knob (fifty-fifth build on; absent: all of them)
  "arrangements": [ { "id", "name", "arrangement": { "parts": [...] } } ], "arrangementAt": id   the arrangements kept (sixty-ninth build), each as "arrangement" is written, and which of them is on the page. "arrangement" is still the one on the page, and is what is read for it: a build from before these plays it and carries these two keys. Absent: one, ARRANGEMENT 1, which is "arrangement" (it is not written while it is the only one and has that name).
  "stacks":    [ { "id", "name", "kind", "snaps": [ids], "lanes"?: [n], "here"?: true, "mark"?: "..." } ],   the rack: the stacks the web library has lit for the instrument, and those kept on the instrument ("here": true: made here). "lanes": one number for each of "snaps", as on a stack knob; absent: every snapshot takes everything. "mark": the stack in a line, as the web library and this one last agreed it was (absent: they never have). One made here may hold no snapshots (fifty-third build on: it is filled by hand); one of the web library's whose snapshots have all gone is not read (absent: none)
  "modules":   [ { "id", "name", "knobs": [16, each as in "knobs"], "motion": { "<control id>": value } } ],   the modules kept (fifty-first build on). The one in the chassis is written as its knobs stand ("knobs", above, is the same sixteen); absent: one, the session's knobs
  "moduleAt":  "<id>",                                           which of them is in the chassis
  "keptKnobs": [ { "id", "kind": "macro" | "stack", "knob": { as one of "knobs" } } ],   the knobs kept by themselves, to put on a place of any module (fifty-second build on; absent: none)
  "stacksGone": [ { "id", "mark", "here" } ],   stacks put out of the rack here that the web library is still to be told of (absent: none)
  "defs":      { "<print>": definition, ... }
}
```

**Audio files** (a lane's sample, an insert's or SPACE's impulse responses) are named in `files`,
`space`, `insertFiles` and a snapshot's `insertFiles` and `laneFiles`. From the thirtieth build on,
SF-000 copies every file it is given into `samples/` in the library folder (the folder
`session.json` is in), and a path that does not start at the root is from that folder down, with
forward slashes: `samples/3fa9c2e1/kick.wav`. The folder's name is the first eight hex digits of
the file's SHA-256. A path that does start at the root was written by an earlier build: the file
where it was then. `samples` lists the copies this library has made: `file` (from the library
folder down), `from` (the whole path it was copied from), `sha256` (all of it) and `bytes`. A
reader given a whole path looks for an entry whose `from` is that path and reads its `file`; only
if there is none does it read the path itself. That is how a snapshot from before copies were
kept, which is not rewritten (rule 3), still finds its sample once the original has gone. Two
entries may name one `file` (the same audio given from two places).

A knob's `lib` and `libmark` (forty-third build on; absent on a knob the web library has never
seen) are the id of the module it is there, and what it held when the two last agreed (its kind,
name, depth, snapshots' uids and links in one line). LOAD goes by them: a knob whose line is
still its `libmark` has not been changed here since. A build from before carries both.

**The two kinds of knob** (forty-fourth build on; `"module": 2`). The first four of `knobs` are
macro knobs: `links` and `depth`, no `snaps`. The other twelve are stack knobs: `snaps` in their
order (up to 128), no `links`, `depth` not used.

- `lanes`, on a stack knob, is one number for each of its `snaps`, in the same order: a bit for
  each part of the rig that snapshot takes (bit 0 the first lane, ... bit 10 the eleventh, bit
  11 the returns, 12 the drum bus, 13 the key). Absent: every snapshot takes everything.
- `at`, on a stack knob while `stackCue` is true: where the knob stood when CUE was lit (the
  knob's own value, `snap.kN` in `controls`, is then where it has been turned to choose).
- A session with no `"module"` is from before the two kinds: any of its sixteen could hold
  `snaps`, `links` or both, and `"pick": true` marked one that stepped through its snapshots.
  SF-000 brings it over when it opens it (README: The snapshot module) and keeps the file as
  it was beside it, as `session-before-two-kinds.json`.
- `knobsAside`: knobs of such a session that found no knob of their kind (`name`, `kind`
  "stack" or "learn", `snaps`, `lanes`, `links`, `depth`). ONLINE sends each to the web library,
  off its module, and takes it out of this list.
- `knobsUnseated`: `{ "id", "slot" }` for each module of the web library whose knob was emptied
  here, until ONLINE has taken it off the library's module.
- A knob's `reseat` (beside `lib`): it was brought over to another place, and the web library's
  module has not been told yet.

A knob's `links` are the knobs it is tied to (LEARN): `c` is the knob's id as `values` has it
(`bass.cutoff`), `d` how far the full turn takes it, as a share of that knob's whole travel from
-1 to 1 (the knob's `depth` multiplies it). Absent on a knob tied to nothing. A link to an id
this build does not have is written back as it was read.

`libraryId` is sixteen hex digits made once for this library. It is also kept in
`library-id.txt` beside the session, so it outlives a build that drops it from the file.

## A snapshot

```
{
  "id":    1794442207135,                     this library's handle for it; its clip is clips/<id>.ogg, its lanes clips/<id>.stems
  "uid":   "8f39febfd29e418b-1a1cd171f9f",    its name everywhere, for good: libraryId, "-", the id in hex
  "name":  "SNAP 010",
  "time":  1794442207135,
  "made":  { ... },                           what it was made with (below)
  "profile": { "v": 1, ... },                 what it sounded like (below); absent if nothing was playing
  "values":  { "kick.tune": 0.4583, "lead.i1.c2": 0.55, ... },
  "patterns": [ { "len": 16, "steps": [ [step, velocity, gate, ratchet, note, octave], ... ] } per lane ],
  "inserts":  [ [ "DELAY", "RADIO", "NONE" ] per lane ],
  "gen":      [ armed generator per lane, -1 none ],
  "setup":    { "lanes": [ {param: 0..1} ], "inserts": [ [ {..} x 3 ] ], "returns": [ {..} x 2 ], "bus": {..} },
  "insertFiles": [ { "lane", "slot", "files": [ { "place", "type", "files": [paths] } ] } ],
  "played":   { "seqMute": [0 or 1 per lane], "in": [ { "lane", "len", "steps", "notes" } ] },
  "laneFiles": [ path per lane, "" none ],
  "ext":   { "<tool>": anything }             other tools' notes: SF-000 carries them and never reads them
  "lists": 3,                                 which lists of models, and which knobs of theirs, it was taken on (seventy-ninth build on; absent: 1)
}
```

- **`lists`** (seventy-ninth build on). A lane's `model` (and `layer_model`) in `setup` is a place
  on its list, 0 to 1, and the lists have grown: BASS and LEAD have GRANULAR last, KICK has KICK
  FORGE after SAMPLE. A snapshot without `lists` was taken on the lists of before (BASS 10, LEAD
  and CHORD 12, KICK 7, PAD 9): a build that knows `lists` reads its places against those. A
  snapshot is never rewritten for this: one read from a file is written back as it was read.
  `listsBy` (an array, a figure a lane, read over `lists` where it is there): a chip the analysis
  made of lanes from snapshots of different builds says lane by lane which lists each was on.
  **A model can have sound knobs of its own** (GRANULAR on BASS, LEAD, CHORD and PAD; KICK FORGE
  on KICK): while it is the lane's model, the lane's first eight knobs are the model's. Their
  ids in `values` are still the lane's usual ones (`pad.detune` is the pad's first place, which
  on GRANULAR is POSITION), so **a value of one of those eight means what the lane's model in the
  same snapshot makes of it**, and what the usual knob held is in `setup` under its own name.
  With `lists` at 2: PAD on GRANULAR, places 1 to 6 are `gr_pos`, `gr_scan`, `gr_size`,
  `gr_dens`, `gr_spray`, `gr_pitch` (and so on BASS, LEAD and CHORD, where 7 and 8 are CUTOFF and
  the lane's own eighth); KICK on KICK FORGE, places 2 to 7 are `fg_char`, `fg_impact`,
  `fg_body`, `fg_ground`, `fg_pos`, `fg_tone`, and 1 and 8 TUNE and DRIVE as ever. Without
  `lists`, a PAD on GRANULAR has those six places as the pad's usual knobs, which GRANULAR then
  read as its size (COLOR) and its scatter (DETUNE).
  **`lists` 3** (eighty-first build): PERC's list has SAMPLE after its six objects, and a snapshot
  with `lists` under 3 has PERC's `model` as a place among six. Nothing else moved between 2 and
  3. PERC on SAMPLE, places 1 to 8 are `tune`, `res`, `echo_space`, `echo_swing`, `spread` (its
  WIDTH), `vel_sens` (ACCENT), `material` (TONE) and `power` (DRIVE).
  The lane's definition (`defs`) says which models these are, in `own`: `[ { "model", "knobs":
  [ 8, each under the id of the place it takes ], "with"?: { param: place } } ]`: a model's own
  knobs, and what choosing it by hand also set.
- **LOOP** (eighty-first build) is `smp_bars` in the `setup` of a lane that has it (the hats,
  the clap, PERC): a place among OFF, AUTO, 1, 2, 4, 8, 16, 32 BARS. PERC's `setup` also has a
  sample's ids from this build on (`smp_start`, `smp_end`, `smp_fade_in`, `smp_fade_out`,
  `smp_gain`, `smp_norm`, `smp_slice`, `smp_cycle`). A snapshot that names none of these is read
  with each where it starts (LOOP off, the whole file).
- **Slice markers** (eighty-third build) are in a lane's `setup`: `smp_cuts` (0 or 1: the slices
  are the file's own, or the markers') and `smp_cut01` ... `smp_cut32`, each a place in the whole
  file (0 to 1). Only the markers in use are written; a setup that names none has none.
- **A lane's EQ** (eightieth build) is in its `setup`: `eq1_type`, `eq1_hz`, `eq1_gain`, `eq1_q` ...
  `eq8_q`, each a place from 0 to 1. `type`: OFF, BELL, LOW SHELF, HIGH SHELF, LOW CUT, HIGH
  CUT, NOTCH along 0 to 1 (sixths). `hz`: 20 Hz · 1000^v. `gain`: -18 dB + 36 v. `q`:
  0.3 · 40^v. Bands 1, 4 and 7 have no `gain` here: theirs is the lane's `.eq_low`, `.eq_mid`
  and `.eq_high` in `values`, as it always was (centre flat, down to -24 dB, up to +9). The
  whole of it is written only where a lane's EQ is not as it starts, and **a lane's setup that
  says nothing of it means the EQ as it starts**: band 1 a low shelf at 200 Hz, band 4 a bell at
  700 Hz (Q 0.6), band 7 a high shelf at 2.5 kHz, the rest off.
- **`values`**: every stored control by id. A lane's knob is `<slot>.<param>`; an insert's cell
  is `<slot>.i<insert>.c<cell>` (by place: what the place is depends on the effect in `inserts`);
  the lane strip's own are `<slot>.send_a`, `.eq_low`, `.gen_hits` and so on (`.gen_seed` has 128
  places from the thirty-seventh build on; the sixteen it had, at fifteenths, write what they wrote); the returns are
  `ret.dub_*`, `ret.space_*`, `ret.conv_*`; the drum bus's four are `bus.drive`, `bus.crunch`,
  `bus.punch`, `bus.boom`; POCKETS' are `<slot>.pk_home`, `.pk_width`, `.pk_hold`, `.pk_yield`
  on every lane and `pk.amount`, `pk.clear`, `pk.speed`, `pk.returns` for the rig (thirty-first
  build on; what a value of each means is in the `pockets` definition). A control missing from a
  snapshot is left where it is: a snapshot from before there were pockets leaves them alone.
- **`arrangement`** (thirty-fifth build on): `parts`, in the order they play. Each is a snapshot
  by its `snap` id, how many `bars` it lasts (1 to 256), and its `morph`: how long the way into
  it takes from its first bar, **in bars** (0: cut; 0.25, 0.5, 1, 2 ... 256), which a build
  reads as the longest length it has that is not over it, and plays as no longer than the part.
  A part whose snapshot is not in the library is dropped on reading. What a build does not know
  of a part, or of the arrangement, is carried.
  `edited: true` (sixty-sixth build on; absent: no): the part's snapshot was made for it by EDIT,
  and the next EDIT of the part replaces it if nothing else holds it.
- **`stacks`** (thirty-eighth build on): the rack. Until the fiftieth build, the stacks the web
  library's tray held at the last LOAD; from it, the rack ONLINE keeps the same as the library's
  (`mark`, `stacksGone`: a stack with no `mark` and no `here` is one an earlier LOAD brought, and
  is the library's to say), in the library's order. `id` is the web library's own name for the stack, `kind` is `stack` or `song`,
  and `snaps` its snapshots by id, in the stack's order (a song may name one twice). Whatever
  else the library said of a stack (a song's `parts`: `uid`, `bars`, `morph`) is carried beside
  them. A snapshot that is not in the library is dropped from a stack on reading, and a stack
  left with none is dropped.
- **`controls`** (the session's own, beside `base`): what a snapshot leaves alone, by id.
  `g.tempo`, `g.glide`, `mst.*`, the mutes, the snapshot knobs `snap.k1` ... `snap.k16`, and
  (thirty-fourth build on) MOTION on the snapshot knobs: `mot.1_wave`, `mot.1_speed`,
  `mot.1_knob`, `mot.1_amount`, and the same for `mot.2_` and `mot.3_`. Each is 0 to 1: a list
  (wave, speed, knob) is its choice's place among the choices, first 0 and last 1 (the waves:
  OFF SINE TRIANGLE SQUARE RAMP UP RAMP DOWN RANDOM DRIFT STEPS ENVELOPE KICK; the speeds: the
  suite's twenty-one, `1/16` to `7 BAR`; the knob: 1 to 16); the amount has none at 0.5 and is
  the share of the knob's turn either way from there, doubled. One a build does not know is
  carried.
- **`setup.bus`**: the drum bus's CONFIG, by parameter id (`damp`, `boom_tune`, `type`, ... and
  `in_1` to `in_11`: which lanes play through it, in `made.slots` order). Absent on a snapshot
  from before there was a bus (twenty-eighth build): the bus is then left as it is.
- **A step** may grow more numbers after the sixth; a reader takes the ones it knows.
- **`played`** (twenty-ninth build on): what was playing that `patterns` does not say.
  `seqMute`, in `made.slots` order: 1 if the lane's own sequencer was muted, so its `patterns`
  entry was not sounding. `in`: one entry for each lane that notes were coming into from outside,
  `lane` its place in `made.slots`; `len` and `steps` are those notes as a pattern (as in
  `patterns`: step 0 is the first sixteenth of a bar), and `notes` the notes themselves over that
  loop, each `[beat in the loop, length in beats, MIDI note, velocity]` (a length of -1: still
  held when it was taken). A lane with no entry had none. Absent altogether: a snapshot from
  before this was kept, whose lanes are taken to have been playing their own `patterns`.
- **`laneFiles`** (twenty-ninth build on): the audio file each lane held, by path (see Audio
  files, above, for what a path is), in
  `made.slots` order. Absent: not known.
- **`ext`** is where another tool writes: `ext.<toolname>`. Tags, an embedding, a place on a map,
  a note. It is the one place a tool may change in a snapshot that already exists.

### `made`

```
"made": {
  "app":     "0.1.0 5e2fe06",
  "slots":   [ "kick", "closed_hat", ... ],                    the order of every per-lane list here
  "lanes":   { "kick": "<print>", ... },                       each lane's definition
  "fx":      { "DELAY": "<print>", "RADIO": "<print>" },       each effect this snapshot has in an insert
  "returns": { "dub": "<print>", "space": "<print>", "conv": "<print>" },
  "bus":     "<print>",                                         the drum bus (absent before there was one)
  "pockets": "<print>",                                         POCKETS (absent on a snapshot that holds no pocket values)
  "chassis": "<print>"
}
```

A print is sixteen hex digits: a hash (FNV-1a, 64 bits) of the definition's text. The same
definition gives the same print on any machine; any change to it gives another. A snapshot with no
`made` was taken before prints were kept: read it by control id, as SF-000 always has.

Today SF-000 reads every snapshot by control id whatever its prints say. They are there for the
time lanes take different modules: a build will then know exactly what each stored value was a
position of, and can load the module it was made with, or translate.

### `defs`

Every definition any snapshot in the file names, by print. Never pruned.

| kind | what it is | holds |
| --- | --- | --- |
| `engine` | one of the suite's sound modules | every parameter: `name`, `type` (float, choice, toggle, momentary), `min`, `max`, `centre`, `unit`, `choices`, `def` |
| `lane` | what plays in a slot | `slot`, `name`, `engine` (a print) and `engineId`, `pitched`, `octave`, `fixed` (parameters set once, plain units), `knobs` (twelve: `id`, `label`, `param`, `kind`, `lo`, `hi`) |
| `fx` | an insert effect | `name`, `engine`, `fixed`, `cells` (eight places: `id` c1..c8, `label`, `param`, `kind`, `lo`, `hi`; null = empty) |
| `return` | DUB, or SPACE as reverb or as convolution | as `fx`, with four `knobs` by full id |
| `bus` | the drum bus | as `return`: four `knobs` by full id (`bus.drive` ...) |
| `pockets` | POCKETS: who has which part of the spectrum | `v`, `bands` (28), and its knobs by id, `lane` (the four every lane has: `pk_home` ...) and `rig` (`pk.amount` ...), each with a `unit` and a `law`: the value in that unit as a formula in `v`, the stored 0 to 1 |
| `chassis` | what every lane has beside its instrument | `v`, the counts, and the strip's and the rig's stored controls: `id`, `label`, `kind`, `choices` |

**From a stored value to a parameter.** A knob (`kind` u or b) at `v` puts its parameter at
`t = lo + v * (hi - lo)` of the parameter's travel. With no `centre` the travel is straight:
`min + t * (max - min)`. With a `centre`, the travel is bent so that half way is the centre:
`min + (max - min) * t ^ (1 / s)`, where `s = log(0.5) / log((centre - min) / (max - min))`.
A menu (`m`) at `v` is choice `round(v * (n - 1))`; a switch (`t`) is on from 0.5. The values in
`setup` are positions along the parameter's whole travel (`lo` 0, `hi` 1), by parameter id.

### `profile`

```
"profile": { "v": 1, "energy", "weight", "bright", "density", "tone", "movement", "wet",   each 0..1
             "bpm", "bars",
             "steady": false,           only when false: see below
             "bands": [16 x 0..255],    the spectrum's shape, 30 Hz to 16 kHz
             "pulse": [32 x 0..255],    where the hits fall in the bar: the low end's sixteen steps, then the top's
             "lanes": [0..255 per lane] }
```

`v` is the way it was worked out (`kProfileVersion`); absent means 1. A profile is worked out
from the clip and the rig's own levels, so a later version can be worked out again from the clip.

The clip and the profile are of bars the rig held the snapshot's state through (nothing changed,
nothing still gliding). `bars` says how many, four at most. `"steady": false` marks the exception:
the rig moved on before a whole bar had been held, and these are the bars it was taken over. A
snapshot taken while something moves has no profile at first; it is added when the bars have come.

## For a tool that writes snapshots

- Give a new snapshot its own `uid` (your own sixteen hex digits, `-`, anything unique to you) and
  an `id` no snapshot in the library has. Keep `made` and the values as you found them if it came
  from SF-000.
- Write only under `ext.<yourname>` in a snapshot that exists.
- Bring the definitions a snapshot names along with it: copy them from `defs`.
- Leave what you do not know where it is.

## A snapshot's lanes: `clips/<id>.stems`

Beside a snapshot's clip, the same bars of each thing the mix was made of, as it went into the mix
(before the master). One file:

```
SF0STEMS1\n
{"rate": 48000.0, "bars": 4, "stems": [{"key": "kick", "bytes": 81234}, {"key": "bass", "bytes": 90112}, ...]}\n
<the first stem: an Ogg Vorbis file of that many bytes><the second>...
```

`key` is a lane's slug (`kick`, `hat`, ... as in control ids), `dub` or `space` (the two returns as
they go into the mix), or `bus` (what the drum bus did: the bussed lanes' sum as it leaves the bus,
less their sum as it came). Only stems that sounded are there, in the rig's order. Every stem is
stereo, as long as the clip to the sample, at the clip's rate. Together they are what the master
chain was given. The file is written once, when the clip is, and deleted with the snapshot. A
snapshot may have none (taken before the fortieth build, or with nothing sounding).

ONLINE sends this file as it is to the web library (`PUT /api/stems/<uid>`, forty-fourth build
on), which keeps it whole and gives one stem of it by its key (`GET /api/stems/<uid>?key=bass`).

## A snapshot's sound without loss: `clips/<id>.lossless`

Beside the clip and the stems, the same bars once more with nothing thrown away, for the library to
be studied from away from the instrument (seventy-seventh build on). The instrument never plays this
file: what it plays is the clip and the stems. One file:

```
SF0LOSSLESS1\n
{"rate": 48000.0, "bars": 4, "bits": 24, "parts": [{"key": "mix", "bytes": 912345, "gain": 1.0}, {"key": "kick", "bytes": 401234, "gain": 1.0}, {"key": "bass", "bytes": 512002, "gain": 0.5}, ...]}\n
<the first part: a FLAC file of that many bytes><the second>...
```

- `mix` is first: the output, after the master, the bars the clip holds. Then each stem that
  sounded, under the keys the stems file uses (a lane's slug, `dub`, `space`, `bus`) and in the
  same order. A stem that was silent is left out, as it is there.
- Every part is a whole FLAC file: stereo, 24 bits, at the rate the interface ran at, as long as
  the clip to the sample.
- `gain` is what the sound was multiplied by before it was written. 1 for nearly everything. A
  stem that ran over full scale (one may, before the master) is written quieter by a power of
  two, which loses nothing, and says so: **divide by `gain`** to have the sound as it was.
- Written once, after the clip and the stems (what the instrument plays is on the disk first),
  and deleted with the snapshot. A snapshot from before the seventy-seventh build has none.

About ten megabytes for four bars at 130 with every lane sounding.

## The log: `log/<YYYY-MM>.jsonl`

What is done with chips, a line as it is done, for the library to learn from (seventy-seventh
build on). A month a file, a JSON object a line, only ever added to. The instrument never reads it.

```
{"k": "1a1e7a958f1-0001-3fc2", "t": 1794888009802, "a": "open", "app": "0.1.0 1a2b3c4", "library": "e1a826a5c30e4b6d"}
{"k": "1a1e7a95a02-0002-3fc2", "t": 1794888010242, "a": "recall", "u": "e1a826a5c30e4b6d-1a1e7a958f1", "parts": 16383, "notes": false, "base": "e1a826a5c30e4b6d-1a1e79f0c11"}
```

Every line has `k` (the line's own name: no two alike, in any run), `t` (when, milliseconds since
1970) and `a` (what was done). `u` is the chip it was done to, by its `uid`. A snapshot the rig came
with is never sent to the web library, so it also has `id` (its number here) and `name`. `base`,
where it is given, is the chip the set was last on (empty: on none). `parts` is which parts of the
rig were taken, a bit each: the lanes in the rig's order, then the returns, the drum bus, the master.
`knob` counts from 1.

| `a` | WHEN | WITH |
|---|---|---|
| `open` | the first line of a run | `app` (the build writing: what a line means is that build's to say), `library` |
| `run`, `stop` | the transport started or stopped (to within three seconds) | `bpm` |
| `take` | SNAPSHOT | `u`, `base`, `running`, `bpm` |
| `recall` | a chip recalled by hand, here or on a controller (not the arrangement's own: see `part`) | `u`, `parts`, `notes`, `base`; from a stack knob's place also `from: "knob"`, `knob` |
| `hear` | a chip put in the headphones | `u`, `parts`, `base` |
| `knob` | a stack knob turned (CUE out) came to a chip | `u`, `knob`, `how: "turn"` |
| `between` | a stack knob pressed between two chips | `u` (the one below), `with` (the one above), `f` (how far between, 0 to 1), `knob` |
| `add`, `drop`, `move` | a chip put into, taken out of, or moved within a stack knob's pile (`to`/`from`/`in`: `"knob"`, with `knob`), a stack of the rack (`"stack"`, with `stack` its id and `name`), or the arrangement (`"arr"`, with `arr` its id) | `u`, `at` (its place, from 0); `add` has `parts`, `move` has `to` |
| `onto` | a stack of the rack put onto a knob | `stack`, `name`, `knob`, `chips` |
| `module` | a kept module put in the chassis | `module`, `name` |
| `arr_to_stack` | the arrangement kept as a stack | `arr`, `stack`, `name`, `chips` |
| `play` | the arrangement started | `arr`, `from`, `parts` (how many it has) |
| `part` | the arrangement came to a part | `u`, `arr`, `at`, `bars`, `morph`, `parts` |
| `delete` | a chip deleted | `u` |

What ONLINE and LOAD do is not the player's doing and leaves no line. A thing done in several
steps is one line (a stack put onto a knob is `onto`, not an `add` for each chip). A tool reading
the log takes a kind of line it does not know as something a later build writes, and passes over it.

## What ONLINE sends for the library to be studied from

After the snapshots, their clips and their stems, and never ahead of them (seventy-seventh build
on). All of it is added to there; nothing a library holds is changed by it.

| WHAT | HOW | WHEN |
|---|---|---|
| the log | `POST /api/actions` `{ "library", "lines": [...] }`, each file from where the library last took it, five hundred lines at most a time. A line sent twice (the same `k`) is kept once | as lines are written |
| the session without its chips | `POST /api/sessions` `{ "library", "at", "app", "session" }`: `session.json` less `library` and `defs` (the library has every chip and definition already) and less the rig as it stands this minute (`wrote`, `base`, `baseId`, `controls`, `activeLane`, `files`, `space`, `insertFiles`, and each knob's `at`: a recall or a turn of a knob is not another session), plus `chips`: `[{ "id", "uid" }]`, since knobs, stacks, modules and arrangements name chips by `id`. Kept there if it differs from the last one sent | when it has changed, once a minute at most |
| a sound file the rig was given | `POST /api/files/have` `{ "hashes" }` once, then `PUT /api/files/<sha256>?name=` for each the library lacks: the file in `samples/`, checked here against its hash first and there again | with the rig stopped, one a pass |
| a snapshot's sound without loss | `PUT /api/lossless/<uid>`: the file above as it is. The library gives one part by its key (`GET /api/lossless/<uid>?key=mix`, with the part's `gain` in the answer's `x-gain`) | with the rig stopped, one a pass, once the library holds the snapshot |

The two large kinds wait while the transport runs: nothing of megabytes is read or sent under a set
being played. A library from before it kept any of this says "not found" and is not asked again
that run. What has gone is remembered in `online-sent.json` (`archive`: how far into each file of
the log, which sound files, the last session's hash; and `lossless` beside each snapshot).

## A knob in the web library

A knob of the snapshot module is a stack there (`/api/stacks`). A **stack knob** is kind
`stack` with `items` in order, each `{ "u": uid, "off"?: [parts] }`: `off` names the parts of
the rig that snapshot does **not** take, a lane by its slug, then `returns`, `bus`, `master`
(absent: it takes everything; a part not named is taken, so a lane a later rig adds comes with
the rest). A **macro knob** is kind `learn` with no items and `extra.links` as a knob's `links`
are here, and `depth`. `slot` is its place on the library's module, 1 to 16 (absent: off it): 1
to 4 take a macro knob; 5 to 12, 14 and 15 a stack knob; 13 and 16 nothing; and the library refuses the other kind. A kind
`macro` there is from before the two kinds: a stack knob if it has items. `GET /api/load` gives
the module as `knobs`: `{ id, slot, kind, name, depth, uids, off?, links? }`, `off` one list
for each of `uids`.
