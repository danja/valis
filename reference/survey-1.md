# Native plugin DSP → LeanDSP: 60-source architectural survey

Working research note, 2026-09-17. This expands the original five deep dives with **55 additional implementation cases**, for **60 cases total**.

The purpose is not to port these plugins. It is to ask a harder question:

> **What is the smallest statically bounded computational model that can naturally represent the internal architecture of most real-world audio plugins without hiding the interesting parts behind bespoke primitives?**

Source code was read architecturally. GPL/LGPL source is treated as research material here, not copied into JigDAW/LeanDSP. AGPL projects were excluded from the source-study set. Licences differ per upstream project and must be checked deliberately before any future implementation work.

## Method and an important taxonomy warning

Plugin Universe currently has large overlapping populations such as `effect`, `instrument`, `distortion`, `utility`, `synth`, `midi`, `sampler`, `modulation`, `guitar`, `reverb`, and `dynamics`.

Those labels are useful **facets**, but they are not one clean taxonomy. They mix several dimensions:

- **role** — effect, instrument, utility;
- **signal domain** — MIDI;
- **implementation family** — synth, sampler;
- **DSP technique** — distortion, reverb, dynamics, modulation;
- **application domain** — guitar.

A single implementation can therefore quite reasonably be `effect + distortion + guitar`, or `instrument + sampler`, or `utility + MIDI`.

The 60-case sample below is intentionally **architecturally stratified, not prevalence-weighted**. It over-samples difficult and revealing cases. The counts in this note describe the coded survey, not prevalence across Plugin Universe.

## Headline result

The diverse cases reduce surprisingly well to one common idea:

```text
inputs + parameters + events + previous state + resources + clocks
                              │
                              ▼
                    bounded transition/process
                              │
                              ▼
         outputs + events/analysis + new state + latency/tail
```

I would now describe the desired LeanDSP core as a language of **bounded reactive components**, not primarily as a language of scalar sample expressions.

The original `sample x => expression` remains an excellent *kernel*, but it is only one execution domain inside the component model.

## What the 60 cases actually pressure us to represent

These are hand-coded architectural tags from the source survey:

| Need | Cases | Share |
|---|---:|---:|
| persistent state | 54/60 | 90% |
| sample/audio-rate processing | 52/60 | 87% |
| stereo or explicitly multi-channel structure | 26/60 | 43% |
| reusable/nested components | 18/60 | 30% |
| explicit delay memory | 18/60 | 30% |
| timed events | 14/60 | 23% |
| voices/grains or similar bounded populations | 14/60 | 23% |
| distinct control-rate behaviour | 12/60 | 20% |
| fixed arrays/collections | 12/60 | 20% |
| block/window processing | 11/60 | 18% |
| external resources (samples, IRs, weights, etc.) | 11/60 | 18% |
| explicit nonlinear stage | 9/60 | 15% |
| multiple sample rates / oversampling / resampling | 9/60 | 15% |
| explicit feedback topology | 8/60 | 13% |
| scheduling beyond simple continuous processing | 8/60 | 13% |
| transport/beat dependence | 7/60 | 12% |
| stochastic/random process | 7/60 | 12% |
| spectral/FFT representation | 4/60 | 7% |
| neural inference | 4/60 | 7% |
| opaque external DSP engine is the sensible boundary | 2/60 | 3% |

The striking point is not that exotic algorithms exist. It is that **structure dominates the missing feature list**: state, collections, ports, rates, events, bounded populations, resources, and explicit timing.

## The three execution domains, plus a resource plane

The survey strongly suggests that forcing every plugin through one scalar callback would be a mistake. Most cases naturally decompose into three bounded execution domains:

### 1. Sample kernel

```text
Audio sample + state + audio-rate controls → Audio sample + new state
```

This covers IIRs, oscillators, envelope followers, waveshapers, circuit recurrences, delay taps, modulation and a surprisingly large amount of conventional DSP.

### 2. Block/window kernel

