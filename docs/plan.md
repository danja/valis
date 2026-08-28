# Valis — MVP Implementation Plan

## The brief

The original statement of intent for this project, preserved verbatim:

> Valis will be a DAW plugin that allows creation of virtual analog circuits that can be used for sound generation and effects. The graph model used will be RDF, serialized in Turtle format, following the pattern of /home/danny/hyperdata/transmissions and /home/danny/github/transmission wherever possible reusing standard vocabularies such as that used by LV2.
>
> Declarative parts of the plugin will be expressed in RDF, using librdf, libraptor etc as necessary. DSP will be achieved in C++ with use of the JUCE libraries, /home/danny/github/JUCE
>
> Classes will include things like Filter and NonLinear (or would Transfer be a better name?). This will be subclassed, with implementations taking sensible defaults.
>
> The plugin will feature three views, one which is a syntax-highlighted text editor for Turtle definitions, another a node & arc graphical view, a third will allow control of settings via knobs etc.
>
> The plugin will have every key operation supported by HTTP MCP.
>
> The project will be built in a highly modular fashion to simplify maintenance and extension. Documentation will be provided via GitHub Pages.

## Context

The brief above names the destination but not the route. This document is the route: it converts the brief into an ordered, independently verifiable sequence of milestones, resolves the questions the brief left open, and fixes the architectural seams that are expensive to move later — the RDF vocabulary, the real-time boundary, and the headless operation surface that both the UI and MCP sit on.

**Scope: the MVP is the full brief.** All three views, the DSP engine, the RDF model, and an in-process HTTP MCP server, built for Standalone + VST3 + LV2 + CLAP. Nothing is deferred past MVP. The milestones below sequence that work so there is something demonstrable early and continuously, not so that scope is dropped.

Two deviations from the brief, both deliberate and agreed:

- **serd + sord instead of librdf/raptor** — see the decision table below.
- **`val:Transfer` rather than `val:NonLinear`** — answering the question the brief itself poses.

### Decisions locked before writing this plan

| Question | Decision | Consequence |
|---|---|---|
| Circuit granularity | **Block-level** — nodes are DSP blocks (Filter, Transfer, Oscillator, Mixer), patched modular-synth style | Reuses `juce_dsp` heavily; no MNA solver; feedback requires an explicit unit-delay node |
| `NonLinear` vs `Transfer` | **`val:Transfer`** | Class split is *memory vs. memoryless*; linearity stays a property (`val:linear false`), because `val:Ladder` is a nonlinear *Filter* |
| RDF library | **serd + sord** (not librdf/raptor, deviating from the brief) | Zero transitive deps, ISC/MIT, the LV2 ecosystem's own stack; small enough to vendor, so VST3/CLAP stay portable. No SPARQL — hand-written traversals over `sord_search` |
| MCP location | **In-process, in MVP** | Background thread inside the plugin; sits on the same Ops layer as the UI |
| Formats | **Standalone, VST3, LV2, CLAP** | CLAP needs `clap-juce-extensions`; LV2 and VST3 run helper binaries post-build |
| Graph-view write-back | **Staged: render+drag → arcs → nodes**, full re-serialise | Turtle view is authoritative for prose/comments; graph view for topology. Comments are lost on structural graph edit — warn on first use |

---

## Environment as found

Verified on this machine:

- **JUCE 9.0.1** at `/home/danny/github/JUCE`, clean git checkout on `master`, usable via `add_subdirectory`. `FORMATS` supports `Standalone VST3 LV2` directly (`docs/CMake API.md:505`); CLAP is external.
- **CMake 3.28.3**, **g++ 13.3.0**, ninja — all above JUCE's 3.22 floor.
- **Present:** `libasound2-dev`, `libjack-jackd2-dev`, `libfreetype-dev`, `lv2-dev`, and all eight X11 `-dev` packages. Enough to build with `JUCE_WEB_BROWSER=0` / `JUCE_USE_CURL=0`.
- **Missing:** `libserd-dev`, `libsord-dev`, `libfontconfig1-dev`. Also `libwebkit2gtk-4.1-dev` and `libcurl4-openssl-dev`, which the two `=0` definitions make unnecessary.
- **LV2 vocabulary is already on disk** at `/usr/lib/lv2/core.lv2/lv2core.ttl` and `/usr/lib/lv2/units.lv2/units.ttl` — the reuse the brief asks for needs no download.
- GitHub is reachable, so `FetchContent` works.
- **Licence note to settle early:** JUCE 9 is AGPLv3-or-commercial; `LICENSE` here is MIT. A distributed binary linking JUCE under the AGPL obliges AGPL for the combined work.

