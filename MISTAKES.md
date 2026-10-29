# Mistakes log

Short entries: what happened, why, and what prevents it. Rules that came out of
an entry live in `CLAUDE.md`, which is where they get read.

## Measuring with a stale binary

**What happened:** a rewritten `val:Reed` measured perfectly in its unit test and
wrongly through `valis-render`. Later a model-layer feature passed its tests and
`valis-render` played silence. Both times rounds went into comparing the two paths
before anyone suspected the binaries.

**Root cause:** `cmake --build build --target <test>` builds that test and the
library, not `valis_render`, so the tool was linked against the previous
`libvalis_core.a`. The giveaway the first time was a peak that never moved
whatever the inputs (`0.2500`, the old element's output scale).

**Prevention:** build everything (`cmake --build build`, no target) before
measuring through any tool. A number that does not move when the inputs move is
evidence about which code is running. When a test and a tool disagree about a
circuit whose compiled form prints identically in both, suspect the binaries
first. In `CLAUDE.md` under the change workflow.

## Events located by block, not by stream position

Happened twice, from opposite directions. Both are the same bug.

- **`val:SignalGenerator` impulse:** it fired only when a period wrapped on
  sample 0 of a block (`i == 0 && phase < inc`), so most periods produced
  nothing and the output changed with the host's block size. The phase
  accumulator already tracked wraps exactly; gating on the block index threw
  that away.
- **`valis-render` notes:** it compared the note time to the start of each block
  for equality, so `--gate-on 0.02` (sample 960, not a multiple of 512) never
  sent a note-on and the example rendered silent. Only `--gate-on 0.0` worked,
  which is why it survived.

**Prevention:** a per-sample condition depends on the stream position (phase,
counters held in members), never on the index within the block. A stream-position
event fires in the block that contains it, `at >= blockStart && at < blockStart + n`,
never by equality with a boundary. Tests render the impulse train and assert its
exact count. Rule in `CLAUDE.md` under real-time rules.

## A flute that was structurally a stopped pipe

**What happened:** the first `val:Flute` was in tune across three octaves and
stable, and sounded nothing like a flute. Its spectrum had strong odd harmonics
and almost no even ones, which is a stopped pipe or a clarinet. Many parameters
were tried; none moved the balance.

**Root cause:** the excitation, not the resonator. The jet was a cubic with a
constant bias driven hard enough to clip, and a symmetrically clipped signal is a
square wave with no even harmonics. The jet delay was also half the air column's,
so its phase reinforced odd harmonics and cancelled even ones.

**Prevention:** model the excitation, not a curve that resembles it. The flow is
`Uj * tanh((eta - y0) / b)`; `y0`, the jet's offset, breaks the symmetry and is now
`val:offset`. A lowpass on the jet's response, tracking pitch, stops it
reinforcing the third and fifth. `tests/dsp/FluteTest.cpp` asserts the harmonic
ordering. A model can be stable and in tune and still be the wrong instrument, and
only a spectrum measurement says so. In `CLAUDE.md` under physical models.

## Granulator Freeze: two positions that could not differ

**What happened:** the Freeze control "didn't appear to make any difference", and
the Mix knob below the fold of the Controls panel could not be reached.

**Root cause:** `freeze` was a three-way choice (Auto, Record, Freeze), and Auto
meant "keep material loaded by `val:file`". The example loads material through a
`val:SampleLoad`, so Auto and Record were the same thing by construction. Separately,
`juce::Slider` consumes the scroll wheel, so scrolling the panel nudged the dial
under the pointer instead.

**Prevention:** every position of a control must be able to differ in the circuit
that exposes it. `freeze` is now a plain `lv2:toggled` switch, and an unconnected
input is detected from `ProcessArgs::silence`. `PanelSlider` scrolls by default and
adjusts only with Ctrl or Command held. Both rules in `CLAUDE.md` under the
Controls view.

## Oxide survey elements: four slips caught by the first test run

All four were found by the new tests, or by the compiler, before anything else used the elements.

- **Redux hold length:** the counter was decremented before it was tested, so a
  hold of 4 lasted 3 samples. Test the counter first, then count down, and assert the
  hold length against a strictly rising input (`tests/dsp/ReduxTest.cpp`).
- **Reverb decay 3 to 4 dB short:** line lengths were fractional at size 1, and linear
  interpolation of a fractional delay is a lowpass, so high-frequency modes decayed
  faster than the time asked for. Base lengths are whole samples now. A delay that is
  read between samples in a feedback loop is also a filter; measure the decay with the
  damping off before blaming anything else.
- **Wavetable loudness:** each frame was scaled once, to peak at 1 at full bandwidth.
  A pulse-like frame then peaked at 0.12 at an ordinary pitch, where most harmonics are
  dropped. Every table is scaled on its own now. When a normalisation depends on a
  property of the data that the playback path changes, normalise after the change.
- **A summed input needs a `val:Mixer`:** the split-reverb example had the same arc
  written twice and the multiband example fed two arcs into one compressor. The
  compiler caught both with located errors; nothing to prevent beyond rendering each
  new example with `valis-render` before writing a test around it.

## Running only the nearest tests after changing a shipped example

**What happened:** adding a choke to each DMX voice card changed the number of
elements the circuit compiles to. The new tests and the DMX engine test passed;
`CircuitCompilerTest`, which counts the DMX nodes (`8 * 9 + 4`), was not run until
the full suite at the end, where it failed.

**Root cause:** the tests chosen were the ones for the code being written, and
a shipped example is read by tests that have nothing to do with the element that
changed. A test that pins a count of a shipped file's contents is exactly the kind
that no targeted run will include.

**Prevention:** after editing anything under `examples/`, run the whole suite, or
at least `grep -rl <example name> tests/` and run those. A count asserted on an
example is a reason to update the count when the example grows, not a sign the
example is wrong.

## Adding a mode port to an element whose test says it has none

**What happened:** to give the Grist feedback filter a lowpass, bandpass and highpass
choice, `val:StateVariable` got a `mode` port and a selected output. `OntologyTest`
asserts the element has no `mode` port: the Scream circuit taps `lp` and `hp` at the
same time, so the design is named outputs, not a switch. The full suite caught it.

**Root cause:** the change was made to the nearest element that had the three signals,
without reading the test that pins how that element is meant to be used.

**Prevention:** before adding a port to an existing element, grep `tests/` for the
element's name and read any assertion about its ports. A test that says why a port is
absent is a design decision, so put the feature in a new element
(`val:AudioSelect`) rather than reversing it.

## Modulation effects that moved once per block

**What happened:** `val:Chorus`, `val:Flanger` and `val:Phaser` read their LFO once per block and held
the delay, or the allpass pole, for the whole block. The delay stepped at every block boundary, which
a fast rate or a long block made audible, and the sound depended on how the host cut the stream. None
of the three put its LFO back to its starting phase on `reset()`, and `val:Noise` did not restart its
sequence. A conformance suite written over every element found all four in one run.

**Root cause:** the elements were written when the engine's block was the unit of time, and nothing
checked that a different cut of the same stream gave the same sound. It is the "events located by
block" mistake above in a different place: a continuous quantity sampled at the block rather than at the
sample.

**Prevention:** `tests/dsp/ElementConformanceTest.cpp` now holds every element to determinism, reset,
block-size independence, boundedness under random controls, no allocation in `process()` and decay.
A new element is checked without a line of test code, and an exception needs a reason written next to
it. A modulation source that runs inside an element advances per sample (or per fixed count of
samples carried across blocks), never per block.

## A context built twice, and only one of them complete

**What happened:** the `render` tool refused to run inside the plugin ("no ontology or registry") from the
day it was written. `ValisProcessor::ops()` built the context the ops run in and never set `ctx.registry`.
Every test passed, because every test builds its own context by hand and sets everything.

**Root cause:** the context is assembled in two places that share no code, and the tests' copy was the
complete one. A field an op needs but the plugin forgot is invisible to any test that does not use the
plugin's copy, and the plugin cannot be constructed headless.

**Prevention:** `tests/ops/ContextWiringTest.cpp` reads `OpContext` from `Ops.h` and the body of
`ValisProcessor::ops()` as text and fails if any field is not assigned. Adding a field to the context now
fails the build until the plugin sets it. The general lesson: where a thing that tests cannot construct is
assembled by hand in one place, test the assembly against the declaration of what it must contain.

## A rule applied in the views and not in the ops

**What happened:** a `val:Param` can give its own `lv2:minimum` and `lv2:maximum`, narrower than the port's.
The Controls view and the host parameter both used it. `list_params` and `set_param` did not: they reported
the port's range, so a caller was told a knob went from -60 to 24 dB when it went from 20 to 24, and
`set_param` clamped to the wrong range. Found when the stability check drew values from `list_params`'
ranges and they were outside the knob's.

**Root cause:** the same fact ("what range does this knob have") was worked out separately in the UI, in the
processor and in the ops, and the ops copy was written first and never updated when the override was added.
The ops were meant to be the single implementation the views sit on, but the views had grown their own.

**Prevention:** `testABindingsOwnRangeIsTheRangeTheOpsReport` holds the ops to the binding's range. Where a
view and an op both need a value, the view should ask the op; the next step is to make the Controls view and
the processor read their ranges from `listParams`.

## `valis-render --midi` tripped a JUCE assertion on a relative path

**What happened.** The first build of the `--midi` option constructed `juce::File` from the path
as given. A relative path such as `chord.mid` fires `JUCE Assertion failure in juce_File.cpp:219`
and the file is looked up against nothing in particular. The render still worked, which hid it.

**Root cause.** `-o` already resolves against the working directory, but the new input option was
written without looking at how the neighbouring one handled its path.

**Prevention.** Any user-supplied path in a tool goes through
`juce::File::getCurrentWorkingDirectory().getChildFile(...)`. Run the tool once with a relative
path and watch stderr for JUCE assertions, not only for the exit code.
