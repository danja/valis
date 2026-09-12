# Valis

A DAW plugin that builds virtual analog circuits from RDF/Turtle descriptions.
`docs/plan.md` holds the plan and milestone status.

Four concerns stay separate. Changes flow downward, never sideways.

```
ui/ · mcp/            adapters only
      ops/            every key operation is one Op
rdf/ · model/ · compiler/     message thread only
engine/ · dsp/                real time
```

## Real time, non negotiable

- No allocation, filesystem, network, unbounded logging or unpredictable locks
  in `processBlock` or any element's `process`.
- RDF parsing, model building and compilation happen on the message thread only.
- `CompiledCircuit` is preallocated on the message thread and swapped in by
  atomic pointer. The retired one is freed on the message thread.
- Report bad Turtle, unknown elements and cycles as located, recoverable errors.
  Never silence, never crash.

## Architecture

- The UI never mutates the engine. Changes go model, then compiler, then engine.
- Every operation is an `Op` in `src/ops/`. UI views and the MCP server are thin
  adapters over those Ops, never a second implementation.
- Editor metadata (`val:x`, `val:y`, colours) lives in a separate graph from
  execution metadata, so dragging a node does not invalidate the compiled
  circuit.
- `valis_core` links `juce_dsp` and serd/sord only, no `juce_gui_*`. The model
  and DSP layers build as plain console executables.
- The parameter list is fixed at construction: 64 normalised slots, bound by
  `val:Param` declarations.

## Writing code here

- C++20. Small files, one class each. Headers in `include/valis/`,
  implementations in `src/`. Every file starts with `// path/filename`.
- `tests/` mirrors `src/`. One bare `main()` plus `<cassert>` per file,
  registered with CTest. No GoogleTest, no Catch2. Cover valid, invalid and
  failure cases.
- **Never call anything with side effects inside `assert()`.** Release builds
  define `NDEBUG`, which expands `assert(expr)` to `((void)0)` and silently
  skips the call. Write `const bool ok = store.parse(...); assert(ok);`
- Comments describe purpose where intent is not obvious. Not effects.
- Leave `TODO:` where work remains, and do not leave them unactioned.
- `./build.sh` is the one command build. `./valis` launches the standalone.
  Run the narrowest relevant tests first, then `./build.sh` if native code
  changed.

## RDF

- One namespace: `http://purl.org/stuff/valis/`, prefix `val:`, trailing slash.
- `src/rdf/Vocabulary.h` is the single source of IRI truth. Frozen string
  constants only.
- Reuse LV2 and units vocabularies rather than inventing terms.
- Model arcs explicitly with named ports. Do not encode topology as `rdf:List`.
- Compare terms with `sord_node_equals`, never pointer identity.
- A test asserts that `vocabs/valis.ttl` and `ElementRegistry` agree in both
  directions. Drift is a test failure.

## Control arcs replace, they do not add

A control arc overwrites the destination port's value each block. A fixed
`val:cutoff` on a Ladder has no effect if an arc also targets `cutoff`, so the
resting value belongs in the control path, for example a Scale's `val:min`.

For drum voices using `val:TwinTBridge`, connect the amp envelope directly to
the VCA cv, and route `val:NoteGate` velocity to the TwinTBridge `velocity`
port. Velocity through the VCA cv path closes the VCA on note off before the
oscillator's decay finishes.

## Documents

Technical plain English. No em dashes, no novel jargon. Link anything that is
not common knowledge.

Record mistakes in `MISTAKES.md`: what happened, root cause, prevention.
Read `TODO.md` at the start of a session and keep it current.