```text
Frame[N] + block state → Frame[M] + new state
```

This is the natural home for FFT/STFT, convolution, reverse windows, some granular operations, neural inference, resampling and algorithms with explicit latency. It should not be disguised as thousands of scalar primitive nodes.

### 3. Event transition/scheduler

```text
Timed Event + event state + transport → events / changes to bounded populations
```

This handles MIDI transformers, note allocation, sample triggering, voice stealing, grain scheduling, sequencers and transport-aware changes.

### 4. Immutable/resource plane

```text
Resource<T> = identity + integrity + metadata + bounded runtime view
```

Samples, wavetables, impulse responses, tuning maps and neural weights should be first-class resources rather than unexplained bytes that happen to live in Wasm memory.

## Plugin Universe categories: what they share and where they diverge

| Facet | Common computational shape | Distinct representational pressure |
|---|---|---|
| **effect** | audio → stateful transform → audio | baseline component/state model; stereo/sidechain and latency soon appear |
| **instrument** | events → bounded voice population → audio | event timing, note state, voice allocation/stealing, tuning |
| **distortion** | filters + nonlinear transfer, often oversampled | nonlinear math, coefficient derivation, oversampling domains, FIRs |
| **utility** | routing / analysis / conversion | arbitrary port directions, analysis outputs, sometimes no transformed audio at all |
| **synth** | oscillators + envelopes + modulation + filters + voices | component arrays, modulation graph, control/audio rate distinction |
| **MIDI** | event stream → state/scheduler → event stream | frame timestamps, queues, transport; scalar audio is irrelevant |
| **sampler** | events + resource regions → resampling voices → audio | immutable assets, zones/regions, looping, interpolation, voice pools |
| **modulation** | phase/control generators and delayed transforms | tempo/transport, control rate, interpolation, phase/reset semantics |
| **guitar** | composition of nonlinear/filter/delay/convolution/neural blocks | not a computational primitive: often a stress test for composition and multirate DSP |
| **reverb** | large delay/allpass/comb/FDN/convolution networks | large static memory, explicit feedback, arrays, multichannel, tail semantics |
| **dynamics** | detector → envelope → gain computer → gain stage | sidechains, lookahead, analysis/control rates, log units, attack/release state |

The useful conclusion is that these should **not become eleven special language modes**. They emerge from a smaller orthogonal basis.

## The 60 implementation cases

`needs` is shorthand for the language/runtime feature pressure visible in the inspected source. A row is an architectural reading, not a claim of source equivalence.

