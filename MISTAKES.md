# Mistakes log

## Measured a rewritten element with a stale binary

**What happened:** the rewritten `val:Reed` measured perfectly in its unit test,
in tune to a few cents and stable for five seconds, and measured wrongly through
`valis-render`: 37 cents flat, drifting further down every half second, with the
harmonics collapsing into noise. Several rounds went into looking for a
difference between the two paths, including checking the element for block-size
dependence, which it did not have.

**Root cause:** `cmake --build build --target dsp_ClarinetTest` builds that test
and the library it needs, and does not build `valis_render`. The renders were
made by a binary linked against the previous `libvalis_core.a`, so they were
measuring the *old* element. The giveaway was in the output all along: every
render reported `peak 0.2500` whatever the pitch or the controls, and `* 0.25f`
was the old implementation's output scale. The new one has no such constant.

**Happened again**, on a model-layer feature this time: a `val:Subcircuit` could
expose an inner element's option, the unit tests passed, and `valis-render`
played silence. Two rounds went into comparing the model and the compiled
circuit between a working file and a broken one, both of which printed
identical options, control values and topology. They were identical. The test
binary had been rebuilt and the tool had not.

The second time cost more than the first, because the two paths agreeing on
everything printable is exactly what a stale binary looks like, and it reads as
impossible rather than as a clue. **When a test and a tool disagree about a
circuit whose compiled form is identical in both, suspect the binaries before
suspecting the code.**

The rule had been written down under "Physical models" in CLAUDE.md, which is
where it was learnt but not where it applies. It is now in the change workflow,
which is where it gets read.

**Prevention:** build everything before measuring through a tool.
`cmake --build build` with no target takes a few more seconds and removes the
whole class of problem. And a number that does not move when the inputs move is
evidence about which code is running, not a curiosity: chase it first.

---

## A flute that was structurally a stopped pipe

**What happened:** the first `val:Flute` was in tune to a few cents across three
octaves and stable everywhere, and sounded nothing like a flute. Its spectrum
was a strong fundamental with a strong third and fifth harmonic and almost no
second or fourth, which is the signature of a stopped pipe, a panpipe or a
clarinet. Sign conventions, jet ratios from 0.05 to 0.95, loop losses, filter
poles, radiating from the embouchure as well as the open end, and coupling
strengths were all tried and measured; none of them moved the balance.

**Root cause:** two things, both in the excitation rather than the resonator.

The jet was driven by a cubic with a constant bias, and the loop gain was high
enough that it spent most of the cycle clipped against the cubic's limits. A
symmetrically clipped signal is a square wave, and a square wave has no even
harmonics.

Underneath that, the jet path's delay was half the air column's, so its phase
alternated exactly with harmonic number: it reinforced the odd harmonics and
cancelled the even ones. That ratio was not a free choice, because it is also
the ratio at which the fundamental is most strongly reinforced, so the
instrument was most stable exactly where it sounded least like itself.

**Prevention:** model the excitation, not a curve that resembles it. The flow
into a real flute is `Uj * tanh((eta - y0) / b)`, and the two terms that were
missing are the two that mattered. `y0`, the jet's offset from the edge, is what
breaks the symmetry and puts the even harmonics in; it is now the val:offset
port, and it is what a player changes by rolling the instrument. A lowpass on
the jet's response to the acoustic field, tracking the played pitch, stops the
jet path reinforcing the third and fifth as hard as the fundamental, and is
there because a wide jet cannot follow a short wavelength.

Measured at A4 played softly, the second harmonic went from 0.02 of the
fundamental to 0.16 and the third from 0.32 to 0.11: the second now leads, which
is what a flute does. `tests/dsp/FluteTest.cpp` asserts that ordering, so a
change that returns it to a stopped pipe fails the build.

The general lesson is that a physical model that is stable and in tune can still
be modelling the wrong instrument, and only a spectrum measurement will say so.
Tuning and stability tests do not cover timbre.

---

## Granulator Freeze had two positions that could not differ, and a panel that could not be scrolled

**What happened:** In `examples/granular.ttl` the Freeze control "didn't appear
to make any difference", and the Mix knob in the Output section could not be
reached at all.

**Root cause:** two separate mistakes.

`val:Granulator` declared `freeze` as a three-way choice: -1 Auto, 0 Record,
1 Freeze, where Auto meant "keep material loaded by `val:file`, record
otherwise". The granular example loads its material through a `val:SampleLoad`
rather than the granulator's own `val:file`, so `fileLength` was 0 and **Auto
and Record were the same thing**. Two of the three positions were
indistinguishable by construction. Worse, selecting Freeze from a cold start
left the buffer empty for ever, so the circuit rendered silence.

The Controls panel is taller than the window and scrolls, but the pointer is
almost always over a dial, and `juce::Slider` consumes the scroll wheel. Trying
to scroll down nudged whatever dial was under the pointer instead of moving the
view, so everything below the fold was unreachable.

