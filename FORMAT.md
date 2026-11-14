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
  "samples":   [ { "file", "from", "sha256", "bytes" } ],   the copies of audio files this library has made
  "library":   [ snapshot, ... ],
  "knobs":     [ { "name", "depth", "snaps": [ids], "links": [ { "c": id, "d": amount } ], "pick"? } x 16 ],
  "arrangement": { "parts": [ { "snap": id, "bars": 8, "morph": 0 } ] },   snapshots in a line (absent: none)
  "stacks":    [ { "id", "name", "kind", "snaps": [ids] } ],          the stacks LOAD brought from the web library (absent: none)
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

A knob with `"pick": true` (thirty-ninth build on) holds a stack: it steps through its `snaps`
(up to 128) and recalls the one it is on when pressed; it does not sweep through them, and
`depth` is not used. Absent, the knob sweeps. A build from before reads such a knob as a macro
of that many snapshots and carries `pick` back out.

A knob's `links` are the knobs it is tied to (LEARN): `c` is the knob's id as `values` has it
(`bass.cutoff`), `d` how far the full turn takes it, as a share of that knob's whole travel from
-1 to 1 (the knob's `depth` multiplies it). Absent on a knob tied to nothing. A link to an id
this build does not have is written back as it was read.

`libraryId` is sixteen hex digits made once for this library. It is also kept in
`library-id.txt` beside the session, so it outlives a build that drops it from the file.

## A snapshot

```
{
  "id":    1794442207135,                     this library's handle for it; its clip is clips/<id>.ogg
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
}
```

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
- **`stacks`** (thirty-eighth build on): the stacks the web library's tray held at the last LOAD,
  in its order. `id` is the web library's own name for the stack, `kind` is `stack` or `song`,
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
