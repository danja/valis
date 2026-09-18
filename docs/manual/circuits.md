# Writing circuits

A circuit is a `val:Circuit` that names its elements and its arcs. Everything
else follows from the ontology.

## Elements

An element is an instance of a class the ontology declares. Setting a property
whose name matches a control port overrides that port's default:

```turtle
:vcf a val:Ladder ;
     val:cutoff 800.0 ;      # a control port of val:Ladder
     val:resonance 0.4 .
```

`val:cutoff` and the port symbol `cutoff` are the same thing. A property the
class does not declare is ignored; a class that does not exist, or that is
abstract, is an error naming the element.

## Arcs

Topology is explicit. Valis does not use an `rdf:List` pipeline, because a list
cannot express a graph:

```turtle
:a1 a val:Arc ;
    val:from [ val:node :osc ; val:port "out" ] ;
    val:to   [ val:node :vcf ; val:port "in"  ] .
```

An arc runs from an output port to an input port, and both ends must be the
same rate - audio to audio, control to control.

## Modulation

An arc ending on a control port carries modulation. Depth belongs to the arc,
not to either end, so one source can drive two destinations by different
amounts:

```turtle
:m1 a val:Arc ;
    val:from  [ val:node :lfo ; val:port "out"    ] ;
    val:to    [ val:node :vcf ; val:port "cutoff" ] ;
    val:depth 0.6 .
```

Control values update on a fixed 32-sample grid aligned to stream position, so
a circuit sounds the same whatever buffer size the host chooses.

## Feedback

A cycle must pass through a `val:UnitDelay`, which reads the previous sample.
Anything else has no latency in the loop and cannot be evaluated, so the
compiler rejects it and prints the path round the loop:

```
feedback loop with no val:UnitDelay to break it: loop -> sat1 -> svf
```

## Summing

Two arcs arriving at the same audio input is an error unless the destination is
a `val:Mixer`. Only a mixer is documented to sum, so a second wire onto an
occupied input fails visibly rather than quietly changing the sound.

## Parameters

The plugin's parameter list is fixed at 64 slots, because VST3, LV2 and CLAP all
require a static list. A `val:Param` binds a slot to an element property, and
supplies the name the host displays:

```turtle
:p0 a val:Param ; val:slot 0 ; val:target :vcf ; val:property val:cutoff ;
    lv2:name "Cutoff" ; lv2:symbol "cutoff" ; units:unit units:hz .
```

The range and unit come from the element's port declaration, so the host's knob
covers exactly the range the element accepts.

### What the Controls view draws

The port also decides the shape of the control, so a choice never appears as a
dial:

| The port declares | The panel shows |
|---|---|
| `lv2:portProperty lv2:toggled`, or an enumeration with two scale points | a two-position switch, labelled with the scale points |
| an enumeration with more scale points | a strip with every option named and one lit |
| anything else | a dial, with the value in the property's own units |

So a binary port is declared `lv2:toggled` with a scale point per position:

```turtle
[ a lv2:InputPort, lv2:ControlPort ; lv2:symbol "loop" ;
  lv2:name "Loop" ; lv2:default 1.0 ;
  lv2:minimum 0.0 ; lv2:maximum 1.0 ;
  lv2:portProperty lv2:toggled ;
  lv2:scalePoint [ rdfs:label "One shot" ; rdf:value 0 ] ,
                 [ rdfs:label "Loop"     ; rdf:value 1 ] ]
```

A switch that has to supply some other pair of values, or reach more than one
destination, is a `val:Select`: its `select` port is the switch, `a` and `b` are
the two values it chooses between, and `thru` repeats the switch position for
anything else that needs it.

A `val:SampleLoad` element draws a file slot with a Load button beside the
monitor boxes, so the sound file it plays can be changed without editing the
document.

## Subcircuits

A `val:Subcircuit` is a circuit fragment with declared ports that is
instantiated as if it were an element. It is written once and stamped out as
many times as the circuit needs, which is how one definition serves both sides
of a stereo pair, three identical toms, or six detuned oscillators.

Each `lv2:port` names the inner port it stands for, with `val:node` and
`val:port`:

```turtle
:Channel a val:Subcircuit ;
    lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:symbol "in" ;
               val:node :vcf ; val:port "in" ] ,
             [ a lv2:OutputPort , lv2:AudioPort ; lv2:symbol "out" ;
               val:node :drive ; val:port "out" ] ,
             [ a lv2:InputPort , lv2:ControlPort ; lv2:symbol "cutoff" ;
               lv2:default 900.0 ; lv2:minimum 20.0 ; lv2:maximum 12000.0 ;
               units:unit units:hz ;
               val:node :vcf ; val:port "cutoff" ] ;
    val:element :vcf , :drive ;
    val:arc :toDrive .

:vcf   a val:Ladder .
:drive a val:DiodePair .
:toDrive a val:Arc ; val:from [ val:node :vcf   ; val:port "out" ] ;
                     val:to   [ val:node :drive ; val:port "in"  ] .
```

An instance is an element whose class is the definition. It may set any control
port the definition exposes, exactly as on any other element:

```turtle
:left  a :Channel ; val:cutoff 700.0 .
:right a :Channel ; val:cutoff 1300.0 .
```

Arcs and `val:Param` bindings address an instance by its exposed ports, and the
model redirects them to the element behind each one.

A port that declares `lv2:default` sets that value on the element behind it, in
place of whatever that element declared for itself: the port is the face the
subcircuit presents. A port with no `lv2:default` leaves the inner value alone.
An instance's own value sits on top of either, the way a turned knob sits on top
of a declared value.

Definitions live in the same document as the circuit that uses them. They may
nest, and a definition that ends up instantiating itself is reported rather than
recursing.

### What expansion does

The model expands every instance before the circuit is compiled, so the compiler
and the engine never learn that subcircuits exist and the real-time rules are
untouched. Each inner element is renamed by prefixing it with the instance it
belongs to, so two instances never collide:

```
:left  a :Channel  ->  :left/vcf   :left/drive
:right a :Channel  ->  :right/vcf  :right/drive
```

Those are the names that appear in diagnostics and in the Circuit view. The
Circuit view can draw an instance closed, as a single box carrying only the
ports arcs actually cross it on; right-click a node to close or open the
instance it belongs to. Closing is a view preference and changes nothing the
compiler reads.

`examples/subcircuit.ttl` is a worked example.

### Voices

`val:voices` on an instance stamps the definition out that many times as a pool,
between 1 and 16:

```turtle
:poly a :Voice ; val:voices 8 .
```

The elements inside are the ordinary monophonic ones. What makes the pool
polyphonic is that the engine gives each copy its own note: a note on is
allocated to a voice, and every element in that copy sees that note's gate,
velocity and number rather than the circuit's. A `val:MidiPitch` inside the
definition therefore reports a different frequency in each voice.

Allocation is deterministic, so a polyphonic circuit renders the same way every
time: a voice that has never sounded first, then the one whose note was released
longest ago, then the oldest still held. A released voice is not reused at once,
because whatever is in it still has a release tail to finish; with more notes
than voices the pool steals rather than growing.

An input port of the instance reaches every voice. An output port is summed by a
`val:Mixer` the expansion adds, so everything downstream sees one signal and the
rule that only `val:Mixer` sums its inputs still holds. A control output cannot
be exposed from a pool: there is one value per voice and no honest way to choose
between them, so it is reported rather than silently taking the first.

A `val:Param` bound to a port of a polyphonic instance drives every voice
together, or turning the knob would let them drift apart.

`examples/polysynth.ttl` is a worked example: one voice definition, eight
copies, summed into a saturator.

## Stereo

Elements are mono. Stereo is built in the patch rather than by widening a port:
`val:Input` offers `left` and `right` alongside `out`, `val:Output` takes the
same three, and a stereo circuit is two chains between them. A subcircuit
instantiated once per channel is the tidy way to write that.

`out` on `val:Input` carries the host's channels averaged, so a mono circuit
needs to know nothing about how many channels arrived. `val:Output` falls back
to `in` for either side that is not wired.

## The block domain

Most elements work one sample at a time. Some algorithms cannot: a Fourier
transform has a window, a hop and a latency that a scalar recurrence does not.

Valis needs no separate machinery for these. An element is already an opaque
boundary with a real-time contract, so a block algorithm lives inside one. It
buffers what arrives, transforms every hop, overlap-adds the result, and
declares the latency that costs through `latencyInSamples()`. The engine sums
declared latency per circuit and reports it to the host, so the delay is
compensated rather than hidden.

