## Misc

* support Claude and OpenAI API endpoints

## New instruments

* create a synth that makes jawdropping sounds nobody has heard before 
* Model the Oberheim DMX drum sounds as faithfully as possible, creating new elements as needed.
* create a realistic cello
* look at decomposing the granular element into smaller, reusable components

## From the plugin survey

`reference/survey-1.md` is a 60-case architectural survey of native plugin source,
written for a different project (LeanDSP/JigDAW) but asking the question Valis also
has to answer: what is the smallest bounded computational model that represents real
plugin architecture without hiding the interesting parts. The items below are the
findings that translate into work here, ordered by leverage.

What the survey asks for and Valis already has, so nobody re-derives it: delay gives
graph cycles their semantics (`val:UnitDelay` plus the compiler's `cycleAdj`/`orderAdj`
split); a checked opaque-component boundary (`DspElement` plus `val:implementation`);
resource loading off the real-time thread (`val:SampleLoad`); a distinct control rate
(the engine's 32-sample slice); and declared latency aggregated per circuit and
reported to the host.

* **Reusable subcircuits.** The survey's strongest finding is that a node and a graph
  should be the same abstraction at different zoom levels, because real DSP source is
  processors containing processors. Valis has one flat element list per circuit. The
  cost shows in `examples/909.ttl`: 1088 lines and 107 arcs, because three toms with
  identical structure and a hi-hat made of six oscillators are all hand-expanded.
  Work: let a `val:Circuit` declare its own ports and be instantiated as an element,
  flatten instances in the compiler so the engine and the real-time rules are
  unchanged, and give the Circuit view collapse and expand. Pick a name other than
  `val:Network`, which is already declared as the seam for nodal-analysis subcircuits.
  This is a milestone in `docs/plan.md`, not a single change.

* **Sample-accurate note events.** Every MIDI event is currently flattened to the start
  of the block (`src/plugin/ValisProcessor.cpp:274`, where the comment already calls
  this a later refinement). Queue events with their sample offset and deliver each one
  in the control slice that contains it. This is the same rule CLAUDE.md states for
  everything else that happens at a point in time: located by stream position, fired in
  the block that contains it.

* **Seeded randomness in `val:SignalGenerator`.** Its white-noise shape uses a
  function-local `static uint32_t seed` (`src/dsp/elements/Utility.cpp:395`), so the
  state is shared between every instance in every circuit and is never cleared by
  `reset()`. A render is therefore not reproducible. Make it per-instance state seeded
  in `reset()`, as `val:Granulator` and the waveguide turbulence already do. Small, and
  independent of everything else on this list.

* **Bounded voice pool.** The survey argues synth voices, sampler voices and grains are
  one computational pattern: a fixed pool with deterministic allocation, release and
  stealing, which stays provable because nothing is heap allocated. Valis is
  monophonic: `ProcessArgs` carries one gate, one velocity, one note number. A pool of
  preallocated subcircuit instances is the polyphony answer. Depends on subcircuits and
  on sample-accurate events, so it comes after both.

* **Block and window kernels.** The survey's second execution domain covers FFT, STFT,
  convolution, reverse windows and anything with explicit latency, and argues it should
  not be disguised as thousands of scalar nodes. Valis has `val:FreqAnalyzer` as a
  monitor only; no element transforms in the spectral domain. Work: a block kernel with
  window size, hop and declared latency on the element type, with the compiler
  accounting for the latency. Unlocks phase vocoder pitch shift, spectral freeze and
  convolution reverb.

* **Resource identity for sound files.** `val:SampleLoad` names a path. The survey wants
  a resource to carry identity, integrity and metadata: content hash, channel count,
  sample rate, frame count. That makes a circuit reproducible and turns a missing or
  changed file into a located error rather than silence.

* **Decide the stereo story.** 26 of the 60 cases have explicit multi-channel structure.
  `ValisProcessor` sums the input to mono and fans the output out
  (`src/plugin/ValisProcessor.cpp:325`). Two credible answers: a channel count on audio
  ports, or keep elements mono and make stereo explicit in the patch. Decide before
  writing anything, because it touches every element.

* **Oversampling as a domain rather than a per-element flag.** `val:antialiasing` selects
  antiderivative antialiasing on a single element. Survey cases 7 to 12 (the pedal
  models) instead put a whole nonlinear section inside an oversampled region. Consider a
  subcircuit property that runs everything inside it at a multiple of the sample rate.
  Depends on subcircuits.

* **Use the port unit for default display ranges.** Ports already carry `units:symbol`.
  Check whether `val:Oscilloscope` and `val:FreqAnalyzer` scale from rolling min and max,
  and if so let a declared unit imply a sensible default range instead. Cheap, and it is
  the practical half of the survey's argument that a value should carry semantic meaning
  and not just a number.

* **Event output ports.** Survey cases 42, 43 and 57 are plugins whose output is analysis
  or events rather than audio. Valis has control outputs already. An audio-to-MIDI
  element such as a pitch tracker would need an event output, which the model has no
  notion of. Worth scoping only once timed events exist on the input side.

## Recurring - check periodically

* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
