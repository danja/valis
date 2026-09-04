# Valis : Virtual Analog LLM Integrated System

Valis is a standalone app and a DAW plugin for building virtual analog circuits. Free, open source. Built on JUCE so supports a variety of systems, but currently only tested on Linux.

It can be viewed as a factory for instruments and effects processors. Existing examples include :

* SH-101 style monosynth
* TR-909 drum voices
* Klon Centaur guitar overdrive
* Massive synth "scream" filter
* Clarinet voice (digital waveguide model)

These are all simplified to some extent, but good enough to be fun, imho.

Valis is at an alpha stage of development, I'm still testing/working through bugs, builds are still rather hit & miss. But I wanted to post about it now to ask for suggestions for things to try, any other thoughts..?

https://danja.github.io/valis/

The circuits are built from compiled *elements*, functional blocks at around the same level of abstraction as synth modules. Circuit topologies are defined in RDF/Turtle. The same circuit is presented through three views : a regular plugin view **Controls** with knobs etc, a node-and-arc network of components **Circuit** and a syntax-highlighted Turtle editor **Code**.

Functionality is also supported through MCP, so an LLM can drive every operation the UI can. You can ask AI to design and build circuits. The core of this system is the Valis ontology https://github.com/danja/valis/blob/main/vocabs/valis.ttl 

A short demo video from a few days ago (much has changed): https://www.youtube.com/watch?v=iwrzSXKTRcY