---

## Architecture

Four layers, with two hard boundaries. The layering is lifted from `/home/danny/github/transmission` (`docs/plan.md`, `AGENTS.md`), which solves the same problem — RDF-described audio graph, real-time engine — for a host rather than a plugin.

```
  ┌─ ui/ ────────────────┐   ┌─ mcp/ ──────┐
  │ Turtle │ Graph │ Knobs│   │ HTTP/JSON-RPC│
  └───────────┬──────────┘   └──────┬───────┘
              └────────┬────────────┘
                  ┌────▼─────┐   ops/ — the headless command surface.
                  │   Ops    │   Every key operation is one Op. UI and MCP
                  └────┬─────┘   are both thin adapters over it.
   ═══════════════════ │ ═══════════════ message thread only ═══════════
                  ┌────▼─────┐
                  │  Model   │   rdf/ · model/ · compiler/
                  └────┬─────┘   Turtle → CircuitModel → CompiledCircuit
   ═══════════════════ │ ═══════════════ RT boundary ═════════════════
                  ┌────▼─────┐
                  │  Engine  │   engine/ · dsp/
                  └──────────┘   Preallocated. Never parses RDF.
```

**Boundary 1 — the Ops layer.** Every operation the plugin can perform is an `Op` with a result. The three views call Ops; the MCP server calls the same Ops. This is what makes "every key operation supported by HTTP MCP" a property of the design rather than a parallel implementation to maintain.

**Boundary 2 — the real-time line.** Straight from `transmission/AGENTS.md`, and non-negotiable here too: the audio callback never allocates, never touches the filesystem or network, never parses RDF, never takes an unpredictable lock. `CompiledCircuit` is fully preallocated on the message thread and handed over by atomic pointer swap; the retired one goes into a bounded free-queue drained by a `juce::Timer`.

### Why `valis_core` does not depend on JUCE's GUI

`valis_core` links only `juce::juce_dsp` (which transitively pulls `juce_core` and `juce_audio_formats`) plus serd/sord. No `juce_gui_*`. That keeps every test a plain console executable, makes the whole model+DSP layer testable offline and deterministically, and means a headless `valis-render` CLI falls out for free. GUI code lives only in the plugin target.

---

## Repository layout

Following the `transmission` conventions: public headers under `include/valis/` separate from `src/`, `tests/` mirroring `src/`, one out-of-tree build dir per feature combination, driven by a `build.sh` that echoes a heading before each.

```
CMakeLists.txt              root; options + FetchContent + targets
build.sh                    one-command full build, headings per stage
valis                       launcher script, guards on the binary existing
AGENTS.md                   conventions + the real-time rules
cmake/
  FindOrFetchSerd.cmake     pkg-config first, FetchContent fallback
  FindOrFetchClap.cmake     clap-juce-extensions, behind VALIS_WITH_CLAP
ontology/
  valis.ttl                 THE vocabulary — loaded at runtime, not just docs
examples/
  *.ttl                     example circuits, doubling as test fixtures
include/valis/              public headers for valis_core
src/
  rdf/        Vocabulary.h  TurtleStore.{h,cpp}  TurtleWriter.{h,cpp}
  model/      CircuitModel.{h,cpp}  Element.h  Port.h  Arc.h  ParamBinding.h
  compiler/   CircuitCompiler.{h,cpp}  CompiledCircuit.h
  dsp/        DspElement.h  ElementRegistry.{h,cpp}  elements/*.{h,cpp}
  engine/     ValisEngine.{h,cpp}  BufferPool.h  SwapQueue.h
  ops/        Op.h  OpDispatcher.{h,cpp}
  mcp/        McpServer.{h,cpp}  JsonRpc.{h,cpp}
  ui/         TurtleView.{h,cpp}  TurtleCodeTokeniser.{h,cpp}
              GraphView.{h,cpp}  ControlsView.{h,cpp}  ValisEditor.{h,cpp}
  plugin/     ValisProcessor.{h,cpp}
  tools/      RenderMain.cpp        valis-render CLI
tests/        mirrors src/, one bare-main()+assert executable per file
docs/         plan.md (this, checked in) + manual/, published to Pages
```

---

## The vocabulary

