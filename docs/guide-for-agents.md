# Valis — Guide for Agents

This document tells an AI agent how to build and modify instruments and effects
processors in Valis using the MCP interface. Read it before calling any tools.

## Quick start

1. Confirm Valis is running and the MCP server is on:

   ```sh
   curl -s localhost:7676/health
   ```

2. Read the current circuit and available element types:

   ```
   resources/read  valis://turtle
   resources/read  valis://element-types
   ```

3. Inspect the graph before making changes:

   ```
   tools/call  get_graph
   ```

4. Build or modify, then verify with `get_diagnostics`.

## Concepts

### Elements and arcs

A circuit is a set of **elements** (nodes) joined by **arcs** (edges). Elements
are typed — `val:Oscillator`, `val:Ladder`, `val:VCA`, etc. Arcs are either
audio arcs (`val:Arc`) or control arcs (`val:ControlArc`); they connect a named
output port on one element to a named input port on another.

Every element class, with its port symbols, value ranges, and units, is declared
in the ontology. Always call `list_element_types` or read `valis://element-types`
before wiring ports — symbol names are exact and case-sensitive.

### Audio arcs vs control arcs

| arc type | rate | use |
|---|---|---|
| `val:Arc` | sample rate | audio signal flow |
| `val:ControlArc` | once per 32-sample block | modulation, pitch, gate |

Use `val:Arc` for audio connections (`"in"`, `"out"`, `"left"`, `"right"`).
Use `val:ControlArc` for control connections (`"frequency"`, `"cv"`, `"cutoff"`, etc.).

In the `connect` tool, the `control` boolean selects which kind to create.

### Control arcs replace, they do not add

A control arc **overwrites** the destination port's value every block. If a
`val:ControlArc` targets `val:Ladder.cutoff`, any fixed `val:cutoff` set on
the Ladder instance is permanently ignored while the arc is connected.

Consequence: **the resting value of a controlled port must live in the control
source, not on the element.** For a filter sweep, put the base cutoff in
`val:Scale.min` and the peak in `val:Scale.max`, then route
`Envelope → Scale → Ladder.cutoff`. Do not set `val:cutoff` directly on the
Ladder when an arc reaches that port.

### Parameters

A `val:Param` binding exposes an element's property as a host parameter knob.
Up to 64 slots (0–63) are available. Slots do not need to be contiguous.
`list_params` shows which are bound; `set_param` changes a value in real time.

### Node IDs

Every node has a full IRI, e.g. `urn:valis:basic#vcf`. The local prefix is the
circuit's base IRI. Use exact IRIs when calling `add_node`, `connect`,
`disconnect`, and `remove_node`.

## Workflow

### Build from scratch

1. Call `set_turtle` with a complete, valid Turtle document. This is the fastest
   path when you have a clear design in mind. Use `validate` first to check
   syntax without installing.

2. Add nodes incrementally if the circuit is already loaded and you want to
   extend it without rewriting everything:

   ```
   get_graph          → read current nodes and arcs
   add_node           → insert the new element
   connect            → wire its ports
   set_param          → tweak parameter slots if needed
   get_diagnostics    → confirm it compiled
   ```

### Modify an existing circuit

1. Call `get_turtle` to read the current source.
2. Plan the change — which nodes to add, remove, or rewire.
3. For small edits, use `add_node` / `remove_node` / `connect` / `disconnect`
   individually. Each call is atomic; a failed call leaves the circuit unchanged.
4. For large restructures, edit the Turtle and call `set_turtle`.

### Check your work

`get_diagnostics` returns:
- whether the circuit loaded successfully
- element count, arc count, latency
- any error messages with line/column for parse errors, or element/arc name for
  structural errors

A failed `set_turtle` or `add_node` leaves the previous circuit playing.

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

The `Envelope` gates on/off from host MIDI automatically unless a `val:NoteGate`
is wired to its `gate` port.

### Filter sweep

Route the envelope through `val:Scale` so the resting cutoff is controllable:

```
Envelope.out ──ControlArc──► Scale.in
Scale.out    ──ControlArc──► Ladder.cutoff
```

Set `val:min` on Scale to the base cutoff (Hz) and `val:max` to the peak cutoff.
Do not set `val:cutoff` on the Ladder — the arc overwrites it.

### Effects processor (audio in/out)

```
Input.out ──Arc──► [effect chain] ──Arc──► Output.in
```

Use `val:Input` for the plugin's audio input, `val:Output` for its output.
The circuit has exactly one `val:Output`.

### LFO modulation

```
LFO.out ──ControlArc  val:depth 0.3 ──► Oscillator.frequency
```

Set `val:depth` on the arc (0–1) to scale the modulation amount. At depth 0 the
arc is connected but has no effect — useful for a rate-controlled vibrato that
starts off.

### Per-note drum voices

Use `val:NoteGate` to route a specific MIDI note number to an envelope or
trigger:

```
NoteGate.gate ──ControlArc──► Envelope.gate
```

Set `val:note` on the `NoteGate` to the MIDI note number (0–127) the voice
should respond to.