| # | Implementation / source evidence | Survey roles | Architecture and LeanDSP pressure |
|---:|---|---|---|
| 1 | **dm-BigMuff** — `big_muff/src/lib.rs`, `op_amp2.rs` | effect · distortion · guitar | analogue coefficient derivation → 3rd-order IIR state → clipping/tone; helper functions, records and fixed vectors |
| 2 | **ChowMatrix** — `src/dsp/Delay/DelayProc.cpp` | effect · modulation | nested processors inside delay feedback; explicit delay-bearing cycles, components, stereo, transport/block operations |
| 3 | **amsynth** — `src/core/synth/VoiceBoard.cpp` | instrument · synth | LFO + 2 VCO + ADSRs + VCF + VCA per voice; events, voice arrays, enums and control/audio rates |
| 4 | **Dragonfly Hall** — `plugins/dragonfly-hall-reverb/DSP.cpp`, Freeverb3 tree | effect · reverb | early/late stereo networks made from delays, allpasses and filters; component arrays, feedback, large static memory, tails |
| 5 | **ADLplug** — `sources/opl3/adl/player.cc` | instrument · synth | MIDI wrapper over substantial OPL emulation engine; demonstrates a legitimate opaque/external boundary |
| 6 | **AnalogTapeModel** — `HysteresisProcessing.h`, `ToneControl.cpp` | effect · distortion | nonlinear hysteresis solver / learned variants plus filters; iterative/nonlinear kernels, multirate and optional neural component |
| 7 | **dm-DS1** — `ds1/src/clipper/fir_filter.rs` | effect · distortion · guitar | 8× SIMD/FIR oversampling around nonlinear clipper; fixed vectors and explicit rate domains |
| 8 | **dm-Fuzz** — `fuzz/src/clipper/fir_filter.rs` | effect · distortion · guitar | same revealing pattern: fixed FIR history + oversampled nonlinear stage |
| 9 | **dm-Rat** — `rat/src/op_amp.rs`, FIR clipper | effect · distortion · guitar | parameter-derived analogue transfer → bilinear transform → IIR + oversampled clipping |
| 10 | **dm-SD1** — `biquad_filter.rs` | effect · distortion · guitar | compact transposed biquad recurrence nested into a larger nonlinear pedal model |
| 11 | **dm-Shredmaster** — op-amp/filter modules | effect · distortion · guitar | chain of reusable circuit-derived filter/nonlinear components; composition more useful than bespoke opcodes |
| 12 | **dm-TubeScreamer** — biquad + clipper modules | effect · distortion · guitar | circuit-derived state plus oversampled nonlinear section; same core abstractions recur across pedal families |
| 13 | **AmpForge** — `core/AudioBlock.hpp`, `AmpBlock.hpp` | effect · guitar · distortion | explicit `AudioBlock` component chain; sample transform, reset/lifetime semantics and reorderable composition |
| 14 | **ToneShiftEQ** — `Biquad.h`, `SVF.h`, `BandDetector.h`, `Convolver.h` | effect · utility | combines scalar filters/detectors with convolution and IR morphing; one plugin spans sample, analysis and block/resource domains |
| 15 | **vcf-lv2** — `lowpass.c`, `filter_type*.h` | effect | classic small stateful CV-controllable filters; near-direct LeanDSP sample kernels |
| 16 | **Dilophilter** — `dilophilter.c` | effect | configurable stereo HP/LP stages and changed-parameter coefficient updates; component array + control update separation |
| 17 | **Trilophilter** — `trilophilter.c` | effect | same pattern expanded to HP/mid/LP stages; static component collections are preferable to copy/paste state |
| 18 | **tremelo.lv2** — `tremelo.c` | effect · modulation | smoothed frequency/gain, phase accumulator and sine/square table; audio vs control rate becomes explicit |
| 19 | **dm-GrainDelay** — `grains.rs`, `grains/grain.rs` | effect · modulation | delay buffer + bounded grains + triggers + randomness + stereo mixing; bounded pool/scheduler abstraction |
| 20 | **dm-TimeWarp** — `voices.rs`, `voices/grains/grain.rs` | effect · instrument · modulation | notes drive a population of granular voices reading a delay line; events, voices and grains converge on one bounded-lifetime model |
| 21 | **dm-SpaceEcho** — `space_echo/src/lib.rs`, `wow_and_flutter.rs`, `duck.rs`, `reverb.rs` | effect · reverb · modulation | variable delay + stochastic wow/flutter + saturation + ducking + reverb; nested heterogeneous components and side analysis |
| 22 | **dm-Stutter** — `stutter/src/lib.rs`, `repeat_trigger.rs` | effect · modulation | timed repeat capture/read with random duration/fraction generation; scheduler + transport semantics matter more than new arithmetic |
| 23 | **dm-Whammy** — `whammy/src/lib.rs`, `grains.rs` | effect · modulation | pitch detector drives grain-based pitch shift; analysis stream feeding a bounded granular processor |
| 24 | **dm-Reverse** — `reverse/src/lib.rs`, `phasor.rs` | effect | delay/window readback with phase-controlled reverse behaviour; bounded window/block semantics |
| 25 | **dm-Reverb** — `reverb/src/lib.rs`, `predelay.rs`, `taps.rs`, `taps/grains.rs` | effect · reverb | stereo delay/tap network including reverse/shimmer grains; recursive components plus tail semantics |
| 26 | **dm-Vibrato** — `vibrato/src/lib.rs`, `lfo.rs` | effect · modulation | stochastic LFO enabling plus delay modulation; random source should be explicit/deterministic in a checked build |
| 27 | **RoomReverb** — `Source/RoomReverb.cpp`, Freeverb3 tree | effect · reverb | early + late stereo reverb built from Freeverb3 components; essentially another nested delay-network language |
| 28 | **everb** — `src/everb.hpp` | effect · reverb | compact comb/damping/feedback structures; static delay memory and feedback contracts |
| 29 | **roboverb** — `src/roboverb.hpp` | effect · reverb | similar compact network, useful as a smaller proof/visualisation target than Dragonfly |
| 30 | **Minaton-XT** — `MinatonProcess.cpp`, `src/synth.cpp` | instrument · synth | MIDI-driven synth with waveform/sample data and resampling; resources + events + voice/component state |
| 31 | **padthv1** — `plugin/padthv1_dpf.cpp` | instrument · synth | MIDI is split at exact event positions around audio processing; strong evidence for sample-accurate event segmentation in the host/core |
| 32 | **Geonkick** — `src/dsp/src/oscillator.c` | instrument · synth | oscillator with multiple envelopes, FM/noise/random state and percussion synthesis; reusable envelope/component records |
| 33 | **Nelly-GB-synth** — SameBoy `apu.c`, `apu.h` | instrument · synth | chip APU with registers, clocks, channels, envelopes and model-specific state; fixed structs/arrays and multiple clock domains |
| 34 | **VL1-emulator** — `Voice.cpp` | instrument · synth | waveform voice + ADSR + vibrato/tremolo LFOs + filtering; very conventional expandable voice component |
| 35 | **sfizioso** — sfizz engine + `filters_modulable.dsp` | instrument · sampler | sample regions/voices/resources plus generated/modulable DSP; sampler semantics cannot be reduced to just an oscillator primitive |
| 36 | **Fluida** — `Fluida/XSynth.cpp`, `XSynth.h` | instrument · sampler | wrapper around FluidSynth/SoundFont engine; resource identity, MIDI and opaque-engine contracts are the sensible boundary |
| 37 | **SpectMorph** — `smmidisynth.hh`, `smsinedecoder.hh`, `smifftsynth.cc` | instrument · synth | MIDI voices plus spectral frames, FFT synthesis/decoding and RT memory areas; block/spectral domain must coexist with events/voices |
| 38 | **stftPitchShiftPlugin** — `FFT/PocketFFT.h` | effect · modulation | explicit FFT frame transforms; window/hop size and latency are semantic, not implementation trivia |
| 39 | **speech-denoiser** — RNNoise `denoise.c`, `rnn.c`, FFT | effect · utility | FFT/features → recurrent neural net → spectral gains; block domain + model weights/resources + latency |
| 40 | **NeuralRack** — `StreamingResampler.h`, engine | effect · utility | model inference at model sample rate wrapped in streaming resampling; model rate belongs in component/resource metadata |
| 41 | **Ratatouille** — `NeuralModel.cpp`, `RtNeuralModel.cpp` | effect · guitar | model loading + optional resampling + block inference; opaque or checked neural primitive with hashed weight resource |
| 42 | **master_me** — `MasterMePlugin.cpp`, generated Faust | effect · dynamics · utility | mastering chain plus metering/analysis; illustrates generated DSP and passive outputs, not merely audio-in/audio-out |
| 43 | **harmonizer.lv2** — `harmonizer.cpp`, aubio pitch code | MIDI · utility | audio analysis produces MIDI/event output; one graph crosses audio → block analysis → event domain |
| 44 | **B.Schaffl** — `BSchaffl.hpp`, `Message.cpp`, `Ports.hpp` | MIDI · utility | MIDI pattern delay/stretch/shuffle with randomness and transport; a plugin with essentially no meaningful scalar-audio kernel |
| 45 | **Drumlabooh** — drum/sample engine + `fx-resofilter.cpp` | instrument · sampler | triggered sample resources/voices plus per-voice/effect DSP; reinforces resource regions and bounded voice pools |
| 46 | **BOops Delay** — `FxDelay.hpp` | effect · modulation | transport-relative delay and feedback over stereo buffer; delay + timeline position |
| 47 | **BOops Phaser** — `FxPhaser.hpp` | effect · modulation | position/tempo-derived modulation of delayed stereo signal; explicit transport-derived control stream |
| 48 | **BOops Flanger** — `FxFlanger.hpp` | effect · modulation | variable stereo delay driven by phase; same small primitives compose another named effect |
| 49 | **BOops Reverser** — `FxReverser.hpp` | effect | reads earlier positions from buffered timeline; time-indexed buffer semantics rather than scalar recurrence alone |
| 50 | **BOops Decimate** — `FxDecimate.hpp` | effect · distortion | sample-hold/rate reduction; tiny state machine that fits the scalar core directly |
| 51 | **BOops Reverb** — `FxReverb.hpp` | effect · reverb | embedded stateful stereo reverb component; supports treating complex leaf processors as openable nested components |
| 52 | **BOops EQ** — `FxEQ.hpp` | effect | six filters in a fixed collection; compelling evidence for `Array 6 Filter` rather than six hand-expanded states |
| 53 | **BOops Noise** — `FxNoise.hpp` | utility · synth | seeded/random stereo generator; randomness should be a typed stateful source with reproducibility semantics |
| 54 | **Gula fades** — `fades.dsp` | utility | four-channel gain/fade mapping from smoothed controls; multi-output utility graph with almost no conventional DSP state |
| 55 | **Gula splits** — `splits.dsp` | utility | one input fan-out into several weighted outputs; topology/ports dominate representation |
| 56 | **Gula lfo_cv** — `lfo_cv.dsp` | utility · modulation | control generator with amplitude/offset/shape; a control stream is not merely low-volume audio |
| 57 | **Gula peak_audio_to_cv** — `peak_audio_to_cv.dsp` | utility · dynamics | audio → attack/release/peak analysis → CV/control output; typed analysis/control ports matter |
| 58 | **Gula pequed** — `pequed.dsp` | effect · dynamics | envelope analysis drives dynamic filter behaviour; clear split between detector/control and sample filter |
| 59 | **Gula sweabed** — `sweabed.dsp` | effect | smoothly interpolated filter controls; simple but exposes parameter/control-rate semantics |
| 60 | **Gula vibey** — `vibey.dsp` | effect · modulation | coupled tremolo/vibrato LFO structure; declarative graph maps naturally to nested control/audio components |