**Prevention:** a control's positions must be able to differ in the circuit that
exposes it. `freeze` is now a plain `lv2:toggled` Record/Freeze switch, and the
case the tri-state existed for is handled where the information actually is: the
engine points an unconnected input at its shared silence buffer, `ProcessArgs`
now carries that pointer, and the element records nothing when its input is not
connected. No mode, no ambiguity, and `val:file` still cannot be erased by a
connection that does not exist.

For the panel: a dial inside a scrolling view must not eat the wheel.
`PanelSlider` in `src/ui/ControlsView.cpp` scrolls by default and adjusts only
while Ctrl or Command is held. Rule promoted to CLAUDE.md.

---

## valis-render dropped every note not on a block boundary

**What happened:** `valis-render --note 60 --gate-on 0.02` rendered silence from
`clarinet.ttl`, `sh101.ttl` and `rings-modal.ttl`, while the same circuits made
sound under `tests/engine/ValisEngineTest.cpp`. The circuits were fine.

**Root cause:** the render loop compared the note time to the **start of each
block** for equality: `if (sample == noteOnSample)`. A note at 0.02 s is sample
960, which is not a multiple of the 512-sample block size, so the comparison was
never true and no note-on was ever sent. Only `--gate-on 0.0` worked, which is
why the bug survived: every existing use passed a time that happened to land on
sample 0.

This is the same class of error as the SignalGenerator impulse below, from the
opposite direction: there a per-sample event was gated on the block index, here
a stream-position event was compared against one.

**Prevention:** an event fires in the block that **contains** it:
`if (at >= blockStart && at < blockStart + n)`. Any comparison between a stream
position and a block boundary must be a range test, never equality. The
granular case study renders with `--gate-off`, so a regression shows up as a
silent example.

---

## `assert(side_effect())` silenced by NDEBUG in Release builds

**What happened:** `rdf_TurtleStoreTest` SEGFAULTed only in CI Release builds
(`cmake -DCMAKE_BUILD_TYPE=Release`). Debug builds passed locally.

**Root cause:** Six `parse`/`parseFile` calls were wrapped inside `assert()`:
`assert(store.parse(...))`. In Release mode CMake defines `NDEBUG`, which expands
`assert(expr)` to `((void)0)` — the expression is **never evaluated**. The parse
never ran, the store remained empty, and the immediately following
`circuits[0]` access on an empty vector caused the SEGFAULT.

The initial diagnosis was wrong (suspected serd struct layout difference between
0.30.x and 0.32.x). Removing `libserd-dev` from CI apt-installs had no effect
because that was not the cause.

**Prevention:** Never call a function with observable side effects inside
`assert()`. The correct pattern is:
```cpp
const bool ok = store.parse(...);
assert(ok);
```
This ensures the call happens in all build types. Any function whose return value
you need to assert on must be called before the assert. Rule promoted to CLAUDE.md.

---

## Bass drum VCA silenced by velocity arc through ControlMultiply

**What happened:** The bass drum in `examples/909.ttl` produced no audible output. The TwinTBridge oscillator was triggering and the Tanh stage was processing, but the VCA downstream was closed.

**Root cause:** The amp envelope output was routed through a `val:ControlMultiply` that multiplied it by the `NoteGate` velocity. The velocity goes to 0.0 on note-off. Because a control arc replaces its destination value each block, the VCA cv became `envelope × 0 = 0` the moment the MIDI note-off arrived — before the 500 ms TwinTBridge decay had a chance to sound.

A second flaw: `TwinTBridge` read `args.velocity` (the global last-note velocity) for initial amplitude at trigger time. In a polyphonic drum kit this picks up whichever note fired most recently, not necessarily the bass drum.

**Prevention:** For a drum voice, the VCA should be controlled by the amp envelope alone — the envelope provides the gate shape, and the oscillator's internal amplitude provides velocity sensitivity. When wiring a TwinTBridge voice, connect the amp envelope directly to the VCA cv and route the NoteGate velocity to the TwinTBridge velocity input port (added in this fix). Avoid ControlMultiply in the VCA cv path unless both inputs stay non-zero for the full decay duration. Rule promoted to CLAUDE.md.

---

## SignalGenerator impulse shape depended on block alignment

**What happened:** The `val:SignalGenerator` impulse shape (shape 5) only emitted
when a period wrapped exactly on the first sample of a process block
(`i == 0 && phase < inc`). Most periods produced nothing, and the output changed
with the host's block size — the same class of bug as the M4 control-rate fix,
where rendering at 128 vs 512 samples must be bit-identical.

**Root cause:** The per-sample phase accumulator already tracks period wraps
exactly (`phase < inc` fires once per period). Gating it on the block index
threw that away and reintroduced block-boundary dependence.

**Prevention:** Any per-sample condition in `process` must depend only on the
sample position within the stream (phase, counters carried in members), never on
the index within the current block. A test renders the impulse train over 1024
samples and asserts the exact impulse count and amplitude, so a regression fails
the build.