Namespace `http://purl.org/stuff/valis/`, prefix `val:`, **trailing slash** — matching the `http://purl.org/stuff/transmissions/` convention, and deliberately avoiding the slash-vs-hash split that the transmissions codebase drifted into.

Two lessons from the reference projects shape this:

1. **The ontology is loaded at runtime, not just documented.** In `transmissions`, `docs/.../vocabs/transmissions.ttl` declares `trn:implementation "src/processors/fs/FileReader.js"` for every class — but nothing loads it, and its paths have since gone stale while a hand-written 25-branch factory chain does the real work. Valis loads `ontology/valis.ttl` at startup and drives element construction from it. C++ has no dynamic import, so `val:implementation` holds a **registry key**, not a path — and a unit test asserts that the set of classes declared in the ontology and the set of factories registered in `ElementRegistry` are equal in both directions. Drift becomes a test failure.

2. **Explicit arcs with named ports, not an `rdf:List` pipe.** `transmissions` encodes order as `:pipe (:p10 :p20)`, which cannot express a DAG — its `Fork`/`Choice` branching lives in procedural code and message flags, so the graph is not a faithful picture of the dataflow, and its visual editor has to topologically re-derive the list on export. Valis models edges directly.

```turtle
@prefix val:   <http://purl.org/stuff/valis/> .
@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .
@prefix units: <http://lv2plug.in/ns/extensions/units#> .
@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .

# ---- ontology/valis.ttl (excerpt) ----
val:Element a rdfs:Class ; rdfs:comment "A processing block in a circuit." .

val:Filter a rdfs:Class ; rdfs:subClassOf val:Element ;
    rdfs:comment "An element with memory: y = f(x, state)." .
val:Transfer a rdfs:Class ; rdfs:subClassOf val:Element ;
    rdfs:comment "A memoryless transfer function: y = f(x)." .
val:NonLinear owl:equivalentClass val:Transfer .   # brief-compatibility alias

val:Ladder a rdfs:Class ; rdfs:subClassOf val:Filter ;
    val:implementation "Ladder" ;                   # ElementRegistry key
    val:linear false ;
    lv2:port [ a lv2:InputPort,  lv2:AudioPort   ; lv2:symbol "in"  ] ,
             [ a lv2:OutputPort, lv2:AudioPort   ; lv2:symbol "out" ] ,
             [ a lv2:InputPort,  lv2:ControlPort ; lv2:symbol "cutoff" ;
               lv2:default 1000.0 ; lv2:minimum 20.0 ; lv2:maximum 20000.0 ;
               units:unit units:hz ] ,
             [ a lv2:InputPort,  lv2:ControlPort ; lv2:symbol "resonance" ;
               lv2:default 0.0 ; lv2:minimum 0.0 ; lv2:maximum 1.0 ] .

# ---- examples/basic.ttl (a circuit instance) ----
@prefix : <urn:valis:circuit#> .

:main a val:Circuit ; val:element :osc , :vcf , :drive , :out ;
      val:arc :a1 , :a2 , :a3 .

:osc   a val:Oscillator ; val:wave "saw" .
:vcf   a val:Ladder ; val:cutoff 800.0 ; val:resonance 0.7 .
:drive a val:Tanh   ; val:gain 4.0 .
:out   a val:Output .

:a1 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ;
                val:to   [ val:node :vcf ; val:port "in"  ] .

# Parameter slot binding — see "Parameters" below
:p0 a val:Param ; val:slot 0 ; val:target :vcf ; val:property val:cutoff ;
    lv2:name "Cutoff" ; lv2:symbol "cutoff" ; units:unit units:hz .
```

The LV2 vocabulary carries the port and control-range description exactly as the brief asks — `lv2:port`, `lv2:ControlPort`, `lv2:default/minimum/maximum`, `lv2:symbol`, `units:unit` — and these are the same terms `/usr/lib/lv2/a-comp.lv2/a-comp.ttl` uses, so the local plugin corpus is a working style reference.

Editor metadata (`val:x`, `val:y`, colours) goes in a **separate graph** from execution metadata — a rule taken verbatim from `transmission/AGENTS.md` ("keep editor metadata independent from execution metadata"). Dragging a node must never invalidate the compiled circuit.

---

## Parameters — the constraint that shapes the design

VST3, LV2 and CLAP all require a **static parameter list fixed at construction**. Parameters cannot appear when a new circuit is loaded. So:

