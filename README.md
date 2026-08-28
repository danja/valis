# Valis

**Virtual Analog LLM Integrated System**

Valis is a DAW plugin — Standalone, VST3, LV2 and CLAP — that builds virtual-analog
circuits from RDF/Turtle descriptions. The same circuit is presented through three
views (a syntax-highlighted Turtle editor, a node-and-arc graph, and a knobs panel)
and through an HTTP MCP surface, so an LLM can drive every operation the UI can.

[`docs/plan.md`](docs/plan.md) is the implementation plan and the progress tracker:
it fixes the vocabulary, the layering and the real-time boundary, and each milestone
carries an inline status annotation as it completes.

## Architecture

Four layers with two hard boundaries. Every operation the plugin can perform is one
`Op`; the three views and the MCP server are thin adapters over that single surface,
never a second implementation. Below the model sits the real-time line: the audio
callback never allocates, never does I/O and never parses RDF. `CompiledCircuit` is
built and fully preallocated on the message thread, then handed to the engine by
atomic pointer swap.

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

`valis_core` links `juce_dsp` and serd/sord only — no GUI module — so the model,
compiler and DSP layers stay testable as plain console executables.

## Circuits are Turtle

A circuit is a set of elements and explicitly named arcs between their ports. Element
classes, port symbols and control ranges reuse the LV2 vocabulary rather than inventing
terms.

```turtle
@prefix val:   <http://purl.org/stuff/valis/> .
@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .
@prefix units: <http://lv2plug.in/ns/extensions/units#> .
@prefix :      <urn:valis:circuit#> .

:main a val:Circuit ; val:element :osc , :vcf , :drive , :out ;
      val:arc :a1 , :a2 , :a3 .

:osc   a val:Oscillator ; val:frequency 440.0 ; val:shape 1.0 .   # 1 = saw
:vcf   a val:Ladder ; val:cutoff 800.0 ; val:resonance 0.7 .
:drive a val:DiodePair ; val:seriesResistance 2200.0 .
:out   a val:Output .

:a1 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ;
                val:to   [ val:node :vcf ; val:port "in"  ] .

# Bind host parameter slot 0 to the filter's cutoff.
:p0 a val:Param ; val:slot 0 ; val:target :vcf ; val:property val:cutoff ;
    lv2:name "Cutoff" ; lv2:symbol "cutoff" ; units:unit units:hz .
```

[`vocabs/valis.ttl`](vocabs/valis.ttl) declares the element classes and is loaded at
runtime, not merely documented: a test asserts that the ontology's class set and the DSP
registry's factory set match in both directions. The vocabularies it is written against
are vendored beside it — [`vocabs/lv2/`](vocabs/lv2) holds the LV2 1.18.10 ontologies
(core, units, atom, patch, parameters, port-groups) and [`vocabs/w3c/`](vocabs/w3c) holds
rdf, rdfs and owl.

Element classes go finer-grained than "a saturation curve" where it earns its keep.
`val:Diode` is a Shockley-equation junction — `i = Is·(exp(v/(n·Vt)) − 1)`, defaulting to
a 1N4148, asymmetric because a diode conducts one way. `val:DiodePair` is the antiparallel
soft clipper with a series resistance, and `val:Triode` a Koren-style valve stage. They
carry real device parameters, so a circuit built from them behaves like the circuit it
names. `val:Network` is declared as the seam for future component-level subcircuits.

## Status

**M0 (build skeleton), M1 (RDF layer), M2 (ontology, model, compiler) and M3 (DSP
elements) are complete and verified. M4 through M11 are not started.** The plugin still
passes audio through unchanged: a circuit can be parsed, validated, ordered and its
elements constructed, but the engine that runs them is M4.

What M0 delivers:

- Standalone, VST3 and LV2 all build warning-free from one configure. CLAP is behind
  the `VALIS_WITH_CLAP` option and is untested — its CMake module lands in M11.
- The LV2 build is discoverable: `lv2ls` and `lv2info` resolve `urn:valis:valis`.
- 64 host-visible parameter slots appear in the generated `dsp.ttl` as `lv2:Parameter`
  with `patch:writable`. They are inert until M6 binds them to element properties.
- The editor opens with the three tabs — Turtle, Graph, Knobs — as placeholders.
- serd and sord are resolved by `cmake/FindOrFetchSerd.cmake` and linked into
  `valis_core`.

What M1 adds:

- `TurtleStore` — an RAII wrapper over sord's quad store with serd parsing and
  serialisation. Queries are `object`/`objects`/`subjects`/`subjectsOfType`/`contains`/
  `forEachProperty`, with `add`/`remove` for graph edits. Terms compare by value.
- Parse errors carry serd's line and column, so the Turtle view can mark the gutter in
  M7: a missing `.` reports as `4:0: missing ';' or '.'`.
