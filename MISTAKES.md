# Mistakes log

## `assert(side_effect())` silenced by NDEBUG in Release builds

**What happened:** `rdf_TurtleStoreTest` SEGFAULTed only in CI Release builds
(`cmake -DCMAKE_BUILD_TYPE=Release`). Debug builds passed locally.

**Root cause:** Six `parse`/`parseFile` calls were wrapped inside `assert()`:
`assert(store.parse(...))`. In Release mode CMake defines `NDEBUG`, which expands
`assert(expr)` to `((void)0)` — the expression is **never evaluated**. The parse
never ran, the store remained empty, and the immediately following
`circuits[0]` access on an empty vector caused the SEGFAULT.

The initial diagnosis was wrong (suspected serd struct layout difference between
0.30.x and 0.32.x). Removing `libserd-dev` from CI apt-installs had no effect
because that was not the cause.

**Prevention:** Never call a function with observable side effects inside
`assert()`. The correct pattern is:
```cpp
const bool ok = store.parse(...);
assert(ok);
```
This ensures the call happens in all build types. Any function whose return value
you need to assert on must be called before the assert. Rule promoted to CLAUDE.md.

---

## Bass drum VCA silenced by velocity arc through ControlMultiply

**What happened:** The bass drum in `examples/909.ttl` produced no audible output. The TwinTBridge oscillator was triggering and the Tanh stage was processing, but the VCA downstream was closed.

**Root cause:** The amp envelope output was routed through a `val:ControlMultiply` that multiplied it by the `NoteGate` velocity. The velocity goes to 0.0 on note-off. Because a control arc replaces its destination value each block, the VCA cv became `envelope × 0 = 0` the moment the MIDI note-off arrived — before the 500 ms TwinTBridge decay had a chance to sound.

A second flaw: `TwinTBridge` read `args.velocity` (the global last-note velocity) for initial amplitude at trigger time. In a polyphonic drum kit this picks up whichever note fired most recently, not necessarily the bass drum.

**Prevention:** For a drum voice, the VCA should be controlled by the amp envelope alone — the envelope provides the gate shape, and the oscillator's internal amplitude provides velocity sensitivity. When wiring a TwinTBridge voice, connect the amp envelope directly to the VCA cv and route the NoteGate velocity to the TwinTBridge velocity input port (added in this fix). Avoid ControlMultiply in the VCA cv path unless both inputs stay non-zero for the full decay duration. Rule promoted to CLAUDE.md.
