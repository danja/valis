# Skream — the Valis proof of concept

## Why this one

[`/home/danny/github/downspout/docs/skream-plan.md`](/home/danny/github/downspout/docs/skream-plan.md)
proposes a bespoke plugin: port the DSP from [Scream](https://github.com/danja/Scream),
approximate its GUI in DPF, ship ten presets. That is a reasonable plugin and a poor
proof of anything.

The better version is the same sound built as a **Valis circuit** — a Turtle file the
user can open, read, re-patch and hand to an LLM. It is the right proof of concept
because the scream filter is small enough to write in forty lines of Turtle and
demanding enough to exercise the parts of Valis that are easy to get wrong:

- a **nonlinearity inside a feedback loop**, which is the hard case for a block-level
  engine and the thing that forces `val:UnitDelay` to be real rather than decorative;
- a **filter with several simultaneous outputs**, which forces ports to be named rather
  than assumed;
- **antiderivative anti-aliasing**, which is a better answer than oversampling for a
  memoryless nonlinearity and which the ontology should therefore be able to express;
- a **gate driven by a signal from elsewhere in the circuit**, which forces control-rate
  arcs to be first-class.

If Valis can express Scream faithfully, it can express most of what a virtual-analog
effect does.

## What Scream actually is

Not a synth. Scream is a distortion effect that recreates the filter from Massive that
made the 2010-era growl, and it is built on published work rather than guesswork:
Zavalishin's *The Art of VA Filter Design* ch. 6 for the nonlinearities, Andy Simper's
[`SvfLinearTrapOptimised2`](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf)
for the filter, Jatin Chowdhury's second-order ADAA for the saturator, and
Giannoulis/Massberg/Reiss for the compressor.

Its inner loop (`src/plugin.c`, around line 600) is:

```
  in ──[in gain]──▶(+)──[tanh ADAA2]──[SVF lowpass]──┬──[dry/wet]──▶ out
                    ▲                                │
                    │                                ▼
                    └──[z⁻¹]──[gate]◀──[tanh ADAA2]──[SVF highpass]──[fb gain]
                                 ▲
                                 └── envelope of the input
```

The feedback path is what screams: the loop gain, highpass corner and second saturator
set the resonant howl, and the gate — driven by an envelope follower on the *input*,
not on the loop — stops it self-oscillating when nothing is playing.

Two details worth keeping. The `Resonance` control is not the filter's Q alone; in the
original it raises the internal distortion drive and the Q together, which is why the
sound gets dirtier as it gets more resonant. And Scream deliberately drops the original's
keytracking, because an effect cannot know the pitch of its input.

## The same thing in Valis

```turtle
:screamLoop a val:Mixer .

:sat1 a val:Tanh ; val:antialiasing val:ADAA2 .
:lp   a val:StateVariable ; val:cutoff 800.0 ; val:resonance 4.0 .
:hp   a val:StateVariable ; val:cutoff 300.0 ; val:resonance 2.0 .
:sat2 a val:Tanh ; val:antialiasing val:ADAA2 .
:fb   a val:Gain ; val:gain -6.0 .
:z    a val:UnitDelay .
:gate a val:Expander ; val:threshold -120.0 ; val:ratio 2.0 .

# forward path
:a1 a val:Arc ; val:from [ val:node :in    ; val:port "out" ] ;
                val:to   [ val:node :screamLoop ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :screamLoop ; val:port "out" ] ;
                val:to   [ val:node :sat1  ; val:port "in" ] .
:a3 a val:Arc ; val:from [ val:node :sat1  ; val:port "out" ] ;
                val:to   [ val:node :lp    ; val:port "in" ] .

# feedback path, broken by the unit delay
:a7 a val:Arc ; val:from [ val:node :z     ; val:port "out" ] ;
                val:to   [ val:node :screamLoop ; val:port "in" ] .

# the gate is opened by an envelope of the input, not of the loop
:m1 a val:Arc ; val:from [ val:node :env   ; val:port "out" ] ;
                val:to   [ val:node :gate  ; val:port "amount" ] ;
                val:depth 1.0 .
```

The full circuit is [`examples/skream.ttl`](../examples/skream.ttl).

## Why this beats a bespoke plugin

**The commented-out lines become the interface.** Scream's inner loop carries five
abandoned saturators in comments — `sinarctan`, `sinarctan2`, `softsine`, `softsine2`,
a plain clamp. Choosing between them meant editing C and rebuilding. In Valis it is one
word in a Turtle file, live, with the audio running.

**Presets can differ in topology, not just in values.** A preset system stores parameter
values against a fixed graph. A Valis preset is the graph, so one preset can put the
saturator before the filter and another after it, or add a second feedback path, or
remove the gate. The ten presets the brief asks for become ten `.ttl` files, and none of
them are limited to the knobs the author thought of.

**An LLM can design the sound.** Every operation is an `Op` reachable over HTTP MCP, so
"make the growl more vocal, keep the low end" is a sequence of graph edits against a
described vocabulary rather than a request to nudge opaque sliders. That is the claim
in the project's name, and this is the circuit that tests it.

**It is inspectable.** The graph view shows the feedback loop, the unit delay that breaks
it, and the gate's control arc — the actual signal flow, not a faceplate.

## What it demands of Valis

Gaps between this circuit and the ontology as it stands:

- [ ] **Control-rate arcs.** An arc must be able to end on an `lv2:ControlPort`, carrying
      one value per block, with modulation depth as a property of the arc
      (`val:depth`) rather than of either endpoint. Without this there is no modulation
      and Valis is a static chain.
- [ ] **`val:antialiasing`** on `val:Transfer`, with `val:ADAA1`, `val:ADAA2`,
      `val:Oversample2x`, `val:Oversample4x` and `val:None`. ADAA2 tanh needs `double`
      internally — single precision overflows the second antiderivative.
- [ ] **Named multi-output filters.** `val:StateVariable` should expose `lp`, `bp` and
      `hp` output ports rather than a numeric `mode`, matching the Cytomic
      mixing-coefficient form it implements.
- [ ] **`val:Expander`** — hard-knee expander/gate with attack and release.
- [ ] **`val:EnvelopeFollower`** — peak and RMS detection, the control source for the gate.
- [ ] **`val:Compressor`** — Giannoulis/Massberg/Reiss, soft knee, upward and downward,
      for the OTT-style stage the brief asks for after the filter.
- [ ] **More saturators** — `val:SinArcTan` (`x/√(x²+1)`) and `val:SoftSine` (`x/(|x|+1)`),
      both straight from Scream and both cheap.
- [ ] **Feedback validation.** The compiler must accept a cycle that passes through a
      `val:UnitDelay` and reject one that does not, naming the offending arc.

## Acceptance

Load `examples/skream.ttl` in the standalone, feed it a bass line, and hear the growl.
Change `val:antialiasing` from `val:ADAA2` to `val:None` in the Turtle view and hear the
aliasing appear. Open the graph view and see the feedback loop. Raise the feedback gain
over MCP and hear it approach self-oscillation, with the gate holding it silent between
notes.

## Licensing note

Scream is a reference for *technique*, not a source of code to copy. The techniques it
uses are published and independently implementable: the Cytomic SVF paper, Zavalishin's
book, the Giannoulis compressor paper, and Chowdhury's ADAA (BSD-3). Check
`/home/danny/github/Scream/LICENSE` before lifting anything verbatim.
