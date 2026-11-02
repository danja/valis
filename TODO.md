## Misc

* **Cumbia Cuántica** (avant-garde cumbia; `valis-artifacts/artifacts/cumbia-cuantica/`, built by `scripts/cumbia-cuantica/make.py`): built, rendered and
  mixed (v3, -11.2 LUFS, true peak -1 dBTP, loudness range 3.3 LU; measured, not heard). It uses all five circuits from `scripts/elements-demo/`. Needs a
  listener: whether the surf guitar (`bassman-amp`) sounds like a guitar (`valis-shot is` reads it as a synthesizer preset, 0.63; an electric guitar scores
  0.00), whether the talk box (`vocoder-speak`) says words, whether the triplet arpeggio slips against the bar as meant, whether the 8-bar fracture is too
  long, and whether the cumbia pattern is right (written from the usual description, not checked with a player). Not done: a Reaper project
  (`valis-song reaper --midi` skips the two `val:recorded` parts, so they would need adding by hand); a vocal; a second version with the guitar
  played by hand. **A limit of the song format found here:** a part must be a circuit played by a MIDI file. A circuit driven by the transport
  alone (`arp-quantiser`) or by audio input (`vocoder-speak`) has to be rendered by the track script and marked `val:recorded`, so its gating by section is
  done in Python. A part option naming an input file, and one gating a part by section, would let `valis-song` do both.
* **Songs as data, next steps** (`docs/songs.md`): fixed patterns and chords are now a `val:Score` (`valis-song score`); `dub-metal` is converted (its `metal` part is still generated in Python). Not tried: a score written first for a new song. Not done in the score: a pattern that depends on the bar's position in a cycle other than the chord progression, per-play velocity scaling, tuplets, and a vocal-guide format. The stem checks
  the `write-a-song` skill asks for (per-bar rms, `isnan`) are still by hand. `fit_params` and the analysis renders have their own
  block loops, not `renderBlocks`. The mix takes about 70 s for a 3-minute song in a Debug build; Release is several times faster.
