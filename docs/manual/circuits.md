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

## Options

A `val:` property that is not a control port configures the element rather than
driving it. The class supplies a default; the instance may override it:

```turtle
:sat1 a val:Tanh ; val:antialiasing val:ADAA2 .   # the class default
:sat2 a val:Tanh ; val:antialiasing val:None .    # overridden here
```

`val:antialiasing` chooses how a nonlinearity suppresses the harmonics it
creates above Nyquist: `val:None`, `val:ADAA1` or `val:ADAA2`. Antiderivative
anti-aliasing is cheaper and cleaner than oversampling for a memoryless curve -
measured on a 5 kHz sine driven at 8×, ADAA1 cuts alias energy 4.2× and ADAA2
cuts it 16.4×. ADAA2 costs one sample of latency, which the plugin reports to
the host.

## Editor metadata

`val:x` and `val:y` record where the graph view puts a node. They live apart
from execution metadata: moving a node never invalidates the compiled circuit,
so dragging one does not interrupt the audio.
