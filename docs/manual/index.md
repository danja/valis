# Valis

Valis is a DAW plugin that builds virtual-analog circuits from RDF/Turtle
descriptions. The circuit is not compiled into the plugin: it is a document you
can read, edit and hand to a language model.

[Video Demo](https://www.youtube.com/watch?v=iwrzSXKTRcY)

<div class="screenshots">
  <a href="images/controls.jpeg"><img src="images/controls-thumb.jpeg" alt="Controls view"></a>
  <a href="images/circuit.jpeg"><img src="images/circuit-thumb.jpeg" alt="Circuit view"></a>
  <a href="images/code.jpeg"><img src="images/code-thumb.jpeg" alt="Code view"></a>
</div>

- [Writing circuits](circuits.md) : the Turtle format, elements and arcs
- [Element reference](elements.md) : every class the ontology declares
- [The MCP surface](mcp.md) : driving the plugin over HTTP
- [Building](building.md) : prerequisites and the build

### Case studies

- [SH-101 subtractive synth](sh101.md) : monophonic VCO → VCF → VCA with MIDI and LFO
- [Klon Centaur](klon.md) : asymmetric diode overdrive with clean blend
- [Mutable Instruments Rings](rings.md) : Karplus-Strong plucked string, and how to create a new element
- [TR-909 drum synthesiser](909.md) : per-note drum routing, bridged-T resonators, and sectioned controls

### See Also

- [Downspout](https://danja.github.io/downspout/) : VST3 plugins
- [Transmission](https://danja.github.io/transmission/) : Generative Audio Workstation
- [Flues](https://github.com/danja/flues) : earlier LV2 plugins and Web Audio toys
- [danny.ayers.name](https://danny.ayers.name) : blog

## Why RDF

An audio plugin normally hides its structure. Its topology is compiled in, its
parameters are whatever the author decided to expose, and a preset is a list of
numbers against a fixed graph.

Valis inverts that. The circuit is a graph in a standard format, described with
a published vocabulary that reuses LV2's own terms for ports, ranges and units.
That makes three things possible that a bespoke plugin cannot do:

- **A preset can change the topology**, not just the values. Two presets can
  route the signal differently, or use different devices entirely.
- **The structure is inspectable.** The graph view draws the actual signal flow
  because it reads the same document the engine runs.
- **A language model can design the sound**, because every operation the
  interface can perform is reachable over HTTP MCP against a described
  vocabulary.

## A circuit

```turtle
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:basic#> .

:basic a val:Circuit ;
    val:element :in , :vcf , :drive , :out ;
    val:arc :a1 , :a2 , :a3 .

:in    a val:Input .
:vcf   a val:Ladder ; val:cutoff 800.0 ; val:resonance 0.4 .
:drive a val:DiodePair ; val:seriesResistance 2200.0 .
:out   a val:Output .

:a1 a val:Arc ; val:from [ val:node :in    ; val:port "out" ] ;
                val:to   [ val:node :vcf   ; val:port "in"  ] .
```

The proof of concept is [Skream](../skream.md): the Massive "scream" filter,
expressed as a circuit rather than as ported DSP.
