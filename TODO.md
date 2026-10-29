## Misc

* review pre-existing examples to see if they can be improved with the new features - subcircuits etc
  (profiles are done: every shipped instrument and effect now declares a trn:PluginProfile)
* create emulations of all the instruments used by New Order to make "Blue Monday". Reuse elements as appropriate, something new may be needed for the bass sound, plucked string subcomponents if they don't already exist

## New instruments

The instruments that are done are described in `docs/examples.md`. What is open:

* **FM.** Not yet modelled: keyboard level and rate scaling, a pitch envelope, and the
  DX7's 32 algorithms as named presets of `val:FMMatrix`.
* **val:NamModel.** Build with `-DVALIS_WITH_NAM=ON`; `examples/nam.ttl`. Still to do:
  check a declared `val:sha256` as `val:SampleLoad` does; resample around a model
  trained at a rate other than the engine's (it is refused now); optional use of the
  model's own input/output level and loudness; time a WaveNet at the engine's
  32-sample slice, since per-call overhead may dominate; a session override for the
  model file like the one for `val:SampleLoad`.
* **Grist follow-ups.** `examples/grist.ttl` has a lowpass only in its feedback path,
  where the plugin has lowpass, highpass and bandpass; a mode switch needs the
  state-variable filter's three outputs selected by a control. Density is grains per
  second here and grains per window in the plugin, which needs a divide that no element
  offers. `val:Quantise` takes the seven built-in scales and no custom set.
* **Granulator.** A grain pool cannot yet be patched between elements, because there is
  no port that carries a reference to a buffer; that is what it would take to split
  the granulator further.
* **DMX voice cards** are monophonic on the real machine: one sound at a time, so a
  closed hi-hat cuts an open one. Here the sounds on a card sum instead. `val:Choke`
  gates a control signal and cannot stop a sample already playing, so this needs
  either a stop on `val:SampleLoad` or a ducking VCA per card.
* **Cello above D4.** The odd harmonics fall away until the fundamental is more than
  26 dB below the second partial, so the tone reads as an octave ambiguity rather than
  a note. Likely the fixed poles of the loop filters becoming a large fraction of a
  short loop; the next step is to scale them with frequency.
* **Bow mode locking.** The bow locks into a neighbouring mode at isolated combinations
  of pitch and bow position (a band just above position 0.125 at 165 Hz, for one). Real
  strings do this when bow force is wrong for the position (the Schelleng diagram), so
  the fix is probably to scale bow force with position rather than to suppress it.
* make an accurate simulation of the Boss DD-3 delay pedal

## Elements from the Oxide Studio survey

A review of `~/github/oxide-studio` (MIT/Apache, Rust) found these gaps in Valis. The
oxide code is a pointer to what is missing, not code to port: write each from the
algorithm, mono, with a test. In order of use:

* **Reverb size and decay.** Size glides, but a fractional delay low-passes the tail
  slightly, so `decay` is exact only at size 1.
* **STFT time-stretch** with formant preservation, beside `val:Granulator`. Oxide's warp
  engine uses a phase vocoder with Laroche-Dolson phase locking and a spectral envelope
  taken by cepstral liftering. `val:SpectralGate` in `src/dsp/elements/Spectral.cpp` has the
  window, hop and latency machinery to build on. A time-stretch changes the length of the
  signal, which an element that produces one output sample per input sample cannot do, so
  it needs either a sound file source (as `val:Granulator` has) or a live version that
  stretches a recent history.
* **Wavetable follow-ups.** Tables are the built-in three sets: loading a user table
  from a sound file, as `val:SampleLoad` does, is the next step.
* **PitchShift for chords.** The 30 ms two-head shifter is good on a single voice and
  shows a 33 Hz flutter on a held chord. A longer grain with a correlation search for the
  jump point (SOLA) would fix it at the cost of latency.

## Ideas from the map.audio PAM survey

From https://map.audio/mfx (a sampler and drum synth, so only its effects and modulation
design apply). Roughly in order of how self-contained they are:

* **Scala follow-ups.** `val:MidiPitch` and `val:Quantise` read a `.scl` file
  (`src/dsp/Tuning.h`). Still to do: a `.kbm` keyboard mapping, MTS-ESP sync, a
  `val:tuning` session override like the one for `val:SampleLoad`, and offering the scale
  to `val:Granulator`'s grain pitch.
* **Squeeze.** PAM's description of it could not be found (searched once, April 2026
  reviews do not mention it). Probably a bit-depth or dynamic-range squeezer; check the
  PAM manual before building.
* **A/B morph follow-ups.** `OpDispatcher::morph` and the `morph` MCP tool move every
  `val:Param` that declares `val:valueA` and `val:valueB`. Still to do: a slider for it in
  the Controls view, a host parameter so it can be automated, and a modulation source so an
  LFO can sweep it.
* **More drum engines.** `examples/modal-tom.ttl` is a membrane voice and
  `dx7-bells.ttl` covers FM bells. The tom has a pitch drop. Still open: a chaos voice and a snare
  with a wire rattle.
* **Randomizer follow-ups.** `OpDispatcher::randomize` and the `randomize` MCP tool
  draw every parameter from its range by seed. Still to do: a button for it in the Controls
  view, bounding it by a declared scale or genre, and randomising modulation and structure
  rather than only the knobs.

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

* check builds for warning messages (touch the sources and rebuild to see them all). Fix
  any in this codebase; the one left is in the third-party `httplib.h` (`-Wswitch-enum`)
* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
