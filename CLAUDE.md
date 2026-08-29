# Valis

Virtual Analog LLM Intelligent Simulation — a DAW plugin that builds virtual-analog
circuits from RDF/Turtle descriptions. See `docs/plan.md` for the full plan and the
current milestone status.

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

- Log mistakes in MISTAKES.md (what happened, root cause, prevention).
- Periodically read TODO.md and check for outstanding tasks, carry these out. When a task has been completed, remove it from the  doc.

## Non-negotiable real-time rules

- Never allocate, access the filesystem or network, log through an unbounded sink,
  or take an unpredictable lock in `processBlock` or any element's `process`.
- Keep RDF parsing, model building, and circuit compilation off the real-time path.
- `CompiledCircuit` is fully preallocated on the message thread and handed to the
  engine by atomic swap; the retired one is freed on the message thread.
- Use preallocated buffers and bounded lock-free queues for callback communication.
- Surface failures — bad Turtle, unknown element, cycle — as located, recoverable
  errors. Never silence, never crash.

## Architecture rules

- The UI never mutates the engine directly. Changes go model → compiler → engine.
- Keep editor metadata (`val:x`, `val:y`, colours) in a separate graph from
  execution metadata. Dragging a node must not invalidate the compiled circuit.
- Every operation the plugin can perform is an `Op` in `src/ops/`. The three views
  and the MCP server are thin adapters over it — never a second implementation.
- `valis_core` links `juce::juce_dsp` and serd/sord only. No `juce_gui_*`, so the
  whole model and DSP layer stays testable as plain console executables.
- The plugin's parameter list is fixed at construction (64 normalised slots).
  Turtle `val:Param` declarations bind slots to element properties.

## Repository conventions

- Small files, one class each. Public headers in `include/valis/`, separate from `src/`.
- `tests/` mirrors `src/`. One bare-`main()` + `<cassert>` executable per file,
  registered with CTest. No GoogleTest, no Catch2.
- Cover valid, invalid, and failure cases — not just the happy path.
- Every external dependency sits behind a CMake `option()`; the default build is
  as close to dependency-free as the plugin allows.
- `build.sh` is the one-command build, echoing a heading before each stage.
  `./valis` launches the standalone build.
- Prefer deterministic offline audio tests (`valis-render`) over device-based ones.



## Code

- C++20. Match the surrounding code's idiom and comment density.
- Every file starts with a one-line `// path/filename` comment.
- Comments describe purpose, not effect, and only where intent or an unusual API
  is not obvious.
- Don't apologise for errors: fix them.
- Where further work in an area is needed, leave a `TODO:` comment.

## RDF

- One namespace: `http://purl.org/stuff/valis/`, prefix `val:`, **trailing slash**.
- `src/rdf/Vocabulary.h` is the single source of IRI truth — frozen string
  constants, no namespace-builder machinery.
- Reuse standard vocabularies rather than inventing terms. Ports, ranges and units
  come from `lv2:` and `units:`; local copies are at `/usr/lib/lv2/core.lv2/` and
  `/usr/lib/lv2/units.lv2/`.
- `ontology/valis.ttl` is **loaded at runtime**, not just documentation. It declares
  each element class with a `val:implementation` registry key, and a test asserts
  the ontology's class set and `ElementRegistry`'s factory set match in both
  directions. Drift is a test failure, not a surprise.
- Model arcs explicitly with named ports. Do not encode topology as an `rdf:List`.
- Compare terms with `sord_node_equals`, never pointer identity.

## Libraries

Prefer these when their functionality is needed:

- RDF: serd (parse/serialise), sord (in-memory store)
- DSP: `juce_dsp` — `StateVariableTPTFilter`, `LadderFilter`, `WaveShaper`,
  `Oscillator`, `DelayLine`, `Oversampling`, `FastMathApproximations`
- GUI: `juce_gui_basics`, `CodeEditorComponent` for the Turtle view
- HTTP: cpp-httplib; JSON: `juce::JSON`/`juce::var`
- Tests: CTest

## Change workflow

1. Read the relevant model, compiler, ops and test contracts first.
2. Identify thread affinity and ownership before writing anything.
3. Keep the change in the smallest affected subsystem.
4. Add or update the public interface first, then focused tests.
5. Run the narrowest relevant tests, then `./build.sh` if anything native changed.
6. Update the milestone status in `docs/plan.md` when a milestone completes.

## Reference projects

- `/home/danny/github/transmission` — layering, CMake feature-gating, real-time rules
- `/home/danny/hyperdata/transmissions` — Turtle idiom and app conventions
- `/home/danny/github/JUCE` — 9.0.1; `extras/AudioPluginHost` is both a reference
  graph editor and the test host
