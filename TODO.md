## Misc

* review pre-existing examples to see if they can be improved with the new features - subcircuits etc
  (profiles are done: every shipped instrument and effect now declares a trn:PluginProfile)
* create emulations of all the instruments used by New Order to make "Blue Monday"
* create docs/examples.md - describe the existing instruments and the new ones below there (and clear the text below)

## New instruments

* **granular decomposed.** `src/dsp/Random.h` is now the one deterministic
  generator (it replaced four separate copies), `src/dsp/GrainPool.h` is the
  bounded grain population with its window and placement, and
  `src/dsp/SampleFile.h` is the file reader and its resource checks. The
  granulator is assembled from those rather than containing them. A grain pool
  cannot yet be patched between elements, because there is no port that carries
  a reference to a buffer; that is what it would take to split it further.
* **chimera: a synth built on coupled bowed strings.** `examples/chimera.ttl`.
  Each voice is two val:Bow instances shaking each other's bow arm through a
  unit delay, so they lock, beat or refuse to settle depending on the tuning
  ratio and the coupling. `val:Bow` gained a `drive` audio input for it.
* **DMX done.** `examples/dmx.ttl` is the seventeen-voice kit on General MIDI
  channel 10, playing the captures in `samples/Oberheim DMX`. The DMX was a
  sample player, so modelling it faithfully means playing the samples rather
  than synthesising an imitation; tuning is a playback rate, as it was on the
  machine. The numbered samples are alternative sounds, not duplicates: see the
  README beside them. Level, pan and tune are per voice card, which is where the
  machine put them.
* the DMX's voice cards are monophonic: one sound at a time, so a closed hi-hat
  cuts an open one. Here the sounds on a card sum instead. val:Choke gates a
  control signal and cannot stop a sample already playing, so this needs either
  a stop on val:SampleLoad or a ducking VCA per card.
* **val:option done.** A val:Subcircuit exposes an inner element's option under
  a name an instance can set, the way lv2:port exposes an inner port. The
  question of what happens when two inner elements recognise the same key does
  not arise: the ontology does not declare which option keys an element accepts
  and setOption answers true for keys it does not know, so nothing can tell.
  The definition names its targets instead, and naming two is a fan-out the
  author asked for. `examples/dmx.ttl` is one voice definition stamped out
  seventeen times because of it.
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
