# The suite's sound code in SF-000

`suite/core`, `suite/modules` and `suite/third_party` are copied from
[wileydavis/SF-Live-Suite](https://github.com/wileydavis/SF-Live-Suite) `main` at `4eb9405`
(Nov 10, 2026). SF-000 never writes to that repo.

Changes made to the copy:

- `core/Bus.cpp`: the shared-memory name is `/sf_000_bus_v1`, so SF-000 never shares a bus with
  the suite's plugins running in Live on the same Mac.

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
