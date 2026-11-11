# The suite's sound code in SF-000

`suite/core`, `suite/modules` and `suite/third_party` are copied from
[wileydavis/SF-Live-Suite](https://github.com/wileydavis/SF-Live-Suite) `main` at `4eb9405`
(Nov 10, 2026). SF-000 never writes to that repo.

Changes made to the copy:

- `core/Bus.cpp`: the shared-memory name is `/sf_000_bus_v1`, so SF-000 never shares a bus with
  the suite's plugins running in Live on the same Mac.

Nothing else is edited. SF-000 hosts the suite's own `SFProcessor` for every lane, insert, return
and master stage (see `Source/Rig.h` and `Source/Engine.cpp`), so the sound, the 64-sample chunking
and the 30 ms smoothing are the suite's.

To take newer suite code: copy the three folders again and re-apply the change above.
