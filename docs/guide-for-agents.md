# Valis: Guide for Agents

This document tells an AI agent how to build and modify instruments and effects
processors in Valis using the MCP interface. Read it before calling any tools.

The server also offers this guide and every example as resources, so you can read them
without access to the repository: `valis://guide`, `valis://examples` (an index saying
what each one is) and `valis://examples/NAME`, such as `valis://examples/basic`.

Three other files are worth knowing about, and none of them is a substitute for
asking the running program:

- [`manual/elements.md`](manual/elements.md) is the element reference. It is
  generated from the ontology, so every port symbol, range, unit and description
  in it is exact. `list_element_types` returns the same ports from the live program.
- [`examples.md`](examples.md) describes every circuit in `examples/`. Start from
  the closest one rather than from nothing: `load_file` loads it, and `get_turtle`
  shows you how it is written.
- [`manual/mcp.md`](manual/mcp.md) covers running the server, its port and its
  token.

## Quick start

1. Confirm the circuit is loaded and compiled:

   ```
   tools/call  get_diagnostics
   ```

   If it cannot connect, the server is off. It binds to `127.0.0.1` on port 7676
   (`VALIS_MCP_PORT` changes that) and is enabled from Settings in the plugin. With several
   instances loaded, each takes the next free port above that, and `/health` names the one that
   answered: see [the MCP manual](manual/mcp.md#several-instances).

2. Read the current circuit and the available element types:

   ```
   resources/read  valis://turtle
   resources/read  valis://element-types
   resources/read  valis://examples        (to choose a starting point)
   ```

3. Inspect the graph before making changes:

   ```
   tools/call  get_graph
   ```

4. Build or modify, then check the result with `get_diagnostics` and `lint`, and
   then listen to it (see [Hearing a circuit](#hearing-a-circuit)).

## The tools

| tool | what it does |
|---|---|
| `get_turtle`, `set_turtle`, `validate` | read the circuit's source, replace it, or check source without installing it |
| `load_file`, `save_file` | read a Turtle file from disk into the circuit, or write the circuit to one |
| `list_element_types` | every element class with its ports, ranges and units |
| `describe_element` | one class in full: what it is for, every port with its range, unit, choices and description, and the options it takes |
| `get_graph` | the circuit as JSON: elements, properties, arcs |
| `add_node`, `remove_node` | add an element by class, or remove one and its arcs |
| `connect`, `disconnect` | join or separate two ports |
| `apply_edits` | several of those four as one: all take effect or none do, installed once |
| `history`, `undo`, `redo` | the versions the circuit has had, and going back and forward over them |
| `diff` | what changed between two versions, in plain sentences |
| `list_params`, `get_param`, `set_param` | the host parameter bindings, and their live values |
| `morph` | move every parameter that has two stored ends between them |
| `randomize` | give the parameters new random values from a seed |
| `get_sample`, `set_sample` | the file a player or model element is using, and a different one |
| `get_diagnostics` | whether the circuit loaded, its size and latency, and `problems`: what an element recovered from |
| `lint` | static checks for things that compile and still do not do what was meant |
| `check_stability` | run the circuit with random settings of its knobs, all jumping at once, and report any that blow up |
| `get_profile` | what the circuit says it is: its role, signals and cautions |
| `note_on`, `note_off`, `all_notes_off` | play the circuit as a keyboard would |
| `read_outputs` | the live value of every control output, or one node's |
| `render` | render the circuit offline to a wav file; reports peak, RMS and a full analysis |
| `analyze` | measure any wav file: pitch, octave levels, brightness, onsets, clipping |

A failed edit leaves the previous circuit playing. Every edit goes through the
same validation as a typed one.

## Concepts

### Elements and arcs

A circuit is a set of **elements** (nodes) joined by **arcs** (edges). Elements
are typed: `val:Oscillator`, `val:Ladder`, `val:VCA` and so on. An arc connects a
named output port on one element to a named input port on another.

Every element class, with its port symbols, value ranges and units, is declared in
the ontology. Always call `list_element_types` or read `valis://element-types`
before wiring ports. Symbol names are exact and case-sensitive.

`list_element_types` is the whole catalogue and says little about each entry. When you
are about to use an element you do not know, call **`describe_element`** with its name
(`val:Ladder`, `Ladder` or `ladder`). It returns what the element is for and how to wire
it, in the ontology's own words; every port with its default, range, unit and named
choices; and the options it takes besides its ports. A misspelt name comes back with the
nearest real ones.

### Four kinds of port

| kind | carries | arc |
|---|---|---|
| audio | a signal at the sample rate | `val:Arc` |
| control | one value per 32-sample block | `val:ControlArc` |
| event | notes and other timed events, sent to the host | none; the element sends them itself |
| buffer | material one element owns and another reads | `val:Arc` between two buffer ports |

Use audio ports (`"in"`, `"out"`, `"left"`, `"right"`) with `val:Arc` and control
ports (`"frequency"`, `"cv"`, `"cutoff"`) with `val:ControlArc`. The `connect` tool
works out which kind an arc is from the ports, so you do not say. A buffer output
joins only a buffer input; an input takes one source and an output can feed any
number of inputs. `list_element_types` names each port's kind in `rate`.

### Control arcs replace, they do not add

A control arc **overwrites** the destination port's value every block. If a
`val:ControlArc` targets `val:Ladder.cutoff`, any fixed `val:cutoff` set on the
Ladder instance is ignored while the arc is connected.

Consequence: **the resting value of a controlled port must live in the control
source, not on the element.** For a filter sweep, put the base cutoff in
`val:Scale.min` and the peak in `val:Scale.max`, then route
`Envelope → Scale → Ladder.cutoff`. Do not set `val:cutoff` directly on the
Ladder when an arc reaches that port.

### Options

Some properties are not ports. A file name (`val:file`), a pattern (`val:pattern`
on a `val:StepSequencer`), a tuning file (`val:tuning`) and a buffer length
(`val:seconds`) are **options**, set on the element in the Turtle. An option that
cannot be applied, such as a file that does not exist, fails the load with a
message naming the element and the option. `list_element_types` shows ports only;
`describe_element` lists the options an element takes, with what each means.

A relative file path is looked up in the working directory, then in `examples/`,
then in the repository root.

### Parameters

A `val:Param` binding exposes an element's property as a host parameter knob.
Up to 64 slots (0 to 63) are available. Slots do not need to be contiguous.
`list_params` shows which are bound; `set_param` changes a value in real time.
Give a binding a `lv2:minimum` and `lv2:maximum` when the element's own range is
wider than a knob should be.

A binding that also declares `val:valueA` and `val:valueB` takes part in `morph`:
position 0 sets it to A, position 1 to B, and in between to the straight-line
blend, so one call moves every such knob at once.

### Node IDs

Every node has a full IRI, such as `urn:valis:basic#vcf`. The local prefix is the
circuit's base IRI. Use exact IRIs when calling `add_node`, `connect`,
`disconnect` and `remove_node`. Class names are written `val:Ladder`.

### Time and the host

Anything that depends on musical time reads the host's position, so a circuit
stays in time when the host loops or relocates. `val:Transport` gives phase and
pulse signals, and `val:StepSequencer` plays a pattern against the position. When
the host is stopped they run on their own at the last tempo it reported.
`valis-render --tempo 130 --rolling` gives an offline render a playing host.

### Stereo

`val:Input` has `out` (the channels summed), `left` and `right`. `val:Output` has
`in` (both channels) and `left` and `right`. Elements are mono, so a stereo
circuit is two chains. A `val:Subcircuit` written once and instantiated per
channel is the tidy way; see `examples/subcircuit.ttl`.

## Workflow

### Build from scratch

1. Start from the closest example if there is one: `load_file` with a path under
   `examples/`, then change it.

2. Otherwise call `set_turtle` with a complete, valid Turtle document. This is the
   fastest path when you have a clear design in mind. Use `validate` first to check
   it without installing. A circuit needs exactly one `val:Output`.

3. To extend a circuit that is loaded without rewriting it, send the edits together in
   one `apply_edits` call (see [Several edits at once](#several-edits-at-once)):

   ```
   get_graph          → read current nodes and arcs
   apply_edits        → add the new elements and wire them
   set_param          → tweak parameter slots if needed
   get_diagnostics    → confirm it compiled
   lint               → find what compiled and is still wrong
   ```

### Several edits at once

`apply_edits` takes a list of `add_node`, `remove_node`, `connect` and `disconnect` edits
and applies them as one.

- **All or nothing.** The edits are tried in order on a private copy. If one cannot be
  applied, none of them are: the reply names it ("Edit 3 of 8 (connect osc.out to
  vca.Input) failed: ... Did you mean 'in'?") and the circuit, and the sound playing, are
  exactly as they were.
- **Installed once.** The result is handed to the engine in one step, so what is playing
  never passes through a half-built circuit, and eight edits cost one install.
- **Edits can use each other.** A `connect` may name a node an earlier `add_node` made.
  Ids without a colon are in the circuit's own namespace: `"osc"` means `urn:valis:new#osc`
  in a circuit called `urn:valis:new#main`.
- **Only the whole must be sound.** A state between two edits need not be a circuit that
  could run, so a batch can remove the output and add another. Problems that exist only in
  the result (two arcs into one input, no output) are reported when it is installed, and
  then too nothing changes.

```
apply_edits  {"edits": [
  {"op": "add_node", "id": "osc", "class": "val:Oscillator"},
  {"op": "add_node", "id": "vca", "class": "val:VCA"},
  {"op": "connect", "from_node": "osc", "from_port": "out", "to_node": "vca", "to_port": "in"},
  {"op": "connect", "from_node": "vca", "from_port": "out", "to_node": "out", "to_port": "in"}
]}
```

For an edit to a value or a parameter, there is no batch form: use `set_turtle`, or
`set_param` after the structure is in place.

### Trying things safely

Every change made through these tools is kept as a version, so an experiment costs nothing.

- `history` lists the versions, oldest first, each with a number and what made it
  (`connect osc.out to vca.in`, `apply_edits (8 edits)`), and says which is current.
- `undo` takes back the last change and `redo` brings it back. An `apply_edits` batch is one
  version, so one `undo` takes back the whole batch. A new change after an undo ends the redo.
- `diff` says what a change did, in sentences: `added lfo (val:LFO)`, `vcf.cutoff changed from
  800 to 1200`, `connected lfo.out to vcf.cutoff (control)`, `bound slot 3 (Cutoff, vcf.cutoff)`.
  With no arguments it compares the current version with the one before, which is what the last
  change did; give `from` and `to` (numbers from `history`) to compare any two. Use it to check
  that an edit did what you meant, and to describe a change without dumping the Turtle.

A change made in the plugin's editor, outside these tools, is kept as its own version
("changed outside these tools") the next time you use one of them, so undo does not lose it. If
the editor holds text that does not compile, `undo` puts back the last text that did. At most 100
versions are kept, and numbers are never reused. A failed edit is not a version.

### Modify an existing circuit

1. Call `get_turtle` to read the current source.
2. Plan the change: which nodes to add, remove or rewire.
3. For small edits, use `add_node`, `remove_node`, `connect` and `disconnect`
   individually, or together in `apply_edits`. Each call is atomic; a failed call
   leaves the circuit unchanged.
4. For large restructures, edit the Turtle and call `set_turtle`.

### Check your work

`get_diagnostics` returns whether the circuit loaded, its element and arc counts and
its latency. Parse errors carry a line and column; structural errors name the element
or arc. An element can load and still report a problem it recovered from, such as a
missing sound file: those are in `problems`, one entry per element, and an empty list
means there were none.

`lint` reports what compiled but is probably not what you meant. Run it after every
structural change; it costs nothing and it finds the mistakes that are hardest to hear.
Before calling a circuit finished, also run `check_stability`.

| code | severity | means |
|---|---|---|
| `nothing-reaches-output` | error | nothing is connected to the output, so the circuit is silent |
| `control-overrides-value` | warning | a value is set on a port that a control arc replaces every block, so it is never used |
| `knob-overridden` | warning | a parameter is bound to a port that a control arc replaces, so the knob does nothing |
| `not-heard` | warning | an element has no path to the output, so it cannot be heard |
| `reads-silence` | warning | an effect has nothing connected to any audio input |
| `unknown-property` | warning | a `val:` property on an element is neither a port nor an option of it, usually a misspelt port name, which sets nothing; the finding offers the nearest real names |
| `duplicate-param-symbol` | warning | two parameters share a symbol, which a host uses to identify automation |
| `no-profile` | info | the circuit declares no `trn:PluginProfile` |

Each finding names the node and port and says what to do. A warning is not always a
mistake (an element can be unheard on purpose, while you build), but it is always
worth knowing about.

## Hearing a circuit

You cannot listen, so measure. Loading is not the same as working: a circuit can
compile and be silent, or clip.

- `note_on` then `read_outputs` shows what the control signals are doing while a
  note is held. `read_outputs` with a `node` shows one element, which is how to
  read a `val:Oscilloscope` or `val:FreqAnalyzer`, an envelope, or an LFO.
- `render` writes a wav offline and reports peak and RMS, followed by an `analysis`
  of the sound. Use it after every structural change. Pass `note` for an instrument,
  or leave it out for an effect.
- The `analysis` is what to read in place of listening. Start with its `notes`, which
  say in plain words what is wrong or what was found, then the numbers:

  | field | tells you |
  |---|---|
  | `peakDb`, `rmsDb`, `clipped` | how loud, and whether it clips; peak above 0 needs gain staging |
  | `pitch` | whether it has a pitch, and its `hz`, `note` name and `cents` from that note; `stability` is how steady it is |
  | `bandsDb` | the level of each octave from 63 Hz to 16 kHz, in dBFS; `bandCentresHz` names them |
  | `centroidHz`, `rolloffHz` | brightness: higher means brighter |
  | `onsets` | the seconds at which each new sound begins; count them to check a rhythm |
  | `envelopeDbPerQuarterSecond` | how the level moves: a pluck falls, a pad holds |
  | `firstSoundAt`, `silentFraction` | whether it starts late or is mostly silent |
  | `dc`, `nonFinite` | an offset, or values that are not numbers (an unstable circuit) |
  | `stereoCorrelation`, `balanceDb` | how alike the channels are, and which is louder |

  A peak of 0 means silence: check the output is wired, a gate is reaching the
  envelope, and a control arc is not overwriting something it should not.
  A pitch of the wrong octave can be right: a circuit that mixes in a sub-oscillator
  repeats at the lower pitch, and that is the pitch the analysis names.
- `analyze` does the same for a file you already have, such as a recording to
  match or the render of an earlier version to compare with.
- A circuit that processes audio needs an input to process. `render` has none, so
  an effect renders silence unless it generates sound itself. The `valis-render`
  tool takes `--tone HZ` for a sine, `-i FILE` for a recording, and `--tempo` and
  `--rolling` for a playing host. It also reads any Turtle file without the
  plugin running.
- To compare two versions, render both and `analyze` each; compare pitch, `bandsDb`
  and `centroidHz`.
- `check_stability` answers a different question from `render`: not "does it sound right at the
  settings it has" but "does anything a player can do to it make it fall over". It runs the circuit
  on a fresh engine several times, setting every **bound parameter** to a random value in its range
  (evenly, or evenly in the logarithm where the range spans decades, as a frequency is swept), and
  then moving all of them again halfway through, as a preset change would. The first run is the
  declared settings; the rest are numbered from 1 and can be repeated. Each run is judged:

  | verdict | means |
  |---|---|
  | `ok` | finite, between silence and full scale |
  | `clips` | stayed finite but peaked above 0 dBFS |
  | `blows-up` | peaked above +40 dBFS: some combination multiplies gain without limit |
  | `non-finite` | produced values that are not numbers: something becomes unstable |
  | `silent` | nothing came out at all at those settings |

  For a run that is not `ok` the reply gives every parameter's value at the start and after the
  jump, so you can set them with `set_param` and `render` to hear it. The notes say what to do:
  usually a `val:Limiter` before the output, or a narrower range on the parameter.
  **Only bound parameters are varied**, so bind the controls a player would reach with `val:Param`
  before relying on the result; with none bound it tries only the declared settings and says so.
  Pass `note` (default 60) for an instrument, or -1 for none; an effect is given noise and a tone.
  What is playing is not touched.

## Patterns

### Synthesiser (MIDI-driven)

Minimum: `MidiPitch → Oscillator → Output`

```
MidiPitch.out  ──ControlArc──► Oscillator.frequency
Oscillator.out ──Arc──────────► Output.in
```

Add amplitude shaping:

```
Envelope.out ──ControlArc──► VCA.cv
Oscillator.out ──Arc────────► VCA.in
VCA.out ──Arc───────────────► Output.in
```

The `Envelope` gates on and off from host MIDI automatically unless a `val:NoteGate`
is wired to its `gate` port.

### Filter sweep

Route the envelope through `val:Scale` so the resting cutoff is controllable:

```
Envelope.out ──ControlArc──► Scale.in
Scale.out    ──ControlArc──► Ladder.cutoff
```

Set `val:min` on Scale to the base cutoff (Hz) and `val:max` to the peak cutoff.
Do not set `val:cutoff` on the Ladder, because the arc overwrites it.

### Effects processor (audio in and out)

```
Input.out ──Arc──► [effect chain] ──Arc──► Output.in
```

Use `val:Input` for the plugin's audio input and `val:Output` for its output.
To keep the dry signal at full level and add an effect to it, send both to a
`val:Mixer`; `examples/dd3.ttl` does this.

### LFO modulation

```
LFO.out ──ControlArc  val:depth 0.3 ──► Oscillator.frequency
```

Set `val:depth` on the arc (0 to 1) to scale the modulation amount. At depth 0 the
arc is connected but has no effect, which is useful for a rate-controlled vibrato
that starts off. `val:LFO` also takes a `retrigger` gate, a `sync` in beats to lock
it to the host, and a drawn curve in `val:curve`.

### Sequenced parts

A `val:StepSequencer` plays a pattern in time with the host. Its `gate` goes to an
envelope and its `pitch` (in hertz) to an oscillator:

```turtle
:bass a val:StepSequencer ;
    val:pattern "F2 F3 F2 F3 . . Eb2 Eb3" ;   # a note name, a number, or . for a rest
    val:division 0.25 .                        # a sixteenth a step
```

`examples/blue-monday-sketch.ttl` has a bass and four drum parts this way.

### Per-note drum voices

Use `val:NoteGate` to route a specific MIDI note number to an envelope or trigger:

```
NoteGate.gate ──ControlArc──► Envelope.gate
```

Set `val:note` on the `NoteGate` to the MIDI note number (0 to 127) the voice
should respond to.

For `val:TwinTBridge` drum resonators: connect the amplitude envelope directly to
the VCA `cv`, and route the NoteGate velocity to `TwinTBridge.velocity`. Do not
route velocity through the VCA `cv` path. Velocity goes to 0 on note-off, which
closes the VCA before the resonator finishes its decay.

To make sounds on one voice cut each other off (a closed hi-hat stopping an open
one), send the other sounds' gates through a `val:ControlMax` into the player's
`choke`; `examples/dmx.ttl` shows it.

### Sample players, stretching and granulation

- `val:SampleLoad` plays a file; `val:TimeStretch` plays one at a different speed
  or pitch; `val:GrainBuffer` holds one for grains. The file is an option
  (`val:file`), and `set_sample` swaps it in a session without editing the document.
- One `val:GrainBuffer` can feed several `val:GrainCloud` elements through buffer
  arcs. `examples/grain-clouds.ttl` has three.
- A relative path is resolved as described under Options.

### Sectioned knob panel

Group related parameters with `val:section`:

```turtle
:p0 a val:Param ; val:slot 0 ; val:target :env ; val:property val:attack ;
    lv2:name "Attack" ; lv2:symbol "attack" ; val:section "Envelope" .
```

The Controls view draws a thin separator and label between sections.

### Saying what a circuit is

Every shipped circuit declares a `trn:PluginProfile`: its role, what it accepts and
produces, and any caution. `get_profile` reads it. Add one to a circuit you build,
so a catalogue can list it; see any file in `examples/` for the properties.

## Choosing an element

The full list, with every port, is in [`manual/elements.md`](manual/elements.md).
This table is a start for common jobs. Each port named in it exists; the test
`tests/docs/GuideTest.cpp` fails if one stops existing.

| class | role | key ports |
|---|---|---|
| `val:Oscillator` | band-limited VCO | `frequency`, `shape`, `out` |
| `val:Wavetable` | morphing wavetable oscillator | `frequency`, `position`, `set`, `out` |
| `val:Noise` | white or pink noise | `colour`, `out` |
| `val:LFO` | low-frequency oscillator | `rate`, `shape`, `retrigger`, `sync`, `out` |
| `val:StepSequencer` | pattern of notes and rests against the host | `division`, `gatelength`, `gate`, `note`, `pitch` |
| `val:MidiPitch` | note to hertz, with an optional tuning | `reference`, `root`, `out` |
| `val:MidiVelocity` | note velocity, 0 to 1 | `out` |
| `val:NoteGate` | per-note gate | `note`, `gate` |
| `val:Envelope` | ADSR | `attack`, `decay`, `sustain`, `release`, `gate`, `out` |
| `val:Ladder` | Moog-style low-pass | `in`, `cutoff`, `resonance`, `drive`, `out` |
| `val:StateVariable` | 12 dB filter with all three responses at once | `in`, `cutoff`, `resonance`, `drive`, `lp`, `bp`, `hp` |
| `val:Biquad` | shelves, bell, notch, low and high cut | `in`, `mode`, `frequency`, `q`, `out` |
| `val:OnePole` | 6 dB low-pass or high-pass | `in`, `cutoff`, `mode`, `out` |
| `val:VCA` | voltage-controlled amplifier | `in`, `cv`, `out` |
| `val:Gain` | fixed gain in decibels | `in`, `gain`, `out` |
| `val:Mixer` | audio sum | `in`, `left`, `right`, `out` |
| `val:DryWet` | crossfade a processed signal against the original | `dry`, `wet`, `mix`, `out` |
| `val:Scale` | remap 0 to 1 onto a range | `in`, `min`, `max`, `out` |
| `val:ControlMultiply` | multiply two controls | `a`, `b`, `out` |
| `val:ControlAdd` | add three controls | `a`, `b`, `c`, `out` |
| `val:Slew` | glide a control to its target over a time | `in`, `rise`, `fall`, `out` |
| `val:ControlMax` | the largest of three controls; an OR for gates | `a`, `b`, `c`, `out` |
| `val:AudioSelect` | pick one of three audio inputs | `a`, `b`, `c`, `select`, `out` |
| `val:DiodePair` | soft clipper | `in`, `seriesResistance`, `out` |
| `val:Tanh` | tanh saturator | `in`, `gain`, `out` |
| `val:Tape` | saturation, wow, flutter and hiss | `in`, `drive`, `wow`, `flutter`, `hiss`, `out` |
| `val:Delay` | clean delay line with feedback | `in`, `time`, `feedback`, `out` |
| `val:BucketBrigade` | analogue-style delay that darkens as it lengthens | `in`, `time`, `feedback`, `bandwidth`, `out` |
| `val:Reverb` | feedback delay network reverb | `in`, `size`, `decay`, `damping`, `mix`, `out` |
| `val:Compressor` | compressor with an optional upward stage | `in`, `threshold`, `ratio`, `attack`, `release`, `out` |
| `val:Limiter` | lookahead brickwall limiter | `in`, `ceiling`, `release`, `out` |
| `val:PitchShift` | pitch shift in semitones | `in`, `pitch`, `mix`, `out` |
| `val:FreqShifter` | shift every partial by hertz | `in`, `shift`, `feedback`, `out` |
| `val:Reed` | waveguide clarinet voice | `frequency`, `pressure`, `stiffness`, `damping`, `out` |
| `val:Brass` | lip-reed waveguide trumpet or horn voice | `frequency`, `pressure`, `lip`, `breath`, `damping`, `out` |
| `val:Shaker` | PhISEM shaken-particle percussion (maracas, cabasa, guiro) | `trigger`, `velocity`, `particles`, `shake`, `sound`, `frequency`, `resonance`, `regularity`, `scrape`, `out` |
| `val:Pluck` | short plucked-string exciter for a string or comb; position and hardness set the tone | `trigger`, `velocity`, `frequency`, `position`, `hardness`, `out` |
| `val:TwinTBridge` | bridged-T drum resonator | `frequency`, `decay`, `trigger`, `velocity`, `out` |
| `val:SampleLoad` | plays a sound file | `speed`, `start`, `loop`, `trigger`, `choke`, `out` |
| `val:TimeStretch` | a sound file at its own speed and pitch | `speed`, `pitch`, `formant`, `loop`, `out` |
| `val:Input` | plugin audio input | `out`, `left`, `right` |
| `val:Output` | plugin audio output | `in`, `left`, `right` |

## Worked example: a minimal synth through the tools

Start from an empty or placeholder circuit, then build it in one call.

```
# 1. Replace with a skeleton
set_turtle  →  (paste the Turtle below)

@prefix val: <http://purl.org/stuff/valis/> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix :    <urn:valis:new#> .
:main a val:Circuit ; rdfs:label "New" ; val:element :out .
:out  a val:Output .

# 2. Add and wire everything at once. The kind of each arc, audio or control, is worked out
#    from its ports. Ids without a colon are in this circuit's namespace.
apply_edits  {"edits": [
  {"op": "add_node", "id": "pitch", "class": "val:MidiPitch"},
  {"op": "add_node", "id": "osc",   "class": "val:Oscillator"},
  {"op": "add_node", "id": "env",   "class": "val:Envelope"},
  {"op": "add_node", "id": "vca",   "class": "val:VCA"},
  {"op": "connect", "from_node": "osc",   "from_port": "out", "to_node": "vca", "to_port": "in"},
  {"op": "connect", "from_node": "vca",   "from_port": "out", "to_node": "out", "to_port": "in"},
  {"op": "connect", "from_node": "pitch", "from_port": "out", "to_node": "osc", "to_port": "frequency"},
  {"op": "connect", "from_node": "env",   "from_port": "out", "to_node": "vca", "to_port": "cv"}
]}

# 3. Check it loaded, that nothing is wrong that still compiles, and that it sounds
get_diagnostics
lint
render  {"path": "/tmp/new.wav", "note": 60, "seconds": 2}      # peak and RMS must not be 0
```

## Common mistakes

**Mixing audio and control ports.**
An audio output into a control input, or the reverse, is a compile error naming
the ports. Check each port's `rate` in `list_element_types`.

**Setting a fixed value on a port that a control arc already targets.**
The arc wins every block. Move the resting value into the control path.

**Reusing a parameter slot.**
Each slot (0 to 63) may only be bound once. Check `list_params` before adding a
`val:Param`.

**Targeting a port symbol that does not exist.**
Port symbols are exact. `"cutoff"` is not `"Cutoff"`. A "no port" error names the nearest
real ports and lists the ones of the right direction, so read it before looking elsewhere.
`val:StateVariable` has no `out`; it has `lp`, `bp` and `hp`.

**A misspelt property in the Turtle.**
`val:cutof 800` is not an error: any `val:` property that is not a port is taken as an
option, and an option nothing reads does nothing. `lint` reports it as `unknown-property`
with the nearest real names; run it after writing Turtle by hand.

**Two arcs into one audio input.**
Only `val:Mixer` sums its inputs. Anywhere else it is an error, so add a Mixer.

**Creating a feedback loop without `val:UnitDelay`.**
A cycle in the audio graph is an error unless every cycle passes through a
`val:UnitDelay` to break it. A feedback amount inside an element, such as a delay's
`feedback`, is not a cycle in the graph and needs nothing.

**A circuit with no `val:Output`, or two.**
There must be exactly one.

**Loading is not working.**
A circuit that compiles can still be silent or clip. Run `lint`, render it, and
read the analysis's `notes` before saying it is done.

**Forgetting that an effect needs input.**
`render` feeds an effect nothing. Use `valis-render --tone` or `-i` to run audio
through it.

**An unreadable option.**
A misspelt note in a `val:pattern`, a file that is not there or a tuning file with
a bad line fails the load, and the message names the element and the option.
