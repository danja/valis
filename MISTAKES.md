# Mistakes log

## valis-render dropped every note not on a block boundary

**What happened:** `valis-render --note 60 --gate-on 0.02` rendered silence from
`clarinet.ttl`, `sh101.ttl` and `rings-modal.ttl`, while the same circuits made
sound under `tests/engine/ValisEngineTest.cpp`. The circuits were fine.

**Root cause:** the render loop compared the note time to the **start of each
block** for equality: `if (sample == noteOnSample)`. A note at 0.02 s is sample
960, which is not a multiple of the 512-sample block size, so the comparison was
never true and no note-on was ever sent. Only `--gate-on 0.0` worked, which is
why the bug survived: every existing use passed a time that happened to land on
sample 0.

This is the same class of error as the SignalGenerator impulse below, from the
opposite direction: there a per-sample event was gated on the block index, here
a stream-position event was compared against one.

**Prevention:** an event fires in the block that **contains** it:
`if (at >= blockStart && at < blockStart + n)`. Any comparison between a stream
position and a block boundary must be a range test, never equality. The
granular case study renders with `--gate-off`, so a regression shows up as a
silent example.

---

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

---

## SignalGenerator impulse shape depended on block alignment

**What happened:** The `val:SignalGenerator` impulse shape (shape 5) only emitted
when a period wrapped exactly on the first sample of a process block
(`i == 0 && phase < inc`). Most periods produced nothing, and the output changed
with the host's block size — the same class of bug as the M4 control-rate fix,
where rendering at 128 vs 512 samples must be bit-identical.

**Root cause:** The per-sample phase accumulator already tracks period wraps
exactly (`phase < inc` fires once per period). Gating it on the block index
threw that away and reintroduced block-boundary dependence.

**Prevention:** Any per-sample condition in `process` must depend only on the
sample position within the stream (phase, counters carried in members), never on
the index within the current block. A test renders the impulse train over 1024
samples and asserts the exact impulse count and amplitude, so a regression fails
the build.
