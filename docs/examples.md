# Examples

Every circuit in `examples/`, grouped by what it is. Each file starts with a
comment explaining its design, so this page says what is there and where to look.
The [manual](manual/index.md) has case studies for some of them.

## Instruments

**Subtractive**

- `sh101.ttl`: a monophonic synthesiser after the Roland SH-101. Oscillator, sub
  oscillator and noise into a ladder filter and amplifier, with an LFO.
  See [the case study](manual/sh101.md).
- `subsynth.ttl`: a smaller subtractive synthesiser with a switchable oscillator shape.
- `tuning.ttl`: a keyboard played in a [Scala](https://www.huygens-fokker.org/scala/scl_format.html) tuning, 31 tone equal temperament by default. The scale files are in `examples/tunings/`.
- `polysynth.ttl`: a voice written once and stamped out eight times with
  `val:voices`, so the engine allocates and steals notes. Shows polyphony.

**FM, in the manner of the Yamaha DX7**
(see [FM synthesis](manual/dx7.md))

- `dx7-epiano.ttl`: an electric piano on algorithm 5, three modulator and carrier pairs.
- `dx7-bass.ttl`: a bass on algorithm 1, a punch pair and a chain of three.
- `dx7-bells.ttl`: tubular bells on algorithm 1 with inharmonic ratios and a slow release.
- `dx7-brass.ttl`: a brass pad using `val:FMMatrix` for a feedback loop that spans
  two operators, which separate operators cannot do.

**Physical models**

- `flute.ttl`: a jet-drive waveguide. [Case study](manual/flute.md).
- `clarinet.ttl`: a reed and a bore stopped at one end. [Case study](manual/clarinet.md).
- `cello.ttl`: a bowed string with a body of two resonant banks behind it. In tune
  in tune from C2 to E5, measured on the string; above about 600 Hz it can jump an octave.
- `chimera.ttl`: eight voices, each two bowed strings that shake each other's bow.
  They lock, beat or refuse to settle depending on the tuning ratio and the coupling.
- `rings.ttl`: a Karplus-Strong plucked string, after Mutable Instruments Rings.
  `rings-beam.ttl` adds the dispersion of a stiff string or bar, `rings-modal.ttl`
  uses a bank of resonators tuned to a chosen object, and `rings-reed.ttl` is a
  waveguide clarinet. [Case study](manual/rings.md).

**Granular**

- `grist.ttl`: a grain delay after the Grist plugin in downspout. Grains read a short
  history, draw their pitch from a scale, and are fed back through a delay, a
  filter and a limiter. Size and delay time can follow the host tempo.
  See [the granular manual](manual/granular.md).

- `granular.ttl`: granulates a sound file, blends it with the plugin's audio input,
  can track MIDI pitch and can time its grains from the host transport.
  [Case study](manual/granular.md).

## Drums

- `909.ttl`: a TR-909 kit on General MIDI channel 10, with bridged-T resonators and
  sectioned controls. [Case study](manual/909.md).
- `blue-monday-sketch.ttl`: a sketch in the manner of 1983 synth-pop at 130 bpm, with a sequenced saw bass through a resonant ladder filter and four sequenced DMX drum sounds. It is not a transcription of any record; the patterns are placeholders to edit. Needs the host rolling, or `valis-render --tempo 130 --rolling`. Built on `val:StepSequencer`.
- `modal-tom.ttl`: a tom from a noise burst striking six membrane modes, with a pitch drop. The note is its pitch.
- `metal-bash.ttl`: a hammer striking a sheet of metal, tuned to MIDI. Noise through a
  granulator makes the strike, two parallel modal banks (plate and bar tables) make the
  ring, and a distortion, filters, compressor and short delays follow. Four voices.
- `808-bd.ttl`: a TR-808 bass drum, a bridged-T network with a pitch sweep.
- `dmx.ttl`: the Oberheim DMX, a seventeen-sound kit that plays the captured
  samples in `samples/Oberheim DMX`, as the real machine played its ROM. One voice
  card is defined once and stamped out for each sound with `val:option`.

## Effects and processors

- `basic.ttl`: input, a resonant filter, a saturator, output. A starting point to edit.
- `skream.ttl`: the Massive "scream" filter, the project's proof of concept. See `docs/skream.md`.
- `klon.ttl`: a Klon Centaur style overdrive with asymmetric clipping and a clean
  blend. [Case study](manual/klon.md).
- `modulation.ttl`: a chorus, a phaser and a flanger in series, sharing one LFO.
- `vocal-enhancer.ttl`: a ladder filter, a saturator and a compressor on a voice.
  `vocal-enhancer-rig.ttl` is the test rig for it, with a signal generator,
  oscilloscopes and a spectrum analyser.
- `split-reverb.ttl`: a reverb that leaves the low end alone. A crossover sends the
  band above 300 Hz to a feedback delay network reverb and passes the band below it
  dry. A slow `val:Drift` moves the room size, and a limiter holds the ceiling.
- `lofi.ttl`: reduced bit depth and sample rate with jitter, a high cut whose corner
  wanders with a `val:Drift`, a low cut ahead of the quantiser and a limiter.
- `multiband.ttl`: a three-band compressor with an upward stage, built from two
  crossovers and a compressor per band. The low band passes through a third crossover
  so that all three bands carry the same phase shift and add back to a flat response.
- `noise-gate.ttl`: an envelope follower driving a `val:Expander`, with threshold, ratio and release as parameters.
- `ducking.ttl`: sidechain ducking. The left input channel drives an envelope follower that turns down the right channel.
- `dd3.ttl`: a bucket-brigade delay pedal in the manner of the Boss DD-3, using `val:BucketBrigade`. Long delays and repeats go dark. The range switch and hold mode are not modelled.
- `timestretch.ttl`: a sound file at a different speed from its pitch, using `val:TimeStretch`: a phase vocoder with optional formant preservation. Speed 0 holds one moment of the file.
- `grain-clouds.ttl`: one `val:GrainBuffer` holding the bell, read by three `val:GrainCloud` elements at once through buffer arcs: a drone an octave down, a shimmer an octave up, and a scattering middle cloud.
- `nam.ttl`: a Neural Amp Modeler capture. Needs a build with `-DVALIS_WITH_NAM=ON`.
- `subcircuit.ttl`: one `val:Subcircuit` instantiated twice, once per channel,
  which is how stereo is built.

## Halloween set

Eight instruments for a slow, half-time dubstep version of Bauhaus's "Bela Lugosi's Dead", written
as a system test of the MCP interface. Each answers MIDI and is built to be played together; the
notes each drum voice answers are listed in its header.

- `halloween-drums-low.ttl`: kick (bridged-T body with a long sub), two low toms into a dark hall, and a noise slam, through tape and a limiter.
- `halloween-drums-high.ttl`: closed hat, an open hat through a feedback `val:FreqShifter`, a swelling shaker and wood block into a bucket-brigade echo, and a clap into a hall.
- `halloween-bass.ttl`: three layers on one voice: a sine sub, detuned saws through a ladder filter whose cutoff wobbles in time with the host, and a clipped wavetable growl.
- `halloween-organ.ttl`: six sine drawbars at the Hammond footages with key click, overdrive, a chorus for the scanner vibrato and a large room.
- `halloween-guitar.ttl`: a plucked `val:StiffString` through a pickup, amp clip, spring-style reverb and a dotted-eighth echo. The string runs flat as it rises, so it is corrected for the middle of the range.
- `halloween-theremin.ttl`: sine with a scoop into each note, wide vibrato and a random drift, then a slow phaser, echo and a long hall.
- `halloween-ornaments1.ttl`: two drifting vowel-wavetable choirs and a modal glass bell, through a chorus, a spectral delay and a long hall.
- `halloween-ornaments2.ttl`: a falling-saucer dive with warble, wavefolded, bit-crushed and smeared by a feedback frequency shifter, with a stuttering echo.

## Not examples

- `temp.ttl` is a variant of `subsynth.ttl` with the same title.
