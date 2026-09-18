# What Valis can do

Valis builds virtual analog instruments and effects from a text document. The
document is the circuit: there is no separate project format, and nothing is
compiled into the binary. This page lists what the system can do and, for each,
a short note on how.

For the full reference see [writing circuits](manual/circuits.md), the
[element reference](manual/elements.md) and the [MCP surface](manual/mcp.md).

## Circuits are documents

**A circuit is a set of elements and named arcs between their ports, written in
[RDF](https://en.wikipedia.org/wiki/Resource_Description_Framework) using
[Turtle](https://en.wikipedia.org/wiki/Turtle_(syntax)) syntax.** Topology is
modelled with explicit arcs rather than nesting or ordering, so the document is
a faithful picture of the dataflow and can be read, edited and generated.

**Element classes, port names and control ranges reuse the
[LV2](https://en.wikipedia.org/wiki/LV2) vocabulary** rather than inventing
terms. Ports carry their own range, unit and enumeration, so the knobs, the
readouts and the validation all come from one declaration.

**The ontology is loaded at runtime, not documented.** `vocabs/valis.ttl` is read
on startup and a test asserts that the classes it declares and the factories
registered in C++ match exactly in both directions, so a class with no
implementation or an implementation with no class fails the build.

**Errors are located and recoverable.** Bad Turtle carries line and column; an
unknown element, a bad port or a cycle names the element or arc at fault. A
failed load leaves the running circuit alone.

## Three views of the same circuit

**Code** is a syntax-highlighted Turtle editor with inline diagnostics.

**Circuit** is a node and arc graph with drag to connect and a palette built from
the ontology, so a newly declared class appears without any UI change. Node
positions live in a separate graph from execution metadata, so dragging a node
never invalidates the compiled circuit or interrupts the audio.

**Controls** generates knobs from the circuit's `val:Param` bindings. A control
is drawn by its shape rather than by default: a port declared toggled is a
switch, an enumeration is a selector strip with every option named, everything
else is a dial. Readout precision follows the port's declared unit, so a
frequency reads as `440 Hz` and a gain as `-6.0 dB`.

## The element set

**53 elements**, across four families: sources, filters, memoryless transfer
curves, and utility. They include oscillators, noise, envelopes, dynamics,
delays, modulation effects, and physical models.

**Filters and nonlinearities are device models, not relabelled curves.**
`val:Diode` is a Shockley-equation junction defaulting to a 1N4148 at room
temperature; `val:Triode` is a Koren-style valve stage; `val:Ladder` is a
nonlinear filter with drive. They carry physical parameters, so a circuit built
from them behaves like the circuit it names.

**Physical models share one waveguide core**
(`src/dsp/elements/Waveguide.h`): a fractional delay line, a one-pole loss
filter that reports its own phase delay, a DC blocker and deterministic
turbulence. An instrument writes only its exciter. `val:Flute`, `val:Reed`,
`val:StiffString` and `val:ModalBank` are built this way.

**Monitors are elements.** `val:Oscilloscope` and `val:FreqAnalyzer` appear in
the Controls view with live readouts, fed by a bounded ring the audio thread
writes and the message thread reads.

## Structure and reuse

**Subcircuits.** A `val:Subcircuit` declares its own ports and is instantiated
as if it were an element, so one definition serves a stereo pair or a whole drum
machine. The model expands every instance before compilation, renaming inner
elements after the instance, so the compiler and the engine never learn that
subcircuits exist. The Circuit view can draw an instance closed.

**Polyphony is one line.** `val:voices 8` on a subcircuit instance stamps the
definition out eight times as a bounded pool. The elements inside are the
ordinary monophonic ones; the engine gives each copy its own note. Allocation is
deterministic (a voice that never sounded, then the longest released, then the
oldest still held), and with more notes than voices the pool steals rather than
growing.

**Stereo is built in the patch.** Elements are mono. `val:Input` and
`val:Output` each offer `left`, `right` and a summed `out`, so a stereo circuit
is two chains, usually one subcircuit instantiated per channel.

**Feedback is explicit.** A graph cycle is legal exactly where a `val:UnitDelay`
establishes causality. The compiler keeps two adjacency graphs: one over audio
arcs for cycle detection, one over audio and control arcs for ordering, so a
control source always runs before its destination in the same block.

## Timing

**The host timeline reaches elements as transport.** Tempo, position and time
signature are carried forward one control slice at a time, so a musical phase
stays continuous inside a block rather than stepping at buffer boundaries.

**Note events are sample-accurate.** Each event carries its position in the
block and the engine cuts a control slice there, so a note starts on the sample
the host sent it on.

**Nothing is located by block index.** Anything that happens at a point in time
is located by stream position, so output is independent of the host's buffer
size. A test renders the same circuit at two block sizes and compares.

## Audio quality

**Band-limited oscillators.** PolyBLEP correction, measured: alias energy
relative to the fundamental drops 17 times for a saw and 29 for a square.

**Two answers to nonlinear aliasing.** `val:antialiasing` applies
[antiderivative anti-aliasing](https://en.wikipedia.org/wiki/Aliasing) to a
memoryless curve, measured at 4.2 and 16.4 times less alias energy for the first
and second order. `val:oversampling` runs any element at 2 to 16 times the rate,
measured at 185 times less through a hard-driven tanh at 8x. The first is far
cheaper; the second is for elements with memory, where the first does not apply.

**Latency is declared and reported.** Each element reports its own, the engine
sums it per circuit and the plugin tells the host, so delay is compensated
rather than hidden.

## Beyond audio in and audio out

**Spectral processing.** An element may work on a window rather than a sample.
`val:SpectralGate` is a short-time Fourier transform with overlap-add
resynthesis and declared latency, which lets it remove quiet broadband noise
from underneath a loud tone. `src/dsp/elements/Spectral.cpp` is the pattern to
copy.

**Event outputs.** An element may produce notes rather than a signal, through an
`atom:AtomPort` carrying `midi:MidiEvent`. `val:NoteOut` turns a control gate
into MIDI on the plugin's output; behind a frequency estimate it makes an audio
to MIDI converter out of parts that already existed.

**Sound files are resources with an identity.** A circuit naming a file may also
declare its `val:sha256`, channel count, sample rate and length. All are checked
when the file is read, so a sample that was replaced or truncated fails the load
with a message rather than playing something else.

## Driving it from outside

**Every operation is an `Op`.** The three views and the MCP server are thin
adapters over one headless command surface, so there is no second implementation
to keep in step.

**An in-process HTTP [MCP](https://modelcontextprotocol.io/) server exposes 16
tools**, covering the Turtle document, the graph, elements and arcs, parameters,
samples and diagnostics. Inserting an element over MCP redraws the graph view
while the audio keeps running. See the
[guide for agents](guide-for-agents.md).

**A built-in console** runs commands and talks to a language model, which can
design and edit circuits in place. It trims what it sends to fit a provider's
limits and moves to another provider when one will not serve a request.

## Running it

**Standalone, VST3 and LV2 build by default**, with CLAP behind a CMake option.
One command, `./build.sh`, builds everything and runs the tests.

**`valis-render` is a headless renderer** that needs no host, GUI or audio
device. It synthesises a transport from a tempo, can send note events, and
writes a wav file, which is what makes the audio tests deterministic and
checkable offline.

## What the design guarantees

**Nothing is allocated on the audio thread.** RDF parsing, model building and
compilation happen on the message thread; a compiled circuit is fully
preallocated and handed to the engine by an atomic pointer swap, and the retired
one is freed on the message thread. A test enforces this by counting through an
overridden `operator new`.

**Renders are reproducible.** Every random source is seeded per element and
restored on reset, so the same circuit produces the same samples every time.
Without that, no audio test would mean anything.

**Drift is a test failure.** The ontology against the DSP registry, each
element against its own port declarations, and the shipped examples against the
compiler are all checked by the 21 test binaries that `./build.sh` runs.
