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