For `val:TwinTBridge` drum resonators: connect the amplitude envelope directly
to the VCA cv, and route NoteGate velocity to `TwinTBridge.velocity`. Do not
route velocity through the VCA cv path — velocity goes to 0 on note-off, which
closes the VCA before the resonator finishes its decay.

### Sectioned knob panel

Group related parameters with `val:section`:

```turtle
:p0 a val:Param ; val:slot 0 ; val:target :env ; val:property val:attack ;
    lv2:name "Attack" ; lv2:symbol "attack" ; val:section "Envelope" .
```

The Controls view draws a thin separator and label between sections.

## Element quick reference

Call `list_element_types` for the full list with port symbols and ranges.
Common elements:

| class | role | key ports |
|---|---|---|
| `val:Oscillator` | band-limited VCO | `frequency`, `shape`, `out` |
| `val:Noise` | white / pink noise | `colour`, `out` |
| `val:LFO` | low-frequency oscillator | `rate`, `shape`, `out` |
| `val:MidiPitch` | note → Hz | `out` (control) |
| `val:MidiVelocity` | note velocity 0–1 | `out` (control) |
| `val:NoteGate` | per-note gate | `note`, `gate` (control) |
| `val:Envelope` | ADSR | `attack`, `decay`, `sustain`, `release`, `gate`, `out` |
| `val:Ladder` | Moog-style low-pass | `in`, `cutoff`, `resonance`, `drive`, `out` |
| `val:StateVariable` | 12 dB SVF | `in`, `cutoff`, `resonance`, `mode`, `out` |
| `val:OnePole` | 6 dB LP/HP | `in`, `cutoff`, `out` |
| `val:VCA` | voltage-controlled amp | `in`, `cv`, `out` |
| `val:Gain` | fixed gain (dB) | `in`, `gain`, `out` |
| `val:Mixer` | audio sum | `in`, `left`, `right`, `out` |
| `val:Scale` | remap 0–1 → [min, max] | `in`, `min`, `max`, `out` |
| `val:ControlMultiply` | multiply two controls | `a`, `b`, `out` |
| `val:DiodePair` | soft clipper | `in`, `seriesResistance`, `out` |
| `val:Tanh` | tanh saturator | `in`, `out` |
| `val:Delay` | delay line | `in`, `time`, `feedback`, `mix`, `out` |
| `val:Reed` | digital waveguide clarinet | `frequency`, `pressure`, `stiffness`, `damping`, `out` |
| `val:TwinTBridge` | bridged-T drum resonator | `frequency`, `decay`, `trigger`, `velocity`, `out` |
| `val:Input` | plugin audio input | `out` |
| `val:Output` | plugin audio output | `in`, `left`, `right` |

## Worked example — minimal synth via tools

Start from an empty or placeholder circuit, then build up step by step.

```
# 1. Replace with a skeleton
set_turtle  →  (paste the Turtle below)

@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:new#> .
:main a val:Circuit ; val:element :out ; val:arc () .
:out  a val:Output .

# 2. Add nodes
add_node  {"id": "urn:valis:new#pitch", "class": "val:MidiPitch"}
add_node  {"id": "urn:valis:new#osc",   "class": "val:Oscillator"}
add_node  {"id": "urn:valis:new#env",   "class": "val:Envelope"}
add_node  {"id": "urn:valis:new#vca",   "class": "val:VCA"}

# 3. Wire audio
connect  {"from_node": "urn:valis:new#osc", "from_port": "out",
           "to_node":   "urn:valis:new#vca", "to_port":   "in",  "control": false}
connect  {"from_node": "urn:valis:new#vca", "from_port": "out",
           "to_node":   "urn:valis:new#out", "to_port":   "in",  "control": false}

# 4. Wire control
connect  {"from_node": "urn:valis:new#pitch", "from_port": "out",
           "to_node":   "urn:valis:new#osc",   "to_port":   "frequency", "control": true}
connect  {"from_node": "urn:valis:new#env",   "from_port": "out",
           "to_node":   "urn:valis:new#vca",   "to_port":   "cv",        "control": true}

# 5. Check
get_diagnostics
```

## Common mistakes

**Forgetting `val:Arc` vs `val:ControlArc`.**  
Audio ports (`in`, `out`) need audio arcs (`control: false`). Control ports
(`frequency`, `cv`, `cutoff`) need control arcs (`control: true`). Mixing them
causes a compile error naming the mismatched port.

**Setting a fixed value on a port that a control arc already targets.**  
The arc wins every block. Move the resting value into the control path.

**Reusing a parameter slot.**  
Each slot (0–63) may only be bound once. Check `list_params` before adding a
`val:Param`.

**Targeting a port symbol that does not exist.**  
Port symbols are exact. `"cutoff"` is not `"Cutoff"`. Check `list_element_types`
if a connect call fails with "no port" error.

**Creating a feedback loop without `val:UnitDelay`.**  
A cycle in the audio graph is an error unless every cycle passes through a
`val:UnitDelay` to break it.
