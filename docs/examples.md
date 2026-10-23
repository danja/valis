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
  and full from C2 to D4; above that the tone loses its odd harmonics.
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
- `nam.ttl`: a Neural Amp Modeler capture. Needs a build with `-DVALIS_WITH_NAM=ON`.
- `subcircuit.ttl`: one `val:Subcircuit` instantiated twice, once per channel,
  which is how stereo is built.

## Not examples

- `temp.ttl` is a variant of `subsynth.ttl` with the same title.
