# Valis

Virtual Analog LLM Intelligent Simulation — a DAW plugin that builds virtual-analog
circuits from RDF/Turtle descriptions. See `docs/plan.md` for the plan and milestone status.

## Mission

Keep four concerns separate: RDF persistence, the circuit model, real-time DSP, and
the operation surface the UI and MCP both sit on.

```
ui/ (Turtle · Graph · Knobs)   mcp/ (HTTP JSON-RPC)
              └───────┬───────┘
                    ops/          every key operation is one Op
   ═══════════════ │ ═══════ message thread only ═══════
             rdf/ · model/ · compiler/
   ═══════════════ │ ═══════ real-time boundary ═══════
                engine/ · dsp/
```

## Procedures

- Log mistakes in `MISTAKES.md` (what happened, root cause, prevention).
- Read `TODO.md` at the start of each session; carry out outstanding tasks and remove completed ones.
- Use the Read tool rather than sed
- Call MCP tools directly rather than using curl

## Test hosts

A live running instance of Reaper may be available for end-to-end testing via the
`reaper-mcp` MCP server. A live instance of the Transmission host (`/home/danny/github/transmission`)
may also be running and reachable via the `transmission` MCP server. Just ask the user
to confirm a running instance before using MCP tools that require a host.

## Real-time rules (non-negotiable)

- No allocation, filesystem, network, unbounded logging, or unpredictable locks in `processBlock` or any element's `process`.
- RDF parsing, model building, and compilation happen on the message thread only.
- `CompiledCircuit` is fully preallocated on the message thread and handed to the engine by atomic pointer swap; the retired one is freed on the message thread.
- Surface failures (bad Turtle, unknown element, cycle) as located, recoverable errors. Never silence, never crash.

## Architecture rules

- UI never mutates the engine directly: changes go model → compiler → engine.
- Editor metadata (`val:x`, `val:y`, colours) lives in a separate graph from execution metadata. Dragging a node must not invalidate the compiled circuit.
- Every operation is an `Op` in `src/ops/`. UI views and the MCP server are thin adapters — never a second implementation.
- `valis_core` links `juce_dsp` and serd/sord only; no `juce_gui_*`. The whole model and DSP layer is testable as plain console executables.
- The parameter list is fixed at construction (64 normalised slots). `val:Param` declarations bind slots to element properties.

## Documentation rules

- documents should be written in technical plain English
- do not use em dashes or novel jargon
- any references to concepts that aren't common knowledge should contain links to further information

## Control arc semantics

- A control arc **replaces** the destination port's value each block — it does not add to it. A fixed `val:cutoff` on a Ladder instance has no effect if any control arc also targets `cutoff`.
- When designing a circuit, check whether a control arc targets a port before setting a fixed value on it. If an arc reaches that port, the baked value is unreachable.
- The resting value for a controlled port must live in the control path (e.g., in a Scale's `val:min`), not on the element instance.
- The compiler's topological sort uses two adjacency graphs: `cycleAdj` (audio only, for cycle detection) and `orderAdj` (audio + control, for processing order). Control sources are guaranteed to run before their destinations in the same block.
- For drum voices using `val:TwinTBridge`, connect the amp envelope directly to the VCA cv. Route the `val:NoteGate` velocity to the TwinTBridge `velocity` port. Do not route velocity through the VCA cv path (e.g. via `val:ControlMultiply`) — velocity goes to 0 on note-off, closing the VCA before the oscillator's decay finishes.

## Conventions

- Small files, one class each. Public headers in `include/valis/`, implementations in `src/`.
- `tests/` mirrors `src/`. One bare `main()` + `<cassert>` per file, registered with CTest. No GoogleTest, no Catch2. Cover valid, invalid, and failure cases.
- Every external dependency behind a CMake `option()`. `build.sh` is the one-command build; `./valis` launches the standalone.
- Prefer deterministic offline audio tests (`valis-render`) over device-based ones.
- C++20. Match surrounding idiom and comment density. Every file starts with `// path/filename`.
- Comments describe purpose only where intent is non-obvious. No effect descriptions.
- Leave `TODO:` comments where further work is needed; don't leave them unactioned.
- Never call a function with observable side effects inside `assert()`. In Release builds `NDEBUG` expands `assert(expr)` to `((void)0)`, silently skipping the call. Pattern: `const bool ok = store.parse(...); assert(ok);`

## RDF

- One namespace: `http://purl.org/stuff/valis/`, prefix `val:`, trailing slash.
- `src/rdf/Vocabulary.h` is the single source of IRI truth — frozen string constants only.
- Reuse standard vocabularies (LV2, units) rather than inventing terms.
- `vocabs/valis.ttl` is loaded at runtime. A test asserts the ontology's class set and `ElementRegistry`'s factory set match in both directions — drift is a test failure.
- Model arcs explicitly with named ports. Do not encode topology as `rdf:List`.
- Compare terms with `sord_node_equals`, never pointer identity.

## Libraries

- RDF: serd (parse/serialise), sord (in-memory store) — pinned to ≥ 0.32.0; system packages older than that have an incompatible `SerdError` struct layout that causes a SEGFAULT in the error callback. `cmake/FindOrFetchSerd.cmake` enforces this and fetches a known-good version if the system package is absent or too old.
- DSP: `juce_dsp` — `StateVariableTPTFilter`, `LadderFilter`, `WaveShaper`, `Oscillator`, `DelayLine`, `Oversampling`, `FastMathApproximations`
- GUI: `juce_gui_basics`, `CodeEditorComponent` for the Code tab
- HTTP: cpp-httplib; JSON: `juce::JSON` / `juce::var`

## Change workflow

1. Read the relevant model, compiler, ops, and test contracts first.
2. Identify thread affinity and ownership before writing anything.
3. Keep the change in the smallest affected subsystem.
4. Update the public interface first, then add focused tests.
5. Run the narrowest relevant tests, then `./build.sh` if anything native changed.
6. Update milestone status in `docs/plan.md` when a milestone completes.

## Reference projects

- `/home/danny/github/transmission` — layering, CMake feature-gating, real-time rules
- `/home/danny/hyperdata/transmissions` — Turtle idiom and app conventions
- `/home/danny/github/JUCE` — 9.0.1; `extras/AudioPluginHost` is the reference graph editor and test host