`ValisProcessor` constructs an `AudioProcessorValueTreeState` with a fixed pool of 64 generic slots (`p00`…`p63`), each normalised 0–1. A `val:Param` in the Turtle binds `slot N → element property`, carrying `lv2:name`, `lv2:symbol`, range and `units:unit` for display and for the normalised↔real mapping. The knobs view renders only bound slots. Unbound slots exist but are inert and hidden.

This is the single most important thing to get right in M6; retrofitting it is painful.

---

## Milestones

Each is independently verifiable and leaves the build green. `docs/plan.md` gets an inline status annotation per milestone as it completes — the plan doubles as the progress tracker, the convention used in `transmission/docs/plan.md`.

### M0 — Build skeleton *(complete)*
Root `CMakeLists.txt` with `option(VALIS_BUILD_TESTS ON)`, `option(VALIS_WITH_MCP ON)`, `option(VALIS_WITH_CLAP OFF)`; the dependency-free-default + feature-gated-options pattern from `/home/danny/github/transmission/native/CMakeLists.txt`. `juce_add_plugin` with `FORMATS Standalone VST3 LV2`, an explicit `PLUGIN_CODE` (JUCE randomises it per configure otherwise, and hosts lose the plugin), `LV2URI "urn:valis:valis"` (must match `https?://.*|urn:.*` or configure fatal-errors), and **`EDITOR_WANTS_KEYBOARD_FOCUS TRUE`** — without it hosts swallow every keystroke aimed at the Turtle editor. `project()` must list `C` among its languages or JUCE refuses to configure. `build.sh` + `valis` launcher.

*serd/sord gotcha:* both build with **meson**, not CMake, so `FetchContent` + `add_subdirectory` will not work. `cmake/FindOrFetchSerd.cmake` tries `pkg_check_modules(serd-0 sord-0)` first — `sudo apt install libserd-dev libsord-dev` is the fast path — and otherwise fetches `serd v0.32.10` / `sord v0.16.8` and compiles them with a hand-written `add_library(... STATIC)` over their `src/*.c`. Both vendor their own zix, so no third fetch.

*Acceptance:* `./build.sh` green; an empty plugin loads in `extras/AudioPluginHost` (build JUCE with `-DJUCE_BUILD_EXTRAS=ON`) and is listed by `lv2ls`.

*Done:* all three formats build warning-free; `lv2ls`/`lv2info` resolve `urn:valis:valis`; the 64 parameter slots appear in the generated `dsp.ttl` as `lv2:Parameter` + `patch:writable` (JUCE 9 uses the LV2 patch/parameters extension, not ControlPorts). serd/sord are fetched and compiled by `cmake/FindOrFetchSerd.cmake`. Note: JUCE bundles its own serd/sord/lilv for LV2 *hosting* — `JUCE_PLUGINHOST_LV2=0` keeps two copies out of one binary.

### M1 — RDF layer
`Vocabulary.h` as frozen IRI string constants (the `transmission/src/rdf/Vocabulary.js` approach — plain constants, no namespace-builder machinery). `TurtleStore` wraps `SordWorld`/`SordModel` with RAII, parses via `serd_reader`, and — critically — **captures serd's line/column on error** so the editor can put a marker in the gutter. `TurtleWriter` serialises back via `serd_writer` with a stable prefix table.

*Acceptance:* round-trip tests on `examples/*.ttl`; malformed Turtle yields a diagnostic with correct line and column.

### M2 — Ontology, model, compiler
`ontology/valis.ttl`. `CircuitModel` is the immutable in-memory picture (elements, ports, arcs, param bindings). `CircuitCompiler` validates and lowers it to `CompiledCircuit` — a flat, fully preallocated, topologically ordered POD structure that the engine consumes. Validation must reject, with a useful message pointing at a node: unknown element class, unknown port symbol, dangling arc endpoint, duplicate arc, type-mismatched arc (audio↔control), and cycles without an explicit `val:UnitDelay`.

*Acceptance:* valid / invalid / cycle cases each covered by a test, per the `transmission/AGENTS.md` rule that tests cover "valid, invalid, and failure cases". Plus the ontology↔registry set-equality test described above.

### M3 — DSP elements and registry
`DspElement` abstract base, deliberately modelled on `transmission/native/include/transmission/AudioProcessor.h` — virtual methods with **default implementations that degrade gracefully** rather than abort. `ElementRegistry` maps class IRI → factory.