## Strong cross-cutting findings

### 1. “Node” and “graph” really should be the same abstraction at different zoom levels

The source repeatedly presents processors as structs/classes containing smaller processors. A top-level guitar amp contains tone filters; a reverb contains combs/allpasses; a voice contains oscillators/envelopes/filter; an EQ contains bands.

So a LeanDSP component should be renderable as:

```text
[ Reverb ]
```

and, when opened:

```text
predelay → early reflections → diffuser[4] → FDN[8] → width/mix
```

and then further opened into state/filter/delay recurrences. This is not merely a UI trick; it mirrors how real DSP source is structured.

### 2. Fixed arrays are disproportionately important

Real source constantly says, in effect:

```text
Filter bands[12]
Voice voices[16]
Delay taps[8]
Grain grains[N]
Channels[4]
```

Without fixed collections LeanDSP will either become verbose or hide structure behind opaque primitives. A dependently-sized/static `Array N T` is one of the highest-value additions.

### 3. Bounded dynamic lifetime is the right abstraction for voices *and* grains

Synth voices, sampler voices and granular grains look different musically but share a computational pattern:

```text
Pool N Voice
Pool M Grain
```

with deterministic allocation, activation, release and stealing/reuse. This is much better than general heap allocation and remains amenable to static memory proofs.

