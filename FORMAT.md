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
  "library":   [ snapshot, ... ],
  "knobs":     [ { "name", "depth", "snaps": [ids], "links": [ { "c": id, "d": amount } ] } x 16 ],
  "defs":      { "<print>": definition, ... }
}
```

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
  "ext":   { "<tool>": anything }             other tools' notes: SF-000 carries them and never reads them
}
```

- **`values`**: every stored control by id. A lane's knob is `<slot>.<param>`; an insert's cell
  is `<slot>.i<insert>.c<cell>` (by place: what the place is depends on the effect in `inserts`);
  the lane strip's own are `<slot>.send_a`, `.eq_low`, `.gen_hits` and so on; the returns are
  `ret.dub_*`, `ret.space_*`, `ret.conv_*`; the drum bus's four are `bus.drive`, `bus.crunch`,
  `bus.punch`, `bus.boom`. A control missing from a snapshot is left where it is.
- **`setup.bus`**: the drum bus's CONFIG, by parameter id (`damp`, `boom_tune`, `type`, ... and
  `in_1` to `in_11`: which lanes play through it, in `made.slots` order). Absent on a snapshot
  from before there was a bus (twenty-eighth build): the bus is then left as it is.
- **A step** may grow more numbers after the sixth; a reader takes the ones it knows.
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