Reuse from `juce_dsp` rather than reimplementing: `StateVariableTPTFilter` and `FirstOrderTPTFilter` (the topology-preserving transforms are the VA-correct ones), `LadderFilter` (already a nonlinear Moog ladder), `WaveShaper`, `Oscillator`, `DelayLine`, `Oversampling`, `BallisticsFilter`, and `FastMathApproximations` for cheap `tanh`. MVP element set: `Oscillator`, `Noise`, `Filter` (OnePole / StateVariable / Ladder), `Transfer` (Tanh / HardClip / Fold / Diode), `Gain`, `Mixer`, `Envelope`, `UnitDelay`, `Input`, `Output`.

*Acceptance:* per-element offline determinism tests — fixed input buffer in, golden output out.

### M4 — Engine and the real-time boundary
`ValisEngine` owns the active `CompiledCircuit`. Swap via atomic exchange, retirement via a bounded SPSC free-queue drained on a `juce::Timer`. `BufferPool` preallocates every intermediate buffer at compile time; `processBlock` allocates nothing. Nonlinear elements run inside `dsp::Oversampling`.

`valis-render` CLI (`src/tools/RenderMain.cpp`) renders a `.ttl` to a `.wav` headlessly — the fastest possible verification loop, and it needs no GUI, no host, and no audio device.

*Acceptance:* `valis-render examples/basic.ttl out.wav` produces the expected signal; a hot-swap-under-load test asserts no allocation on the audio thread (run the render loop under a debug allocator hook).

### M5 — Ops layer and plugin state
`Op` / `OpDispatcher`: `getTurtle`, `setTurtle`, `validate`, `listElementTypes`, `getGraph`, `addNode`, `removeNode`, `connect`, `disconnect`, `listParams`, `getParam`, `setParam`, `render`, `getDiagnostics`. Every op returns a result-or-error; none of them assume a UI. Plugin state (`get/setStateInformation`) stores the Turtle source verbatim, so a saved session is self-contained.

*Acceptance:* every op exercised headlessly by tests, with no editor constructed.

### M6 — Parameters and host automation
The 64-slot APVTS pool and `val:Param` binding described above. Slot↔property mapping, normalised↔real conversion driven by the `lv2:minimum`/`lv2:maximum`/`units:unit` metadata.

*Acceptance:* automate a bound slot from AudioPluginHost and hear the cutoff move; rebinding via new Turtle re-targets the same slot without a host rescan.

### M7 — Turtle editor view
`TurtleCodeTokeniser : public juce::CodeTokeniser` — only two virtuals to implement (`readNextToken`, `getDefaultColourScheme`). JUCE ships only C++, Lua and XML tokenisers, so this is new code; model it on `juce_XMLCodeTokeniser.cpp` (the smallest complete example) and crib the scanning helpers from `juce_CPlusPlusCodeTokeniserFunctions.h`. Token types: prefix directive, IRI, prefixed name, blank node, string, number, `a`/keyword, punctuation, comment. Wire into `CodeEditorComponent` per `examples/GUI/CodeEditorDemo.h`. Debounced recompile on edit, errors surfaced in a gutter using M1's line/column.

*Acceptance:* typing a broken triple shows a marker on the right line and leaves the previously compiled circuit running.

### M8 — Knobs view
Rotary `Slider`s generated from the bound param slots, with `SliderAttachment`. `examples/Plugins/DSPModulePluginDemo.h` is the in-repo reference for a large parameter set wired to a `dsp::ProcessorChain`. Labels and unit suffixes come from `lv2:name` / `units:unit` / `units:render`.

### M9 — Graph view, staged
Adapt `/home/danny/github/JUCE/extras/AudioPluginHost/Source/UI/GraphEditorPanel.{h,cpp}` — the connector-drag, pin hit-testing and bezier-routing logic is the reusable part; its `AudioProcessorGraph` runtime is **not** (it is a block-rate host graph, it rejects cycles, and Valis has its own engine). `PluginGraph.{h,cpp}` is the reference for persisting node positions.

- Stage 1: render read-only + drag positions → `val:x`/`val:y` in the editor graph.
- Stage 2: drag-to-connect and delete arcs.
- Stage 3: add and delete nodes from a palette built from the ontology.

Structural edits mutate the sord model and re-serialise the whole document. Surface the "graph editing reformats your Turtle and drops comments" warning once, on first structural edit.