### 4. Delay is more than memory: it gives cycles semantics

Feedback is common in delay/reverb/modulation. A graph cycle should therefore be legal exactly where a delay/state-bearing component establishes causality:

```text
feedback cycle without delay  → rejected / algebraic-loop solver required
feedback cycle through z⁻¹    → ordinary causal DSP
```

That gives the graph editor, compiler and verifier one shared rule.

### 5. Control signals need types/rates, not different coloured audio cables only

Examples such as tremolo, dynamic EQ, audio-to-CV, envelopes and transport LFOs all become clearer if a value carries rate and semantic metadata:

```text
Control<UnitInterval> @ control
Control<Hz>           @ control
Audio<F32>            @ audio
Event<Midi>           @ frame
Analysis<RMS>         @ block
```

This also solves much of the visualisation scaling problem: `UnitInterval`, `Bipolar`, `Hz`, `dB`, etc. can imply meaningful default scopes rather than relying on rolling min/max.

### 6. Resources are first-class, not incidental files

Sampler regions, convolution IRs, neural weights and wavetables all need the same underlying facility:

```text
resource snare : Sample {
  uri
  sha256
  channels
  sampleRate
  frames
}
```

The runtime may map/cache/stream it differently, but the semantic graph should retain identity, integrity and relevant metadata.

