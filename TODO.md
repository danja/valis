The Options menu is still there in the standalone version. This should be removed and the items relocated to File and Settings.

Turtle and Knobs don't change when the graph changes from MCP.

Elements for midi in are needed, effectively CV in for pitch and velocity.

We need a SKILL that will aid in creating new elements.

Gain.gain is in dB (−60 to +24), while Envelope outputs 0–1. The arc 
  compiles and runs, but the VCA modulation depth will be 0–1 dB — effectively static.  
  There's no scaling element in the current ontology to remap the envelope range to a     
  useful dB window. A val:VCA element with a linear CV input, or a val:Scale utility,   
  would make this proper. 

Some basic DSP Elements would be useful: delay line, what else?

We need some case studies in the manual & examples :

* Klon Centaur - reproduce the guitar effect, if necessary by using the skill to create any new elements
* Subractive synth - bit like an SH-101
* Mutable Instruments Rings module

When a tag is pushed it should trigger a GitHub build of a Release including the standalone version plus all the plugin types.