### M10 — MCP server
`cpp-httplib` (single header, MIT) via FetchContent, bound to `127.0.0.1` only, port configurable, optional bearer token, off unless enabled. JSON-RPC 2.0 over Streamable HTTP. JSON via `juce::JSON`/`juce::var` — already available through `juce_core`, so no extra dependency. Each MCP tool is a direct adapter to one `Op` from M5; the server thread never touches the audio thread and never blocks it.

*Acceptance:* `curl` an `initialize`, then `tools/list`, then `tools/call` for `setTurtle` and hear the circuit change; a bad Turtle payload returns a JSON-RPC error with line and column rather than crashing.

### M11 — Docs, packaging, CLAP
`clap-juce-extensions` via `cmake/FindOrFetchClap.cmake` behind `VALIS_WITH_CLAP`, built by `build.sh` in its own build dir. `docs/manual/` in Markdown, published to GitHub Pages by a workflow modelled on `hyperdata/transmissions/.github/workflows/docs.yml` (marked + link rewrite + deploy). `AGENTS.md` finalised.

---

## Critical files

New files, so the list is a construction order rather than a change set. The ones where a wrong decision is expensive later:

| File | Why it matters |
|---|---|
| `ontology/valis.ttl` | The contract between Turtle, the registry and the UI palette. Runtime-loaded, so it cannot drift silently |
| `src/rdf/Vocabulary.h` | Single source of IRI truth. One namespace, trailing slash, decided once |
| `src/compiler/CompiledCircuit.h` | The RT boundary object. Must be POD, flat, preallocated, and contain nothing that allocates |
| `src/engine/ValisEngine.cpp` | Where the swap protocol lives. The one place a real-time bug will hide |
| `src/ops/Op.h` | Fixes the operation surface that both the UI and MCP depend on. Widening it later is fine; reshaping it is not |
| `src/plugin/ValisProcessor.cpp` | The 64-slot parameter pool is fixed at construction and cannot be changed afterwards |
| `CMakeLists.txt` | Feature-gating and the serd/sord acquisition strategy |

---

## Conventions to adopt (from the reference projects)

Carried into `AGENTS.md`:

- **The real-time rules**, taken almost verbatim from `transmission/AGENTS.md` — never allocate, do I/O, log through an unbounded sink, or take an unpredictable lock on the audio callback; preallocated buffers and bounded lock-free queues only; RDF parsing and compilation stay off the real-time path.
- **UI never mutates the engine directly** — changes go model → compiler → engine.
- **Editor metadata separate from execution metadata.**
- Small files, one class each, `include/valis/` public headers split from `src/`.
- Every file starts with a one-line path comment (`hyperdata/transmissions/CLAUDE.md`); comments explain purpose, not effect, and only where intent isn't obvious.
- `tests/` mirrors `src/`; one bare-`main()` + `<cassert>` executable per file registered with CTest — no GoogleTest, matching `transmission/native/tests/`.
- `build.sh` echoes a human-readable heading before each build stage; one out-of-tree build dir per feature combination.
- An `examples/` circuit doubles as a test fixture — cheap end-to-end coverage for a declarative system, the same trick as the `apps.json` + `TEST_PASSED` harness in `hyperdata/transmissions`.

---

## Verification

**Per-milestone, automated:**
```bash
./build.sh                                    # JS-free: configure, build, ctest, all stages
ctest --test-dir build --output-on-failure    # unit tests
./build/valis-render examples/basic.ttl /tmp/out.wav
```

**End-to-end, manual, once the plugin is real:**
```bash
# Standalone — fastest loop, no host, no scanning
./valis

# VST3 + LV2 in a real host
cmake -S /home/danny/github/JUCE -B /tmp/juce-extras -DJUCE_BUILD_EXTRAS=ON
cmake --build /tmp/juce-extras --target AudioPluginHost
lv2ls | grep valis && jalv urn:valis:valis      # LV2 path

# MCP surface
curl -s localhost:7676/mcp -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
curl -s localhost:7676/mcp -d '{"jsonrpc":"2.0","id":2,"method":"tools/call",
  "params":{"name":"setTurtle","arguments":{"turtle":"..."}}}'
```

**Acceptance for the MVP as a whole:** load `examples/basic.ttl` in the standalone app, hear it; edit the cutoff in the Turtle view and hear it change; see the same circuit rendered in the graph view and drag a node without interrupting audio; turn the bound Cutoff knob and see the Turtle value follow; automate that parameter from AudioPluginHost via VST3; drive the same edit over HTTP MCP; and have every failure — bad Turtle, unknown element, cycle — surface a located, recoverable error instead of silence or a crash.