### 7. Some implementations should stay opaque

ADLplug and Fluida are useful counterexamples. Their plugin wrappers call substantial external engines. Translating the wrapper into LeanDSP would create a *false appearance* of transparency while leaving the interesting computation elsewhere.

The right model is graduated transparency:

```text
opaque external component
checked ABI/resource/RT contract
        ↓ optionally
transparent LeanDSP component
        ↓ open
nested components
        ↓ open
state / expressions / buffers
```

## A candidate LeanDSP component model

The survey suggests something like this conceptually:

```text
component Compressor where
  input  audio : Audio Stereo F32
  input  side  : Audio Stereo F32 optional
  output out   : Audio Stereo F32

  param threshold : Db := -18 range -60 0
  param attack    : Ms := 10 range 0.1 200

  state env : F32[2] := 0
  buffer lookahead : AudioBuffer Stereo 512

  control @ 64 samples =>
    ... update control values ...

  sample frame =>
    ... bounded recurrence ...
```

An instrument can add events and pools:

```text
component Synth where
  input notes : Event Midi
  output out  : Audio Stereo F32

  pool voices : Voice[16]

  on event e =>
    allocate/steal voices deterministically

  sample frame =>
    sum active voices
```

A spectral processor can add a block domain without contaminating the scalar language:

```text
component PitchShift where
  input  x : Audio Stereo F32
  output y : Audio Stereo F32

  block 2048 hop 512 window Hann =>
    bins := fft(frame)
    ...
    return ifft(bins')
```

## Proposed orthogonal core, rather than a catalogue of magic DSP opcodes

### Values and static structure

- scalar numeric types (`F32`, perhaps selected `F64`);
- semantic numeric wrappers/ranges (`Unit`, `Bipolar`, `Hz`, `Db`, `Seconds`, `Frames`);
- tuples/records;
- `Array N T` and fixed vectors;
- pure helper functions;
- enums/sum types for modes/waveforms/stages.

### Stateful structure

- scalar state;
- typed fixed buffers/ring buffers;
- reusable stateful `component`s;
- `Pool N T` for bounded dynamic populations;
- explicit reset/prepare/lifetime semantics.

### Ports and time

- audio ports with channel/bus shape;
- control/analysis ports;
- event ports with absolute frame timestamps;
- sample, control, block/hop and event execution rates;
- host transport values as explicit inputs;
- sidechains as ordinary typed ports, not special host magic.

### Higher-order realtime structures

- explicit delayed feedback;
- deterministic bounded iteration over static arrays/pools;
- block/window transforms;
- resampling/rate-domain boundaries;
- deterministic random generators with explicit state/seed.

