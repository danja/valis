## Misc

* review pre-existing examples to see if they can be improved with the new features - subcircuits etc
  (profiles are done: every shipped instrument and effect now declares a trn:PluginProfile)
* **Blue Monday.** `examples/blue-monday-sketch.ttl` has the shape: a `val:StepSequencer` bass through
  a saw, ladder filter and VCA, over four sequenced DMX sounds. Its patterns are placeholders, not
  the record's. Still to do: the actual bass pattern and drum programming (needs a reliable
  transcription; do not guess), a Moog Source voice matched to its two oscillators and filter
  rather than the generic saw, the Emulator choir (no samples in the repository), and Peter Hook's
  bass guitar lead, which needs a plucked-string voice on `val:StiffString` (not yet tried).

## New instruments

The instruments that are done are described in `docs/examples.md`. What is open:

* **FM.** `val:RateLevelEnvelope` has keyboard rate and level scaling and a pitch ratio output;
  the scaling curves are smooth approximations of the DX7's stepped ones. The shipped DX7
  examples still set the matrix by hand and do not use `val:algorithm`, the scaling or a pitch
  envelope, and could.
* **val:NamModel.** Build with `-DVALIS_WITH_NAM=ON`; `examples/nam.ttl`. Still to do:
  resample around a model
  trained at a rate other than the engine's (it is refused now); optional use of the
  model's own input/output level and loudness; time a WaveNet at the engine's
  32-sample slice, since per-call overhead may dominate; a session override for the
  model file like the one for `val:SampleLoad`.
* **Grist follow-ups.** The feedback filter now has the plugin's three types, picked by a
  `val:AudioSelect` over `val:StateVariable`'s named outputs. Density is grains per
  second here and grains per window in the plugin, which needs a divide that no element
  offers. `val:Quantise` now takes a Scala scale (`scale` 7, `val:tuning`) but `examples/grist.ttl`
  and the plugin's own scale choice do not use it yet.
* **Granulator.** Split into `val:GrainBuffer` and `val:GrainCloud` joined by a buffer arc (the new
  `val:BufferPort`; see `include/valis/DspElement.h` for `SharedBuffer`). Several clouds can read one
  buffer. `val:Granulator` is unchanged and still the one-element form. Not done: the graph view
  draws a buffer pin like an audio pin, so a buffer arc looks like a signal; a cloud's `trigger` is
  the only way to time its grains from outside (a grain pool still cannot itself be patched
  between elements); and no element other than `val:GrainCloud` reads a buffer yet, which is the
  next use for the port, such as a looper or a sample slicer reading a `val:GrainBuffer`.
* **Bow above about 600 Hz.** Notes up to E5 (659 Hz) are in tune to within a cent and hold their
  fundamental at default pressure, since the loop filters now move with the note above 250 Hz
  (`kScaleAbove` in `src/dsp/elements/Sources.cpp`). At 659 Hz the string still settles an octave
  up at two bow positions (0.10 and 0.20), and at 880 Hz it needs more force than the default.
  The mode lock the old note described, just above position 0.125 at 165 Hz, no longer occurs.
  Positions near the bridge (0.04 to 0.06) show a weak fundamental, which is correct for a bow
  that close. Not yet tried: scaling bow force with position (the Schelleng diagram), which
  should widen the working range further.
* **DD-3 follow-ups.** `val:BucketBrigade` and `examples/dd3.ttl` model the chip's delay-bandwidth
  coupling, the darkening repeats and the compander. Not modelled: the range switch and hold
  mode, clock leakage, and the stage-to-stage losses. The bandwidth ratio (0.35) is a judgement
  that has not been matched to a measured DD-3; check it against a published frequency response.

## Elements from the Oxide Studio survey

A review of `~/github/oxide-studio` (MIT/Apache, Rust) found these gaps in Valis. The
oxide code is a pointer to what is missing, not code to port: write each from the
algorithm, mono, with a test. In order of use:

* **Time-stretch follow-ups.** `val:TimeStretch` plays a file at a chosen speed and pitch, with
  formants held on request. Not done: stretching a live input (it needs a recent history rather
  than a file), a transient detector so drums keep their edges (a phase vocoder smears them over
  its 43 ms window), and a measurement of its cost
  in a release build; a debug build renders 20 s in 2.8 s. Not heard yet either: the tests measure
  pitch, duration, level and formant position, not how it sounds.
* **Wavetable follow-ups.** Tables are the built-in three sets: loading a user table
  from a sound file, as `val:SampleLoad` does, is the next step.
* **PitchShift for chords.** `val:align` picks each head's landing point by correlation. On a
  three-note chord it cut the power spilling off the partials from 1.7% to 0.6% at -5
  semitones and made no difference at +3 (0.1% both). Not yet tried: a longer grain, and
  listening to it. A full SOLA with a variable hop would go further at the cost of latency.

## Single source for a knob's range

The Controls view (`ControlsView.cpp`) and the host parameters (`ValisProcessor.cpp`) each work out a
knob's range from the binding and the port; the ops did their own and were wrong until a test caught
it. Make both read `OpDispatcher::listParams`, so there is one answer. See MISTAKES.md.

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

## Helping agents use Valis

`docs/guide-for-agents.md` is held to the ontology and the tool list by `tests/docs/GuideTest.cpp`.
Done: `check_stability`; `history`, `undo`, `redo` and `diff` (`include/valis/CircuitHistory.h`, `CircuitDiff.h`); `apply_edits`, several edits as one with the circuit installed once; the guide and every example as MCP resources (`valis://guide`, `valis://examples/NAME`;
`include/valis/AgentResources.h`); "did you mean" for classes, ports and properties
(`include/valis/Suggest.h`); `describe_element` (every element has a description of at least 200 characters, enforced by
`OntologyTest`, and lists its options through `val:hasOption`); `get_diagnostics` reports `problems`; `analyze` and the analysis inside `render` measure a
sound (`include/valis/Analysis.h`); `lint` finds mistakes that compile (`include/valis/Lint.h`). Still
to do, in order of use:

* **Suggestions in errors: done for classes, ports and properties.** Still to do: a misspelt option
  value (a note name in a `val:pattern`, an enumeration number out of range) could offer the valid
  ones; and an unknown `val:` property is only reported by `lint`, not at load, because it is legal
  to set one an element ignores.
* **Batch edits: more kinds.** `apply_edits` covers add, remove, connect and disconnect. Not yet
  in it: setting an element's property or option (today that is a change to the Turtle through
  `set_turtle`), binding a parameter, and a dry-run that returns what the circuit would look like
  without installing it.
* **History: follow-ups.** `history`, `undo`, `redo` and `diff` keep up to 100 versions of the circuit's
  text. Not yet: naming a version to return to later (a bookmark), keeping the history across a
  session save and reload (it is lost when the plugin closes), versions for parameter changes
  (`set_param` moves a knob and is not a version), and showing the history in the editor, where undo
  today is the text box's own.
* **Stability check: follow-ups.** `check_stability` varies the bound parameters. Not yet: varying every
  control port of every element (which the conformance suite does for one element at a time), choosing
  the input (a recording, an impulse), a wider search that looks for the worst case and not just a few
  random ones, and checking for a click when a parameter jumps (the zipper test the conformance suite
  also lacks).
* **More lint.** A gate that never reaches an envelope in a circuit that plays notes; a `val:Param`
  range wider than the element's own; an `rdf:value` outside a port's range; gain staging estimated
  from the element chain without a render; and an `lint` option on `set_turtle` so a bad design is
  reported at the moment it is installed.
* **Analysis follow-ups.** Pitch tracking is one pitch (a chord reads as none); onsets are by
  energy and miss a legato change of note; a spectrogram summary and a similarity score between two
  files would let an agent match a reference sound.

## From the Cathode review

A review of `~/github/cathode` (a guitar processor in the spirit of the Axe-Fx III; C# engine,
GPL-3.0-or-later, so a source of ideas only: write each from the algorithm, with a test, and do not
copy code). Its `docs/03-testing-strategy.md`, `05-dsp-and-runtime-research.md` and
`08-blocks-parity.md` are the useful reading. In order of leverage:

* **Conformance suite: what is left.** `tests/dsp/ElementConformanceTest.cpp` now holds every element
  to determinism, reset, block-size independence, boundedness under random controls with every
  control jumping at once, no allocation in `process()`, and decay after input stops. Its first run
  found `val:Chorus`, `val:Flanger`, `val:Phaser` and `val:Noise` (all fixed). Not yet in it: the
  zipper check (no sample-to-sample step above a limit when a control jumps), declared latency
  against measured impulse delay, `Reset()` mid-note, and a full-scale square and DC as inputs. Ten
  elements put out subnormal values after their input stops and rely on the plugin's flush-to-zero
  (`juce::ScopedNoDenormals` in `ValisProcessor::processBlock`); they are listed in the test with
  that reason, and `valis-render` and the console do not set it.
* **Multi-rate conformance: triage the report.** `ElementConformanceTest` renders every element at
  its defaults on one multitone at 44.1, 88.2, 96 and 192 kHz and prints those whose octave-band
  levels differ from 48 kHz by more than 1 dB (3 dB, nonlinear). It is a report for now and
  asserts nothing: 19 elements differ, and each needs a look before it is fixed or excused with a
  reason. (A first version of the check had a 6 dB error of its own: tone power in a window goes with
  the window length squared, so the window is now the same number of samples at every rate.) A first
  guess at each, none confirmed:
  * Probably real, and worth fixing first: `val:StiffString` (15 dB overall at 88.2 kHz),
    `val:Crossover` and `val:StateVariable` (the low bands drop by 17 to 85 dB at 88.2 kHz, yet the
    state-variable filter's own `lp` and `hp` outputs match across rates when measured alone, so the
    difference is in how the test sums an element's several outputs, or a fault in the crossover),
    `val:Reed`, `val:TwinTBridge` and `val:Wavetable` (up to 18 dB in one band at 88.2 kHz, which
    suggests a constant written in samples), `val:Reverb`, `val:ModalBank` and `val:PitchShift` at
    44.1 kHz (a delay length or window rounded to whole samples).
  * Probably by design: `val:Redux` (a hold of N samples is a different frequency at each rate),
    `val:SpectralDelay` (delay is in whole STFT hops, so its resolution is in samples),
    `val:Noise` (white noise of fixed per-sample level has a different density at each rate), and the
    generators `val:Oscillator`, `val:SignalGenerator`, `val:Bow`, `val:Tanh` at 192 kHz and the
    modulation effects `val:Chorus`, `val:Flanger`, `val:Phaser`, whose band levels depend on where an
    LFO or a partial falls. For generators, compare pitch and level of the strongest partial instead.
* **RealtimeSanitizer job.** Clang 20's `-fsanitize=realtime` aborts on any allocation, lock or
  blocking call inside a function marked `[[clang::nonblocking]]`. Cathode builds its native path with it
  and runs two canaries first (an allocation it must catch) so a build where the sanitizer does
  nothing cannot pass. Valis's real-time rules are a list in `CLAUDE.md` that nothing enforces.
* **Golden renders and a CPU budget.** Bit-identity baselines per example circuit, regenerated on
  purpose; per-element cost measured and a reference preset's callback time compared with its budget.
* **Elements Valis lacks that Cathode has.** Each is a small, well-described algorithm:
  * `val:RingModulator`: audio times a sine carrier, with the carrier optionally locked to the tempo
    (a polarity-flipping tremolo). A `val:VCA` takes a control signal, so there is no audio times audio.
  * `val:Convolver`: zero-latency partitioned FIR convolution (uniform partitions, or Gardner's
    non-uniform scheme for long ones) for cabinet and room impulse responses, loaded from a file as
    `val:SampleLoad` is; with minimum-phase trimming by the real cepstrum, and an IR capture by sine sweep.
    The step the amp-simulation examples, and the NAM example, are missing.
  * `val:ToneStack`: the passive treble/bass/middle stack of Fender and Marshall amps, after Yeh and
    Smith, "Discretization of the '59 Fender Bassman tone stack" (DAFx-06).
  * `val:Vibe`: the Uni-Vibe, four unequal first-order allpasses sharing one lamp, with the lamp's
    lopsided response; `val:Rotary`: a Leslie as two rotors, a crossover, Doppler and amplitude
    modulation with motor inertia; `val:Wah`: from the circuit, not a swept bandpass.
  * `val:PitchTracker`: YIN (de Cheveigne and Kawahara, 2002) on a decimated copy, with a pitch output
    and a voiced flag as controls. It would let an oscillator or a granulator follow a played note.
    `val:OctaveDivider` (Schmitt trigger and flip-flops) is a cheaper cousin.
  * `val:Ducker` (wet drops while the dry input is above a threshold; a sidechain for a delay or reverb),
    a loudness meter to BS.1770 (K-weighting, LUFS) as a monitor element, program-dependent attack and
    release from the crest factor for `val:Compressor` (Giannoulis, Massberg and Reiss), and quantise and
    high-cut conditioning for `val:LFO`.
  * Reverb and delay variety: a Dattorro plate (JAES 1997), a multitap and a ten-tap delay with a
    rhythm-quantised variant, a plex-style delay line stack, and a feedback comb with a loop low-pass
    (Karplus-Strong) as a standalone element.
* **Amp modelling.** Cathode's `docs/06-amp-modelling-spec.md` and `07-amp-catalogue.md` describe 88
  amps stage by stage (triode stages with Koren-style tube laws tabulated for Hermite interpolation,
  passive networks, transformer, power amp, speaker load), and the research notes compare DK-method
  nodal solvers, wave digital filters and neural captures. Valis already has `val:Triode`, the
  diode elements and `val:NamModel`, and reserves `val:Network` for nodal analysis; a first circuit
  that uses a tabulated triode and the tone stack above, checked against a measured curve, would show
  whether the stage-by-stage approach is worth taking further.
* **Performance concepts.** Eight scenes per preset and four channels per block are the live-use idea
  behind `OpDispatcher::morph`: a scene is a named snapshot of parameters, and morph is a blend of
  two. Modifiers (LFO, envelope, sequencer, MIDI controller drive any parameter) are control arcs
  already. A block library of presets maps to subcircuit definitions shipped with a name.
* **Oversampling.** Cathode oversamples only around nonlinearities with polyphase IIR half-band filters
  (HIIR, zero added latency) and ADAA, and measures an aliasing floor (signal-to-alias at 1, 2.5, 5
  and 8 kHz tones at maximum drive, 60 dB or better). `val:oversampling` wraps `juce::dsp::Oversampling`;
  an IIR option, and an aliasing-floor test over every nonlinear element, would show whether it is worth it.

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

## Found while building the Halloween set

Failings met using the MCP tools to build `examples/halloween-*.ttl`. Each wants a fix in the program.

* **MCP server is off after launch.** The Standalone starts with the server disabled, and an agent sees
  only a refused connection. Either start it with the app when a token or port is configured, or have
  the Standalone say in its window that the server is off and how to turn it on.
* **MCP tools were not visible to the session.** Neither the Valis nor the Reaper server was registered
  with Claude Code for this project. Ship a `.mcp.json` (`{"mcpServers": {"valis": {"type": "http",
  "url": "http://localhost:7676/mcp"}}}`) so a checkout works without a manual `claude mcp add`.
* **`get_diagnostics` reports `sampleRate` 8000** for the live Standalone while `render` uses 48000. The
  live figure looks wrong, or the Standalone has no audio device open. Find out which.
* **No glide or slew element.** A theremin, portamento bass or legato lead needs a control signal to move
  to its target over a time. `halloween-theremin.ttl` fakes it with a scooping envelope. Add `val:Slew`
  (rise and fall times, in milliseconds).
* **No control-rate add.** `val:ControlMultiply` is the only arithmetic, so a detune in cents, a pitch
  offset in Hz, or the compensation for a loop's delay cannot be written. Add `val:ControlAdd`.
* **`val:StiffString` is not tuned.** It runs flat by a delay that does not depend on pitch (about 90 to
  100 microseconds), measured as 25 cents at E3, 58 at E4 and 104 at E5. The damping filter and the
  dispersion allpasses lengthen the loop. Subtract their phase delay as `Loss::phaseDelay` does in
  `Waveguide.h`, and add a test across the range. `halloween-guitar.ttl` multiplies the pitch by 1.025
  as a stopgap and is in tune only near G3 to G4.
* **`analyze` reports the wrong octave** for a pure tone with a bright second partial: the wood block
  at 1850 Hz was named as 925.6 Hz (A#5). Check the pitch tracker against a sine and a TwinTBridge ping.
* **`render` cannot run a transport.** The tempo-synced wobble LFO in `halloween-bass.ttl` free-runs in
  a `render`, so its musical timing cannot be checked through MCP. `valis-render --tempo --rolling` can.
  Add `tempo` and `rolling` parameters to `render`.
* **Authoring cost.** Eight circuits took eight hand-written generators, because Turtle with one
  `val:Arc` block per connection runs to hundreds of lines. A way to write arcs tersely, or an
  `export` of the loaded circuit built from `apply_edits`, would help an agent write a file.
* **Running a circuit inside Reaper.** Several instances now each get an MCP port (7676, 7677, and so
  on) and `/health` reports the port and the host's track name. Still to do: a way to set a circuit from
  a path in the plug-in's saved state, so a project can be written without opening the GUI; and a check,
  in a real Reaper session with eight instances, that the track name is passed on (`updateTrackProperties`
  is only called by some hosts) and that the ports are what the Settings menu says.
* **`valis-render --midi` is new** (a standard MIDI file played at `--tempo`; add `--rolling` for a tempo-synced
  circuit). It reads only note events and a fixed tempo; tempo changes and controllers are ignored. The `render` MCP tool
  still takes one note, so give it the same `midi`, `tempo` and `rolling` parameters.
* **`reaper-mcp` bugs** are listed in `/chalet/github/reaper-mcp/TODO.md`. The project at
  `~/Music/dead-at-midnight/` is affected by two of them: the master has two ReaLimit plug-ins, and
  no mix could be rendered through `reaper-mcp` (a manual render from Reaper worked).
* **Test `valis-render --midi`.** Nothing covers it. Add a console test that renders a MIDI file whose
  notes fall at offsets that are not multiples of the block size, and asserts every note sounds (the
  MISTAKES.md rule on events and block boundaries). Also: a file with a note-on and no note-off, tempo
  meta events (ignored today), and a path that does not exist.
* **List `--midi` in the manual.** `docs/manual` has no page for `valis-render` options; `usage()` is
  the only description. Add one, with `--tempo --rolling --midi` as the way to render a part.
* **Make `halloween-ornaments1.ttl` polyphonic** like the organ (a `val:Subcircuit` with `val:voices`),
  so the choir can hold a chord. It plays one note at a time now, and the song uses single notes.
* **Make the Halloween guitar and bass polyphonic or document that they are not.** The guitar riff is
  single notes by design; say so in the header of each file.
* **Listen to the song** (`~/Music/dead-at-midnight/dead-at-midnight-reference-mix.wav`) and write down what
  is wrong: balance, the wobble rate against the drums, the guitar's pitch near the top of its range, the
  theremin scoop. Everything so far is measured, not heard. Changes belong in `score/song.py` and the
  circuits.
* **Put the song into the plug-in once a circuit can be chosen per instance** (see the Reaper item
  above): replace the audio stems with eight Valis tracks driven by the MIDI files in `midi/`, so the
  project edits the instruments rather than recordings of them.
* **Keep the song's generator scripts in the repository?** `score/song.py` and the circuit generators
  (`gen.py`, the eight `*.py` files) are in the session scratchpad and `~/Music/dead-at-midnight/score/`
  only. Decide whether a terse circuit writer belongs in `scripts/` (see "Authoring cost").

## Recurring - check periodically

* check builds for warning messages (touch the sources and rebuild to see them all). Fix
  any in this codebase; the one left is in the third-party `httplib.h` (`-Wswitch-enum`)
* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