`val:SpectralGate` is the worked example. It applies a gain to each frequency
bin, so it can take quiet broadband noise out from underneath a loud tone, which
no time-domain filter can do: the two occupy the same moments.

```turtle
:gate a val:SpectralGate ; val:threshold -40.0 ; val:fftSize 2048 .
```

`val:fftSize` is a power of two between 64 and 8192, and is also the latency in
samples. A larger window separates frequencies more finely and costs more delay.

`src/dsp/elements/Spectral.cpp` holds the transform and the pattern to copy for
a new spectral element.

## Event outputs

Most elements produce a signal. Some produce events instead: a pitch tracker, a
sequencer, anything whose output is a note rather than a waveform.

An event port is `atom:AtomPort` carrying `midi:MidiEvent`, which is LV2's own
spelling. It has no buffer and no control slot, so the compiler passes it by;
the engine collects what every element emits into one bounded list per block and
the plugin sends it to the host as MIDI.

`val:NoteOut` is the element that does this. The gate rising sends a note on,
the gate falling releases it:

```turtle
:note a val:NoteOut ; val:velocity 0.5 ; val:channel 3 .
```

The pitch is a frequency in Hz rounded to the nearest semitone, read once when
the note starts, so moving the control while the note is held does not retune a
note the host has already been told about. `val:MidiPitch` and
`val:Oscilloscope`'s frequency estimate both drive it directly, so an
audio-to-MIDI converter is a patch rather than a new element.

Whatever the circuit emits replaces the MIDI the plugin was given, rather than
being added to it. A circuit with no `val:NoteOut` sends nothing.

## Options

A `val:` property that is not a control port configures the element rather than
driving it. The class supplies a default; the instance may override it:

```turtle
:sat1 a val:Tanh ; val:antialiasing val:ADAA2 .   # the class default
:sat2 a val:Tanh ; val:antialiasing val:None .    # overridden here
```

`val:oversampling` runs one element at a multiple of the sample rate: 1, 2, 4, 8
or 16. A nonlinearity makes harmonics above the ones it was given, and any that
land above Nyquist fold back down as tones that were never in the signal.
Oversampling gives the element more room before that happens and filters the
result on the way back down.

```turtle
:sat a val:Tanh ; val:gain 8.0 ; val:oversampling 8 .
```

The engine wraps the element, so any element can be oversampled and one written
later needs no change to take part. Measured on a 5 kHz sine through a tanh
driven at 8x, running the element at 8x cuts alias energy 185 times. The
resampling filters are polyphase IIR, chosen for low latency rather than flat
phase, so a parallel dry path around an oversampled element will not sum
cleanly; the latency they add is reported to the host.

`val:antialiasing` answers the same problem the other way and costs far less, so
it is the first thing to reach for on a memoryless curve. Oversampling is for
where the element has memory and antiderivative anti-aliasing does not apply.

`val:antialiasing` chooses how a nonlinearity suppresses the harmonics it
creates above Nyquist: `val:None`, `val:ADAA1` or `val:ADAA2`. Antiderivative
anti-aliasing is cheaper and cleaner than oversampling for a memoryless curve -
measured on a 5 kHz sine driven at 8×, ADAA1 cuts alias energy 4.2× and ADAA2
cuts it 16.4×. ADAA2 costs one sample of latency, which the plugin reports to
the host.

### Sound files

`val:file` names a sound file for `val:SampleLoad` or `val:Granulator`. Relative
paths resolve against the working directory, then `examples/`, then the
repository root.

A file is a resource with an identity, not just a path, so a circuit may state
what it expects the file to be. Each of these is optional and checked on the
message thread when the file is read:

```turtle
:smp a val:SampleLoad ;
    val:file "samples/bell.wav" ;
    val:sha256 "694a657853b352b611868fa31227a3ded425efc094542c9f76ee77275dccf507" ;
    val:channels 1 ;
    val:sampleRate 32000 ;
    val:frames 83200 .
```

A file that is not what was declared fails the load with a message naming both
what was declared and what the file is, rather than playing something the
circuit was not written for. `val:sha256` is the
[SHA-256](https://en.wikipedia.org/wiki/SHA-2) of the bytes on disk, as 64
hexadecimal characters. Declaring nothing loads whatever the path resolves to.

## Editor metadata

`val:x` and `val:y` record where the graph view puts a node. They live apart
from execution metadata: moving a node never invalidates the compiled circuit,
so dragging one does not interrupt the audio.