### Resources

- immutable sample/wavetable/IR/model/tuning resources;
- content hash/integrity metadata;
- bounded runtime views;
- asynchronous loading outside the real-time kernel.

### Runtime metadata

- declared latency;
- tail semantics;
- memory bound;
- maximum event/voice/grain counts;
- supported sample-rate/channel constraints.

## Verification should vary by component family

“One verified DSP language” should not imply one giant theorem.

### Universal structural obligations

For every checked component we can plausibly establish:

- all state/buffer accesses are statically bounded;
- declared pools cannot exceed capacity;
- no allocation/file/network/blocking call occurs in the RT kernel;
- loops have static or host-bounded limits;
- memory growth is absent from compiled RT code;
- event queues are bounded and ordered;
- feedback cycles are causally delayed or explicitly delegated to an algebraic-loop primitive;
- reset/prepare produce valid state.

### Numerical obligations where useful

For circuit/filter/dynamics components:

- parameter domains;
- finite coefficient computation;
- filter stability under stated ranges where tractable;
- bounded/saturating transfer properties;
- equivalence of scalar and SIMD/oversampled formulations to stated tolerance.

### Scheduling obligations

For synth/sampler/granular/MIDI components:

- deterministic event ordering;
- bounded voice/grain population;
- allocation/stealing invariants;
- note-off/release lifecycle properties;
- sample-accurate event boundaries.

### Resource/opaque-component obligations

For samples, IRs, models and external engines:

- content identity/integrity;
- dimensions/rate/channel declarations;
- ABI and memory contract;
- latency/tail/RT-safety claims as explicit attestable properties.

This is a much more credible notion of “verified plugin” than pretending that a neural model, a MIDI shuffler and a second-order IIR should all carry the same style of proof.

## Consequences for the JigDAW graph/runtime

The internal graph UI and the outer project graph should converge on the same generic machinery:

```text
select / move / connect / disconnect
zoom / fit / overview
collapse / expand recursively
filter node categories
automatic relayout
inspect/edit parameters and semantic units
probe values at their native rate
scope / timeline / FFT / events
show latency, tail and resource boundaries
```

A closed component can show a legible big-picture label. Opening it progressively reveals ports → components → state/expressions. A foreign native/JigDAW plugin can remain opaque while still exposing profile/ports/latency/resources. A LeanDSP component can open all the way down.

## What I would build next from this survey

The source evidence has changed the priority order. I would **not** spend the next cycle adding dozens of named DSP functions. The highest-leverage language work is:

1. `component` + typed multiport I/O;
2. `Array N T` and records/tuples;
3. reusable stateful component instances;
4. semantic units and explicit rate domains;
5. bounded `Pool N T` for voices/grains;
6. timed event handlers and transport inputs;
7. first-class immutable resources;
8. explicit delayed-feedback graph semantics;
9. bounded block/window kernels with declared hop/latency;
10. then selected standard-library components: oscillator, envelope, biquad/SVF, delay/interpolator, FIR/resampler, FFT, convolution, dynamics detector.

That basis covers far more real plugin architecture than a growing bag of opaque `reverb()`, `sampler()`, `pitchshift()` and `bigmuff()` calls.

## Survey limitations

- This is a **representational survey**, not a benchmark and not a compatibility test.
- The sample deliberately overweights structurally informative implementations.
- Some large systems were inspected at a representative core/entrypoint rather than exhaustively line-by-line.
- Several projects wrap external libraries; those are specifically useful for identifying opacity/resource/ABI boundaries.
- Plugin Universe category counts are overlapping facets and should not be interpreted as mutually exclusive classes.
- The coded feature counts are judgements from source architecture and should be treated as research annotations, not objective upstream metadata.

A machine-readable companion file, `native-to-leandsp-survey.json`, records the 60 cases, roles, architectural needs and source paths so we can query/revise the coding rather than freezing these judgements in prose.