- The test parses every file under `vocabs/`, so a bad ontology edit fails the build.

What M2 adds:

- `Ontology` loads the 24 implementable element classes from `vocabs/valis.ttl` at
  runtime, with their ports, ranges and units.
- `CircuitModel` resolves a circuit's elements, arcs and parameter bindings against it.
- `CircuitCompiler` validates and topologically orders, reporting every problem it finds
  with the element or arc at fault named: unknown or abstract class, unknown port, wrong
  direction, audio/control rate mismatch, duplicate or dangling arc, silent fan-in onto a
  non-Mixer input, wrong output count, and a feedback loop with no `val:UnitDelay` —
  printed as the path round the loop.
- [`examples/skream.ttl`](examples/skream.ttl) compiles clean: 13 nodes, both filter taps
  in use, and the gate's sidechain resolved as a control arc.

What M3 adds:

- All 24 elements, with the ontology and the registry asserted equal as sets in both
  directions — a class with no factory, or a factory with no class, fails the build.
- Antiderivative anti-aliasing, measured rather than claimed: on a 5 kHz sine driven at
  8×, ADAA1 cuts alias energy 4.2× and ADAA2 cuts it 16.4×. ADAA2's one-sample delay is
  reported through `latencyInSamples()`.
- Physical device models. `val:Diode` clips its positive half at +0.58 V and passes the
  negative half intact; `val:DiodePair` is symmetric. `val:StateVariable` is the Cytomic
  TPT form with lp, bp and hp taken from one state.

## Development

Build everything, run the tests, then build the release plugin:

```sh
./build.sh
```

`build.sh` configures `build/` as Debug with tests, runs CTest, then configures
`build-release/` as Release without tests. Set `JOBS` to override the parallelism.

Run the unit tests alone:

```sh
ctest --test-dir build --output-on-failure
```

Launch the standalone application — it prefers the Release build, falling back to
Debug:

```sh
./valis
```

Configure and build by hand, for example a tests-only core with no plugin targets:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DVALIS_BUILD_TESTS=ON \
  -DVALIS_BUILD_PLUGIN=OFF \
  -DVALIS_WITH_MCP=ON \
  -DVALIS_JUCE_DIR=/home/danny/github/JUCE
cmake --build build --parallel "$(nproc)"
```

The options are `VALIS_BUILD_TESTS` (ON), `VALIS_BUILD_PLUGIN` (ON), `VALIS_WITH_MCP`
(ON), `VALIS_WITH_CLAP` (OFF), and `VALIS_JUCE_DIR` (path to the JUCE checkout).

### CLAP

`build.sh` adds a third configuration in `build-clap/` when asked:

```sh
VALIS_WITH_CLAP=1 ./build.sh
```

That is equivalent to configuring with `-DVALIS_WITH_CLAP=ON`, which pulls in
`clap-juce-extensions` through `cmake/FindOrFetchClap.cmake`. That module is written
in M11, so this permutation does not build yet.

### LV2 verification

The LV2 bundle is written into the plugin's artefacts directory, so point `LV2_PATH`
at it rather than installing:

```sh
LV2_PATH="$PWD/build/valis_plugin_artefacts/Debug/LV2" lv2ls
LV2_PATH="$PWD/build/valis_plugin_artefacts/Debug/LV2" lv2info urn:valis:valis
```

`lv2info` reports the plugin's ports, but not its parameters: JUCE 9 exposes plugin
parameters through the LV2 patch extension rather than as control ports, and `lv2info`
does not walk those. To see the 64 slots, read the generated manifest directly:

```sh
grep -A5 'plug:p00' build/valis_plugin_artefacts/Debug/LV2/Valis.lv2/dsp.ttl
```

Use `build-release/valis_plugin_artefacts/Release/LV2` for the release bundle.

## Requirements

- CMake 3.22 or newer, and a C and C++20 compiler. JUCE refuses to configure unless
  C is among the project languages, so a C compiler is not optional.
- JUCE 9.0.1, added by `add_subdirectory`. `VALIS_JUCE_DIR` sets the path and defaults
  to `/home/danny/github/JUCE`.

On Debian or Ubuntu, with `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0` set by the build
(so neither WebKit nor libcurl is needed):

```sh
sudo apt install \
  build-essential cmake ninja-build pkg-config \
  libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libxi-dev
```

serd and sord are used through the system packages `libserd-dev` and `libsord-dev`
when pkg-config finds them, and otherwise fetched and compiled from source by
`cmake/FindOrFetchSerd.cmake`. Either way the build works with no action.

`lv2ls` and `lv2info` come from `lilv-utils` and are needed only for the LV2
verification above.

## Licence

This repository is MIT — see [`LICENSE`](LICENSE). JUCE 9 is AGPLv3-or-commercial, so
a distributed binary that links JUCE under the AGPL obliges AGPL for the combined work.