* **Street Hammer** (hardcore techno inspired by Orff's "Gassenhauer"; `valis-artifacts/artifacts/street-hammer/`): built, rendered and
  mixed (v1, -10.5 LUFS, 170 bpm measured, true peak -1 dBTP; measured, not heard). Needs a listener: the kick against the bass, the
  hoover against the marimba (the figure is G3 to G4, the tune G4 to F6), the risers and whether the three drops hit. Not done:
  a vocal or sample hook; the `valis-song reaper --midi` project for it; a second version at 175 to 180 bpm.
* **909 rim shot, found while checking the kit (31 October 2026, measured, not heard).** The rim shot's spectral centroid is 12.5 kHz
  (a real one is mostly 1 to 3 kHz); not looked at. Every drum now holds its hit's velocity with `val:SampleHold` and scales its level
  by it (`tests/engine/Tr909VelocityTest.cpp`). At full velocity the bass drum (gain +11) peaks at 0.63 and the snare at 0.61; the clap
  (gain -2) at 0.19. Other circuits driven by `NoteGate` (`latin-perc.ttl`, `halloween-drums-*.ttl`) were checked; only the 909 had the fault.
* **Nursery instruments and "All Fall Down"** (`scripts/nursery/`, `circuits/nursery-*.ttl`; the track is in `valis-artifacts/artifacts/all-fall-down/`,
  2 November 2026): eight circuits and a 3:00 track at 140 bpm built from them. Mix v3 is -11.0 LUFS and v4 adds the vocoded SAM voice
  (-11.5 LUFS, true peak -1.0 dBTP). All measured, not heard (the composer has played the project in Reaper and said the voice works; the
  listening tests are theirs). The project `all-fall-down-midi.rpp` has been opened and plays, with the instruments at ports 7677 to 7685.
  Looked at: the lead stem peaks at its -1 dBFS ceiling but only 3 samples in 10 million are above 0.85, so it is not leaning on the limiter.
  The bass sounds an octave under the note below MIDI 69 (measured 55 Hz for note 45, 110 Hz for 57, 440 Hz for 69): its strongest
  partial is the added sine, and the score is written for that. Mix v6 is -10.6 LUFS: the voice is a `val:recorded` part in `song.ttl` (v6), but its WAV is rendered by hand in Reaper and its track is not written by `--midi`.
* **A speech track in a song** (found making All Fall Down, 2 November 2026). The voice is SAM (`~/github/downspout/plugins/sam`) into
  `circuits/nursery-voice-vocoder.ttl`, added to the Reaper project by hand and rendered to `wavs/voice.wav` by hand. Open:
  * The vocoder's carrier is a fixed A, E, A chord, so the voice does not follow the chord loop. Giving the carrier notes (a MIDI input
    played from the pad's MIDI file) would need the `--midi` writer to put a MIDI item on the voice track.
  * `valis-song reaper --midi` could write the `sam` track itself: SAM and the vocoder instance, with `voice.txt` as SAM's state. It needs
    SAM's state format, which is in the downspout repository.
  * The voice level (-22 dB in `make.py`) is a guess; v6 is not heard.
* **Inferno Choir and MIDI controllers** (`circuits/inferno-choir.ttl`, `scripts/inferno/`, 2 November 2026): a scream and wail voice that
  answers notes and CC1 to CC4, through the new `val:MidiCC` and `ValisEngine::setController` (plugin, `valis-render --cc N=V` and CC in
  MIDI files read by `valis-render --midi` and `OfflineRender`). Measured, not heard; the neural check calls the wail a human voice about
  as often as a violin section and the scream a distorted guitar. Needs a listener. The voice now uses `val:Glottal` (Rosenberg, Klatt, LF, Radiated, Cosine, Saw, Pulse);
  with the Rosenberg pulse the neural check reads the wail as a human voice (0.60) but not the distorted scream or a calm note.
  `val:Glottal` follow-ups: its closure aliases at high pitch (use `val:oversampling` on the element above about 600 Hz; not measured), the three
  oscillators share one jitter sequence (no seed port), and the LF solver is tested for Rd 0.3 to 2.7 only. Not done: pitch bend and channel pressure (a scream wants a bend), CC in `valis-song` stems (a part
  reads its MIDI file's controllers only through `valis-render --midi`; `check` ignores them), a `val:MidiCC` smoothing time (use `val:Slew`),
  an editor control that sends a CC without a host, and a check in Reaper: the plugin must be reinstalled (`./install.sh`) to receive CC.
* **An unreleased New Order track: "Tenement Lights".** Built and rendered (`docs/new-order-track.md`,
  `scripts/archive/electro83/`, output in `~/Music/tenement-lights/`). A three-minute arrangement with lyrics
  (`docs/short-circuit-lyrics.md`) and a sleeve (`docs/short-circuit-sleeve.md`) exist; it needs a singer. Still to
  build: the speech stutter (specified in
  `docs/new-order-track.md`), and a Source patch with a third waveform per oscillator and octave switches. Its
  listening is under "Needs a listener" below.

* **UI appearance.** Give the controls a more visual appeal than JUCE's defaults, and add a light theme.

## Needs a listener

Everything here is measured, not heard. Each wants a person with ears, then a change in the score or circuit.

* **Tenement Lights:** balance, the Source's envelope, the choir's formants and the arrangement (`track.py`).
* **Dead at Midnight** (`~/Music/dead-at-midnight/dead-at-midnight-reference-mix.wav`): balance, the wobble rate
  against the drums, the guitar's pitch near the top of its range, the theremin scoop. Changes belong in
  `scripts/archive/halloween/song.py` and the circuits.
* **Lights On, Nobody Home:** the user has the v3 FLAC and is adding vocals, mixing and mastering. The Latin
  percussion was balanced by peak level only (listen, adjust decay times); the trumpets are the weak part (below).
* **Time-stretch:** the tests measure pitch, duration, level and formant position, not how it sounds.

## From earshot (see `docs/shot.md`)

* **Real recordings through `fit_modes`.** `val:FreeModalBank` and `fit_modes` are tested on sounds made from known modes
  (frequencies exact, ring times within 1% and levels within 2 to 3%). A real conga, cowbell or bell has close modes, noise
  and a drumhead that is not purely modal; try a few real recordings and see what comes out. The analysis window is 85 ms,
  so a ring under about 0.1 s is read coarsely, and the fit ignores the attack and the phase of each mode.
  First trial, 31 October 2026, on five CC0 recordings from freesound.org (lossy previews; sources and licences in
  `SOURCES.txt` beside them; the fit was run through the same `fitModes`, `renderModes` and `spectralDistance` calls as
  `OpDispatcher::fitModes`, on audio decoded with ffmpeg, not through MCP or the app's file reader). 12 modes each:

  | recording | distance | what the modes look like |
  |---|---|---|
  | conga, D#4 (373411) | 10.7 dB | 66 to 398 Hz, rings 0.35 to 1.3 s |
  | conga, "Kygo" (678748) | 16.0 dB | 272 to 748 Hz, rings 0.15 to 1.2 s |
  | cowbell, LP (319149) | 13.8 dB | four modes under 100 Hz and two more at 35 and 60 s rings, which is the clamp for "no measurable decay" |
  | cowbell, spurc (376948) | 11.8 dB | three peaks 13 Hz apart near 1500 Hz, then 4.5, 7.5, 10.5, 13.5 and 16.5 kHz, odd multiples of 1.5 kHz, like a synthesised square-wave cowbell |
  | bell (536731) | 13.8 dB | groups of two or three modes 10 to 25 Hz apart near 650, 850 and 970 Hz, rings 2.5 to 5 s |

  On the `compare` scale (a few dB a small change, tens of dB a different sound) these refits are between the two. Open: whether the
  close groups are real doublets (a bell has them) or the picker splitting one peak (it merges only peaks under 10 Hz apart, on bins
  of 0.7 Hz); the low-frequency modes with ring times of 35 to 60 s, which look like rumble and not a struck mode, and probably want
  a floor on the ring time or a high-pass; whether a lossy preview changes the result (try a lossless original); and a listener to
  say whether the refits sound like the recordings. Not tried: the MCP tool itself. (MP3 does not load in this build; see `audio/README.md`.)

* **`fit_params`: what is left.** Both limits found on `dub-bass` are fixed (the note is released where the recording's was, or
  not at all inside a window that is only the start of it; and the search tries a seeded spread of starting points before it
  climbs). From a start that scored 6.4 dB it now reaches 0.45 dB in 120 renders, where one climb stopped at 1.8 dB; the
  true settings score 0. Not done: the climb still uses a fixed step schedule and could use the recording's own spectral
  shape to choose which parameters to move first; it searches only the knobs already bound as `val:Param`; and it matches a
  single held note, not a phrase.
* **A library of reference recordings.** `audio/` holds five CC0 percussion hits from freesound.org. Extend it into a set to
  measure synths against, and to feed the sampler (see "A versatile sampler"). Use: the target for `compare` and `fit_params`;
  a check on `is` and `listen` (the harp, violins, trumpets and shakers in the item below are judged against nothing real yet);
  real input for `fit_modes`; and spectra to set beside a physical model's, as the flute and brass tuning tests do by ear.
  Wanted: for each instrument Valis models, a few notes at different pitches and dynamics, sustained and plucked or struck, plus
  the hits of a drum kit and common percussion. Keep to CC0, or record the licence and the credit that CC BY needs. Freesound's
  previews download without a login (128 kbps MP3, enough to start); the originals and its search API need an account and an API
  token, which the user would have to supply. Keep a manifest per directory (file, author, licence, source URL, pitch and
  dynamic if known), as `audio/README.md` does, and check a sound's own page for its licence, not the search filter. A note on
  which recordings are samples of a synthesiser (the "spurc" cowbell is one) so they are not mistaken for acoustic references.
* **Calibrate `listen`.** Port earshot's vocabulary of 322 descriptions and its statistics, so a cosine becomes a z-score
  and a verdict; then a realness score (real recording or synthesised) on the same CLAP features.
* **Make the harp, violins, trumpets and shakers sound like themselves.** `valis_shot is` hears the harp as a piano
  (53%), the violins as an electric organ (47%), the trumpets as a synthesizer preset (60%) and the shakers as congas
  (62%); the guitars and congas pass (`docs/shot.md`). Rerun `is` after each change.

## New instruments

The instruments that are done are described in `docs/examples.md`. What is open:

* **Sampler panel: what is left.** `src/ui/SamplerBox.cpp` edits the table (level, pan, key, name, file, add and
  remove) as a session override, saved with the project but not in the circuit's Turtle: "Copy table" puts it on
  the clipboard to paste into `val:samples`. Not done: writing the table back into the document, a waveform with
  start, end and loop handles, dragging a file onto the panel, velocity layers and round robin takes shown as
  separate rows (they are grouped under one instrument), a choke group control, and a screenshot test (the panel
  has been built but not looked at; check it in the standalone).
* **Freesound over MCP: what is left.** `freesound_search` and `freesound_download` (`src/net/Freesound.cpp`, test
  `tests/net/FreesoundTest.cpp`) use token authentication, so a download is the 192 kbps Ogg preview, not the original.
  The original needs OAuth2 (the client id and redirect URL are in `.env`; the flow is a browser login and a code
  exchange, not yet written). The fetch is HTTPS through cpp-httplib and OpenSSL, behind `VALIS_WITH_FREESOUND`; the plugin
  now depends on libssl at run time when it is on. Search results are not paged (first page of up to 50).
* **`val:Sampler`: what is left.** The first version is in (`src/dsp/elements/Sampler.cpp`, `src/dsp/SampleMap.h`,
  `circuits/sampler-percussion.ttl`): a table in the circuit (`val:samples`) of zones with key and velocity ranges, root, gain,
  tune, start and end, sustain loop with crossfade, reverse, choke groups and round robin; sixteen voices of its own; pitched by
  cubic interpolation, within 0.1 cent at 44.1, 48 and 96 kHz. Not done:
  * Crossfade between neighbouring zones (a key takes the one zone that answers it, or the nearest).
  * A release loop, and a loop that follows the key (a loop in samples is fixed).
  * A filter with its own envelope inside the element (use `val:Ladder` and `val:Envelope` after it).
  * A monophonic legato mode; voice stealing takes the oldest voice with a 1 ms minimum attack, which can still tick.
  * A note held across the same key being hit again does not retrigger (the engine sees one held note).
  * `circuits/drum-machine.ttl` has per-sound pan on the `left` and `right` outputs; there is no separate output per pad.
  * `circuits/drum-machine.ttl` has one recording per drum: its soft and hard layers are the same file at a different
    gain and tune, and the closed hat's two takes differ by 0.4 semitone. Real layers need several hits per drum
    (a kit with several velocities recorded). Its kit has no side stick, cowbell or tambourine yet.
  * Reading `.sfz` or `.sf2` files, and a General MIDI kit loader; slicing a long file at its onsets
    (`src/ops/Analysis.cpp` has the onset finder).
  * A session override for a zone's file, as `set_sample` does for `val:SampleLoad`, and a key map in the Controls view
    (the Controls view shows only the knobs).
  * Round robin runs on one counter for the element, not one per key, so a pattern across keys can line up with it.
  * Looking at the sound: the example was measured (pitch, level), not listened to.

* **FM.** `val:RateLevelEnvelope` has keyboard rate and level scaling and a pitch ratio output;
  the scaling curves are smooth approximations of the DX7's stepped ones. The shipped DX7
  examples still set the matrix by hand and do not use `val:algorithm`, the scaling or a pitch
  envelope, and could.
* **val:NamModel.** Build with `-DVALIS_WITH_NAM=ON`; `circuits/nam.ttl`. Still to do:
  resample around a model
  trained at a rate other than the engine's (it is refused now); optional use of the
  model's own input/output level and loudness; time a WaveNet at the engine's
  32-sample slice, since per-call overhead may dominate; a session override for the
  model file like the one for `val:SampleLoad`.
* **Grist follow-ups.** The feedback filter now has the plugin's three types, picked by a
  `val:AudioSelect` over `val:StateVariable`'s named outputs. Density is grains per
  second here and grains per window in the plugin, which needs a divide that no element
  offers. `val:Quantise` now takes a Scala scale (`scale` 7, `val:tuning`) but `circuits/grist.ttl`
  and the plugin's own scale choice do not use it yet.
* **Granulator.** Split into `val:GrainBuffer` and `val:GrainCloud` joined by a buffer arc (the new
  `val:BufferPort`; see `include/valis/DspElement.h` for `SharedBuffer`). Several clouds can read one
  buffer. `val:Granulator` is unchanged and still the one-element form. Not done: the graph view
  draws a buffer pin like an audio pin, so a buffer arc looks like a signal; a cloud's `trigger` is
  the only way to time its grains from outside (a grain pool still cannot itself be patched
  between elements); and no element other than `val:GrainCloud` reads a buffer yet, which is the
  next use for the port, such as a looper or a sample slicer reading a `val:GrainBuffer`.
* **Bow above about 600 Hz** (and the violins above G5: `val:Bow` is 51 cents flat at E6, 1318 Hz). Notes up to E5 (659 Hz) are in tune to within a cent and hold their
  fundamental at default pressure, since the loop filters now move with the note above 250 Hz
  (`kScaleAbove` in `src/dsp/elements/Sources.cpp`). At 659 Hz the string still settles an octave
  up at two bow positions (0.10 and 0.20), and at 880 Hz it needs more force than the default.
  The mode lock the old note described, just above position 0.125 at 165 Hz, no longer occurs.
  Positions near the bridge (0.04 to 0.06) show a weak fundamental, which is correct for a bow
  that close. Not yet tried: scaling bow force with position (the Schelleng diagram), which
  should widen the working range further.
* **DD-3 follow-ups.** `val:BucketBrigade` and `circuits/dd3.ttl` model the chip's delay-bandwidth
  coupling, the darkening repeats and the compander. Not modelled: the range switch and hold
  mode, clock leakage, and the stage-to-stage losses. The bandwidth ratio (0.35) is a judgement
  that has not been matched to a measured DD-3; check it against a published frequency response.

## Circuits for elements no circuit uses

Done 3 November 2026: the element catalogue (`scripts/catalogue_elements.py`) listed 11 elements that no shipped circuit used, and
`scripts/elements-demo/` now has a circuit and a measured test for each (`distortion-bench`, `bassman-amp`, `lofi-codec`,
`arp-quantiser`, `vocoder-speak`). A new element still needs one; the catalogue shows which have none. Not done:

* **`gate-garden.ttl`** (optional: `val:SpectralGate` is already shown in `lofi-codec`). A noisy tone, and a hiss with a low note under it. The `threshold`
  dial removes the hiss bin by bin while the tone stays; show the same signal through a time-domain `val:Expander` on a `val:Select` to
  hear the difference. Test: broadband noise level falls by a stated amount while a sine stays within 1 dB.
* **Heard, not measured.** Every one of these five is measured only. None has been listened to; the vocoder in particular needs a real
  voice (the SAM render from "All Fall Down" is in `valis-artifacts`) to judge whether words are understood under LPC and under the
  channel vocoder at several orders.

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
* **More drum engines.** `circuits/modal-tom.ttl` is a membrane voice and
  `dx7-bells.ttl` covers FM bells. The tom has a pitch drop. Still open: a chaos voice and a snare
  with a wire rattle.
* **Randomizer follow-ups.** `OpDispatcher::randomize` and the `randomize` MCP tool
  draw every parameter from its range by seed. Still to do: a button for it in the Controls
  view, bounding it by a declared scale or genre, and randomising modulation and structure
  rather than only the knobs.

## RDF handling (from the inbox)

* **Factoidal (`~/github/factoidal`) as a substitute for the RDF handling: not as it stands.** Looked at, October 2026. The Valis side is
  small and tidy (654 lines, every serd and sord call in `TurtleStore.cpp`), so there is little piecemeal handling to
  replace. Factoidal has a formally verified graph, extracted to 1,710 lines of C (`formal/fstar/c-output/`), and its graph is an
  immutable `list triple`, which would not need a lock; but lookups are linear scans (`find_by_subject`), the extraction has
  no Turtle parser (that is Rust, for WebAssembly, in `rdf-wasm/`), so serd would stay, and it needs the KaRaMeL runtime. The ontology
  holds thousands of triples and a model build makes thousands of lookups, so the linear scans would be the cost. Worth
  revisiting if factoidal grows an indexed graph and a C Turtle parser; what to borrow now is the property (reads that do not
  mutate), which the lock above gives.
* **Songstress follow-ups** (`~/github/songstress`, MIT; one 290-line Python script, `listen.py`, that writes a "listening card" for a
  stereo WAV). Looked at, October 2026. Valis already has loudness, centroid, rolloff, onsets, pitch and one whole-file stereo
  correlation (`include/valis/Analysis.h`). What it adds, as C++ from the algorithm with a test, in order of use:
  * **Done** (stereo over time, section map, key estimate, tempo candidates; see `docs/shot.md` and the headers in `include/valis/`).
    What is left: section-level anomaly flags (the stereo flags fire on every ten-second window of a long, gradually widening intro);
    songstress's tuning-drift correction for the key estimate (it lands a half-step low on detuned material); the key estimate has
    not been checked against a recording whose key is known; chroma reports sounding partials, not played notes, so on bass the key
    is unreliable (`pitchedShare` per section is the warning); and a tempo that changes within a track gets one answer.
  * Not worth porting: the 64-band mel feature matrix as a stored "memory" (`valis-shot listen` has CLAP features already).

## Helping agents use Valis

`docs/guide-for-agents.md` is held to the ontology and the tool list by `tests/docs/GuideTest.cpp`. Still to do, in order of use:

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
  energy and miss a legato change of note. A similarity score between two files exists as `spectralDistance`
  (`src/shot/`); a spectrogram summary does not, and the parameter search under "From earshot" would use both.

## From the Cathode review

A review of `~/github/cathode` (a guitar processor in the spirit of the Axe-Fx III; C# engine,
GPL-3.0-or-later, so a source of ideas only: write each from the algorithm, with a test, and do not
copy code). Its `docs/03-testing-strategy.md`, `05-dsp-and-runtime-research.md` and
`08-blocks-parity.md` are the useful reading. In order of leverage:

* **Conformance suite.** `tests/dsp/ElementConformanceTest.cpp` now holds every element
  to determinism, reset, block-size independence, boundedness under random controls with every
  control jumping at once, no allocation in `process()`, and decay after input stops. Its first run
  found `val:Chorus`, `val:Flanger`, `val:Phaser` and `val:Noise` (all fixed). The zipper check
  (one control jumps, the output must not step) is in; it found mixes and depths applied in one sample in `val:Chorus`,
  `val:Flanger`, `val:Phaser` and `val:Reverb`, now glided over 10 ms (`Smoothed` in `Common.h`), and seven elements are excused
  with reasons. Controls that move a delay time or a note are excused whole, so a new control on one of them is not checked.
  Declared latency is checked against the
  measured delay of an impulse (eight elements declare one; seven match exactly). Full-scale DC and a full-scale square are among its inputs, and every element stays bounded. Ten
  elements put out subnormal values after their input stops and rely on the plugin's flush-to-zero
  (`juce::ScopedNoDenormals` in `ValisProcessor::processBlock`); they are listed in the test with
  that reason, and `valis-render` and the console do not set it.
* **RealtimeSanitizer job.** Clang 20's `-fsanitize=realtime` aborts on any allocation, lock or
  blocking call inside a function marked `[[clang::nonblocking]]`. Cathode builds its native path with it
  and runs two canaries first (an allocation it must catch) so a build where the sanitizer does
  nothing cannot pass. Valis's real-time rules are a list in `CLAUDE.md` that nothing enforces.
* **Golden renders and a CPU budget.** Bit-identity baselines per example circuit, regenerated on
  purpose; per-element cost measured and a reference preset's callback time compared with its budget.
* **Elements Valis lacks that Cathode has.** Each is a small, well-described algorithm:
  * **`val:Convolver` follow-ups.** The element is in (`src/dsp/elements/Convolver.cpp`, `tests/dsp/ConvolverTest.cpp`,
    `circuits/convolver-room.ttl`): a 128-sample direct head and uniform 128-sample partitions, no latency, matching direct
    convolution to about 2e-7. Not done: non-uniform partitions (Gardner, 1995), which would cut the cost of a long response
    from about 1500 multiply-adds per sample per second of response to a few hundred; a stereo response (two files, or one
    two-channel file); minimum-phase trimming of the response by the real cepstrum; capturing a response by sine sweep;
    a pre-delay control; and a measured cost in a release build. The shipped response is synthetic, from `val:Reverb`;
    a real recorded room, with its licence, is wanted in `audio/ir/`. Not heard.
  * **`val:ToneStack` follow-ups.** The Bassman stack is in (Yeh and Smith, checked against the circuit solved node by node). Not done: the
    Marshall and Vox stacks, which are the same circuit with other values (and the Vox has no middle pot); they need component values
    read from a schematic, which could not be confirmed from the sources reached. A `val:model` choice would hold them.
  * `val:Vibe`: the Uni-Vibe, four unequal first-order allpasses sharing one lamp, with the lamp's
    lopsided response; `val:Rotary`: a Leslie as two rotors, a crossover, Doppler and amplitude
    modulation with motor inertia; `val:Wah`: from the circuit, not a swept bandpass.
  * **`val:PitchTracker` follow-ups.** The element is in (`src/dsp/elements/PitchTracker.cpp`, `tests/dsp/PitchTrackerTest.cpp`,
    `circuits/pitch-follower.ttl`): YIN on a copy reduced to about 12 kHz, within 0.2 cent on sines from 41 to 1318 Hz at 44.1 to 192 kHz,
    and right on a missing fundamental. Not done: a recorded voice, guitar or bass has not been tried (only synthetic tones and
    noise); a chord reads as one note or as unvoiced; no smoothing or glide on the output (the oscillator follows each 5 ms step);
    the cost in a release build is unmeasured.
  * **`val:OctaveDivider` follow-ups.** In (`src/dsp/elements/OctaveDivider.cpp`, `tests/dsp/OctaveDividerTest.cpp`): Schmitt trigger at a
    fraction of the input's peak, one or two flip-flops, output scaled to the input's loudness (measured 0.93 of the input's rms on a sine).
    Pitch is exact on sines from 110 to 440 Hz. Not done: tried on a real guitar or bass (only sines and silence); the classic
    low-pass on the output that rounds the square; no example circuit; and not heard.
  * `val:Ducker` (wet drops while the dry input is above a threshold; a sidechain for a delay or reverb),
    a loudness meter as a monitor element (the BS.1770 measurement exists offline as `measureLoudness` in `src/shot/`; this
    needs a streaming version without allocation), program-dependent attack and
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

What the survey asks for and Valis already has (so nobody re-derives it): subcircuits, sample-accurate note events, sound-file
identity, stereo, the block domain (`val:SpectralGate`), per-element oversampling, event output ports, the bounded voice pool,
delay giving cycles their semantics (`val:UnitDelay`, `cycleAdj`/`orderAdj`), a checked opaque-component boundary, resource loading
off the real-time thread, a distinct control rate, and declared latency reported to the host.

* **Oversampling as a region rather than per element.** Done per element:
  `val:oversampling 2/4/8/16` wraps any element and runs it faster, measured at
  185x less alias energy through a hard-driven tanh at 8x
  (`src/dsp/Oversampled.h`). What the survey actually argues for is a *region*:
  survey cases 7 to 12 put a whole nonlinear section inside one oversampled
  domain, resampling once at each end rather than once per element. That needs
  the compiler to keep a region rather than flattening it, and the engine to run
  a node range with N times the buffers. Worth doing only if the per-element form
  turns out to cost too much in a real pedal chain.

## From the dub-metal piece (see `valis-artifacts/artifacts/dub-metal/docs/report.md`)

* **MCP long operations: what is left.** `measure`, `compare`, `sounds_like`, `listen` and `fit_modes` run on the server's own
  thread; `render` prepares a job on the message thread and renders on the server thread; `fit_params` and `check_stability`
  run on the server thread from a snapshot (`CircuitSnapshot`, `RenderJob`). Not done: the first call still waits for the
  neural models to load, and an operation cannot be cancelled or report progress. A caller that disconnects leaves the work
  running to its end.
* **`sounds_like` and `listen` in the app.** Both work now that `build.sh` builds ONNX in by default (checked in the relaunched
  Standalone), but the first call timed out at the 5-second message-thread limit while the models loaded and the second succeeded. That call
  now runs off the message thread (see above), which should end it: check after the next relaunch. Still worth loading the
  models at startup so the first call is not 12 seconds. `build/` (Debug)
  is also configured with ONNX by `build.sh` now. `listen` should still cap the audio it reads.
* **A lighter stab voice.** `tonewheel-organ.ttl` has eight voices of nine drawbars and is slower than real time in Debug, for
  three-note chords a few times a bar. A four-voice organ or an electric-piano stab (`dx7-epiano.ttl`) may do.
* **More industrial sources.** Only `metal-bash.ttl` was used. Try a gated noise loop, a clanking step sequence, a metal
  `val:FreeModalBank` fitted with `fit_modes` from a real recording of struck metal.
* **Needs a listener:** dub-metal v6 (mixed by `valis-song`; v4 changed the stabs and the metal after v3 was heard as stabs that did not
  bubble and metal that sounded like bells). The figures moved, but only a listener can say if it works.
* **Lint for NaN-prone stages.** A gain chain of 27 dB into a drive of 16 was legal in every range yet produced NaN before the
  `Adaa.h` fix. `valis-render` now exits 1 when any sample is not finite, but that check has no test: no shipped element
  produces NaN on purpose. Add a deliberately faulty test element, or a lint for a large gain chain into a drive.

## Found while building the Halloween set

Failings met using the MCP tools to build `circuits/halloween-*.ttl`. Each wants a fix in the program.

* **MCP server is off after launch.** The Standalone starts with the server disabled, and an agent sees
  only a refused connection. Either start it with the app when a token or port is configured, or have
  the Standalone say in its window that the server is off and how to turn it on.
* **Standalone opened its audio device at 8000 Hz.** `get_diagnostics` showed `sampleRate` 8000 (the engine passes the host's rate
  through, and the saved settings had no audio setup, so JUCE took the device's first listed rate). `ValisEditor` now asks for
  48000 when the device is under 32 kHz. Not checked in the running app: rebuild, relaunch, and `get_diagnostics` should say 48000.
* **Authoring cost.** Eight circuits took eight hand-written generators, because Turtle with one
  `val:Arc` block per connection runs to hundreds of lines. A way to write arcs tersely, or an
  `export` of the loaded circuit built from `apply_edits`, would help an agent write a file.
* **Running a circuit inside Reaper.** Several instances now each get an MCP port (7676, 7677, and so
  on) and `/health` reports the port and the host's track name. Done: `valis-song reaper --midi` writes each circuit into the
  instrument's saved state, and in a real Reaper session with nine instances the track names (`riser`, `sam`) and ports 7677 to 7685
  were right (2 November 2026). Still to do: check that the ports are what the Settings menu says.
* **`valis-render --midi` limits.** It reads only note events and a fixed tempo; tempo changes and controllers are
  ignored (`tests/console/RenderMidiTest.cpp` pins the tempo behaviour, so it will fail when the tool learns to read it).
* **`reaper-mcp` bugs** are listed in `/chalet/github/reaper-mcp/TODO.md`. The project at
  `~/Music/dead-at-midnight/` is affected by two of them: the master has two ReaLimit plug-ins, and
  no mix could be rendered through `reaper-mcp` (a manual render from Reaper worked).
* **Make `halloween-ornaments1.ttl` polyphonic** like the organ (a `val:Subcircuit` with `val:voices`),
  so the choir can hold a chord. It plays one note at a time now, and the song uses single notes.
* **Make the Halloween guitar and bass polyphonic or document that they are not.** The guitar riff is
  single notes by design; say so in the header of each file.
* **Put the song into the plug-in: done for dub-metal** (`valis-song reaper --midi`, then circuits loaded through each
  instance's MCP port, 31 October 2026). Not done for Halloween. `--midi` does not write the return buses (echo), high-passes
  or markers, and the circuits live only in the saved instance state, not in the `.rpp` written by the tool.

## Recurring - check periodically

* check the tests for a debugging leftover that makes one pass without testing (an early `return 0;` in `main`, a probe called
  first); `FluteTest` had one for many commits
* check builds for warning messages (touch the sources and rebuild to see them all). Fix
  any in this codebase; the one left is in the third-party `httplib.h` (`-Wswitch-enum`)
* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
