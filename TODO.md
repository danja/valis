## Misc

* support Claude and OpenAI API endpoints

## New instruments

* look at decomposing the granular element into smaller, reusable components
* create a synth that makes jawdropping sounds nobody has heard before 
* Model the Oberheim DMX drum sounds as faithfully as possible, creating new elements as needed.
* **cello: done for C2 to D4, open above it.** `val:Bow` is a bowed-string
  waveguide with a stick-slip friction curve, a soft stopped end, bow width and
  rosin irregularity; `examples/cello.ttl` puts a body behind it. In tune to
  0.3 cents and harmonically full from 65 to 294 Hz, measured in
  `tests/dsp/CelloTest.cpp`. Above about D4 the odd harmonics fall away until
  the fundamental is more than 26 dB below the second partial, so the tone
  reads as an octave ambiguity rather than a note. Likely the fixed poles of the
  loop filters becoming a large fraction of a short loop; the next step is to
  scale them with frequency rather than leaving them constant.
* the bow locks into a neighbouring mode at isolated combinations of pitch and
  bow position (a band just above position 0.125 at 165 Hz, for one). The
  default is clear of the bands found so far. Real strings do this when bow
  force is wrong for the position, which the Schelleng diagram describes, so the
  fix is probably to scale bow force with position rather than to suppress it.
* make an accurate simulation of the Boss DD-3 delay pedal

## From the plugin survey

`reference/survey-1.md` is a 60-case architectural survey of native plugin source,
written for a different project (LeanDSP/JigDAW) but asking the question Valis also
has to answer: what is the smallest bounded computational model that represents real
plugin architecture without hiding the interesting parts. The items below are the
findings that translate into work here, ordered by leverage.

Done so far:

* reusable subcircuits (`val:Subcircuit`, expanded by the model, with collapse
  and expand in the Circuit view; see `examples/subcircuit.ttl`);
* sample-accurate note events (the engine cuts a control slice at each event's
  position, so a note starts on the sample the host sent it on);
* resource identity for sound files (`val:sha256`, `val:channels`,
  `val:sampleRate`, `val:frames`, checked on load);
* stereo (`val:Input` gained "left" and "right" to mirror `val:Output`, and the
  processor no longer sums the host's channels to mono; elements stay mono and a
  stereo patch is two chains);
* port units choosing readout precision (`include/valis/ValueFormat.h`);
* the block domain (`val:SpectralGate`, an STFT with overlap-add resynthesis and
  declared latency; `src/dsp/elements/Spectral.cpp` is the pattern to copy);
* per-element oversampling (`val:oversampling`, an engine-applied wrapper around
  any element; `src/dsp/Oversampled.h`);
* event output ports (`atom:AtomPort` carrying `midi:MidiEvent`, collected per
  block by the engine and sent to the host; `val:NoteOut` is the element);
* the bounded voice pool (`val:voices` on a subcircuit instance, with
  deterministic allocation and stealing in the engine; see
  `examples/polysynth.ttl`);
* the seeded randomness fix in `val:SignalGenerator`.

What the survey asks for and Valis already has, so nobody re-derives it: delay gives
graph cycles their semantics (`val:UnitDelay` plus the compiler's `cycleAdj`/`orderAdj`
split); a checked opaque-component boundary (`DspElement` plus `val:implementation`);
resource loading off the real-time thread (`val:SampleLoad`); a distinct control rate
(the engine's 32-sample slice); and declared latency aggregated per circuit and
reported to the host.

* **Oversampling as a region rather than per element.** Done per element:
  `val:oversampling 2/4/8/16` wraps any element and runs it faster, measured at
  185x less alias energy through a hard-driven tanh at 8x
  (`src/dsp/Oversampled.h`). What the survey actually argues for is a *region*:
  survey cases 7 to 12 put a whole nonlinear section inside one oversampled
  domain, resampling once at each end rather than once per element. That needs
  the compiler to keep a region rather than flattening it, and the engine to run
  a node range with N times the buffers. Worth doing only if the per-element form
  turns out to cost too much in a real pedal chain.

## Recurring - check periodically

* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
