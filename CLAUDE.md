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
- Anything that happens at a point in time is located by stream position, never by the index
  within the current block and never by equality with a block boundary. Both MISTAKES.md
  entries on this are the same bug from opposite directions: a per-sample event gated on the
  block index, and a stream-position event compared for equality against one. An event fires
  in the block that contains it.

## Architecture rules

- UI never mutates the engine directly: changes go model → compiler → engine.
- Editor metadata (`val:x`, `val:y`, colours) lives in a separate graph from execution metadata. Dragging a node must not invalidate the compiled circuit.
- Every operation is an `Op` in `src/ops/`. UI views and the MCP server are thin adapters — never a second implementation.
- `valis_core` links `juce_dsp`, `juce_audio_formats`, `juce_cryptography` and serd/sord only;
  no `juce_gui_*`. The whole model and DSP layer is testable as plain console executables.
  `juce_audio_formats` is linked PRIVATE and exists only so `val:Granulator` can read a sound
  file on the message thread; `juce_cryptography` likewise, only to check a declared
  `val:sha256` against that file.
- The parameter list is fixed at construction (64 normalised slots). `val:Param` declarations bind slots to element properties.

## Documentation rules

- documents should be written in technical plain English
- do not use em dashes or novel jargon
- any references to concepts that aren't common knowledge should contain links to further information

## Controls view

- A control port is drawn by its shape, not by default. A port that declares
  `lv2:portProperty lv2:toggled`, or an enumeration with exactly two scale points, is a
  two-position switch (`src/ui/ToggleSwitch.*`). An enumeration with more is a selector strip
  with every option named on the panel (`src/ui/SelectorStrip.*`). Everything else is a dial.
- So declare a binary port `lv2:toggled` with a scale point per position, and give every
  enumeration scale points. `PortDesc::isBinary()` and `isChoice()` answer which is which; no
  view should test `enumeration` or `toggled` itself.
- An element that needs a UI of its own gets a box in the Controls view beside the monitors,
  as `val:Oscilloscope`, `val:FreqAnalyzer` and `val:SampleLoad` do.
- A binary choice a circuit needs as two arbitrary values is a `val:Select`, not a dial with
  two meaningful positions.
- Every position of a control must be able to differ in the circuit that exposes it. A mode
  that collapses onto another for a given wiring is worse than no mode: see MISTAKES.md on the
  granulator's Auto position.
- The panel scrolls and the pointer is usually over a dial, so a dial must not consume the
  scroll wheel. `PanelSlider` scrolls by default and adjusts only with Ctrl or Command held.

## Transport

- The host timeline reaches elements as `ProcessArgs::transport`. `ValisProcessor` reads it from
  the play head, `valis-render` synthesises one from `--tempo` and `--rolling`, and the engine
  carries `ppqPosition` forward one control slice at a time so a musical phase stays continuous
  inside a block.
- An element must derive musical timing from `ppqPosition`, never by counting host blocks.

## Physical models

- Waveguide instruments share `src/dsp/elements/Waveguide.h`: a fractional delay line, a
  one-pole loss filter that can report its own phase delay, a DC blocker and deterministic
  turbulence. An instrument element writes only its exciter.
- A waveguide is tuned by its *total* loop delay, so subtract what the filters in the loop
  contribute. `Loss::phaseDelay` exists for that; without it, damping detunes the instrument.
- Where the loop is nonlinear, the sounding pitch is not predictable from the delay lengths
  alone. Calibrate against measurements, fit a curve, and put a test on it: `val:Flute` does
  this for the jet ratio and again across the playing range.
- Test the spectrum, not just pitch and stability. A model can be in tune, stable, and the
  wrong instrument: see MISTAKES.md on the flute that was a stopped pipe.
- Measure harmonics against the pitch the instrument actually played, not the one it was
  asked for. A few cents of drift moves a partial out of the measurement window and reports a
  tone far purer than it is.

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
- A sound file a session chooses (`val:SampleLoad`) is an override held by the processor and
  applied to the compiled circuit before it is installed, never an edit to the document. The
  document declares the initial file; the session's choice sits on top of it, exactly as a
  turned knob sits on top of a declared value.
- `DspElement::setOption` returns false when it recognises a key and cannot apply it, writing
  the reason into `error`; the engine turns that into a located load failure. An unknown key
  is not a failure.
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
- Sound files: `juce_audio_formats` - WAV, AIFF, FLAC, Ogg and MP3. Message thread only.
- HTTP: cpp-httplib; JSON: `juce::JSON` / `juce::var`

## Change workflow

1. Read the relevant model, compiler, ops, and test contracts first.
2. Identify thread affinity and ownership before writing anything.
3. Keep the change in the smallest affected subsystem.
4. Update the public interface first, then add focused tests.
5. Run the narrowest relevant tests, then `./build.sh` if anything native changed.
6. **Build everything (`cmake --build build`, no target) before measuring through
   `valis-render` or any other tool.** A targeted test build leaves the tool linked
   against the previous `libvalis_core.a`, so it measures the old code. This has cost
   hours twice; see MISTAKES.md. If a test and a tool disagree about a circuit whose
   compiled form prints identically in both, the binaries differ, not the code.
6. Update milestone status in `docs/plan.md` when a milestone completes.

## Reference projects

- `/home/danny/github/transmission` — layering, CMake feature-gating, real-time rules
- `/home/danny/hyperdata/transmissions` — Turtle idiom and app conventions
- `/home/danny/github/JUCE` — 9.0.1; `extras/AudioPluginHost` is the reference graph editor and test host
