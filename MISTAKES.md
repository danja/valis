# Mistakes log

## Control arc semantics not documented at introduction

**What happened:** `examples/sh101.ttl` was written with `val:cutoff 800.0` on
the Ladder instance and a control arc `envScale.out → vcf.cutoff`. The filter
cutoff knob had no audible effect.

**Root cause:** A control arc that targets a port *replaces* the port's value
every block (the engine writes `controlStore[slot] * depth`, not `+=`). The
fixed `val:cutoff` property was overwritten on every block by the arc, making
the host parameter ineffective. The resting cutoff must live inside the Scale's
`val:min`, not on the Ladder instance.

**Prevention:** Document the replacement rule in the architecture section of
CLAUDE.md and in any case study that introduces control arcs for the first time.
When writing a circuit, ask: "does any control arc target this port?" — if yes,
the port's fixed value is unreachable.

---

## Topological sort excluded control arcs

**What happened:** Pitch tracking lagged by one block; control-source nodes
(MidiPitch, Envelope) were processed in alphabetical rather than dependency order.

**Root cause:** The compiler's Kahn's-algorithm sort built its adjacency from
audio arcs only. Control-source nodes had in-degree 0 from audio arcs and were
ordered alphabetically. `pitch` sorts after `osc`, so the oscillator received
stale frequency data for one block on every note event.

**Prevention:** The topological ordering adjacency must include control arcs
(use a separate adjacency for cycle detection, which must stay audio-only to
avoid counting control cycles as true feedback). Check this whenever a new
arc type is introduced.

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
you need to assert on must be called before the assert.

---

## OpenHarness MCP client drifted from installed MCP library

**What happened:** OpenHarness reported all configured MCP servers as failed even
though the same servers worked in Claude. HTTP servers (`valis`, `transmission`)
failed with `not enough values to unpack (expected 3, got 2)`. The stdio server
(`reaper-mcp`) failed with `'Tool' object has no attribute 'inputSchema'`.

**Root cause:** The local OpenHarness 0.1.9 install expected an older MCP Python
API than the one installed in its virtualenv. `streamable_http_client(...)`
now yields two streams, but OpenHarness unpacked three values. MCP `Tool`
objects now expose `input_schema`, but OpenHarness read `inputSchema`.

**Prevention:** When MCP servers all fail at once but work in another client,
check the local client library compatibility before changing server configs.
Inspect the installed function signatures and model fields in the active
virtualenv, then patch or upgrade the client to match.

---

## ParamBinding range not overridable

**What happened:** `val:Scale` ports need a ±1e6 range for general numeric use.
When a Scale is used for audio-frequency control, the host automation lane showed
a nonsensical ±1 000 000 range and the "Cutoff" knob was unresponsive across
most of its travel.

**Root cause:** `ParamBinding::minimum`/`maximum` were concrete `double` fields
defaulting to 0/1 with no way to override the port's ontology-declared range for
a specific binding.

**Prevention:** Make `ParamBinding::minimum`/`maximum` `std::optional<double>`.
A `val:Param` node may carry `lv2:minimum`/`lv2:maximum` to override the port
range for that binding only. Document this in the val:Param section of the manual.
