# Examples

Every circuit in `circuits/`, grouped by what it is. Each file starts with a
comment explaining its design, so this page says what is there and where to look.
The [manual](manual/index.md) has case studies for some of them.

## Instruments

**Subtractive**

- `sh101.ttl`: a monophonic synthesiser after the Roland SH-101. Oscillator, sub
  oscillator and noise into a ladder filter and amplifier, with an LFO.
  See [the case study](manual/sh101.md).
- `subsynth.ttl`: a smaller subtractive synthesiser with a switchable oscillator shape.
- `tuning.ttl`: a keyboard played in a [Scala](https://www.huygens-fokker.org/scala/scl_format.html) tuning, 31 tone equal temperament by default. The scale files are in `circuits/tunings/`.
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
- `metal-clang.ttl`: `metal-bash.ttl` with a short ring (about a third of a second) and a hotter drive, and the hammer's
  noise burst sent straight to the output as well. It is a struck sheet where `metal-bash` rings like a bell. Play it from C2 to C4.
- `808-bd.ttl`: a TR-808 bass drum, a bridged-T network with a pitch sweep.
- `dmx.ttl`: the Oberheim DMX, a seventeen-sound kit that plays the captured
  samples in `audio/oberheim-dmx`, as the real machine played its ROM. One voice
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
- `halloween-organ.ttl`: eight voices of six sine drawbars at the Hammond footages with key click, then overdrive, a chorus for the scanner vibrato and a large room.
- `halloween-guitar.ttl`: a plucked `val:StiffString` through a pickup, amp clip, spring-style reverb and a dotted-eighth echo.
- `halloween-theremin.ttl`: sine that glides between notes through `val:Slew`, scoops into each note, with wide vibrato and a random drift, then a slow phaser, echo and a long hall.
- `halloween-ornaments1.ttl`: two drifting vowel-wavetable choirs and a modal glass bell, through a chorus, a spectral delay and a long hall.
- `halloween-ornaments2.ttl`: a falling-saucer dive with warble, wavefolded, bit-crushed and smeared by a feedback frequency shifter, with a stuttering echo.

## Instruments for a 1983 electronic record

Built for "Tenement Lights", an original instrumental in the manner of New Order's 1983 work. Each models a
machine the band used; see `docs/new-order-track.md`. The drums are `dmx.ttl`. The score and mix are in
`scripts/electro83/`.

- `moog-source.ttl`: a mono bass voice with a saw and an octave-down square, a 24 dB ladder filter whose cutoff is an envelope sweep plus a share of the keyboard pitch (`val:ControlAdd`), and two ADSR envelopes.
- `six-string-bass.ttl`: a plucked `val:StiffString` played high on the neck through a pickup, amp clip and chorus. One note at a time.
- `prophet-5.ttl`: eight voices of a saw and a sharp square through a 24 dB filter with keyboard tracking, then a chorus and a small room.
- `emulator-choir.ttl`: four voices of a vowel (two saws through three fixed formants), bit-reduced, then a tape wobble, chorus and hall. No recording is used.
- `quadra-strings.ttl`: eight voices of three detuned saws with a slow attack, two choruses and a phaser.
- `reverb-return.ttl`: an effect. Only the room comes out: a low cut, a reverb and a high cut, for a send. Used on the high percussion.
- `pitch-follower.ttl`: an effect. A `val:PitchTracker` holds a saw oscillator an octave below the input's pitch, and its `voiced` output opens a VCA so it is silent when the input has no pitch. Measured: a 220 Hz input gives 110.0 Hz.
- `convolver-room.ttl`: an effect. A convolution reverb: `val:Convolver` with an impulse response from `audio/ir/` (the sound of `reverb-return.ttl` given one click, so it can be swapped for a recorded room). No latency.
- `speaker-room.ttl`: an effect. A low cut, a cone resonance, a soft clip and a small room, for a drum played through a monitor and recorded again. Feed it with `valis-render -i`.

## Acoustic Latin set

Made for "Lights On, Nobody Home" (in `valis-artifacts`). Generated by `scripts/latin/circuits.py`. The aim is
instruments that read as played, not synthesised, so each is a physical model rather than a filtered oscillator.

- `nylon-guitar.ttl`: six voices. `val:Pluck` strikes a `val:StiffString`; the string's own damping makes high harmonics
  fade before low ones, and the note rings on after the key is released. Air and top-plate resonances and a
  `val:ModalBank` plate stand in for the box. Played low and picked it is a guitar, high and strummed a vihuela.
- `harp.ttl`: eight voices, a bright pluck, a long ring and a soundboard resonance.
- `guitarron.ttl`: a mono mariachi bass. A mid-hardness pluck, heavy string damping and a body resonance that lifts
  the second harmonic, which is what lets a bass be heard on small speakers.
- `trumpets.ttl`: four `val:Brass` voices (a lip-reed waveguide). Harder blowing is brighter. Vibrato starts after the note does.
- `violins.ttl`: four `val:Bow` voices behind a violin body (two `val:ModalBank` resonances and a bridge-hill lift).
  Vibrato starts after the note does. Reliable up to about 880 Hz.
- `sampled-strings.ttl`: recorded string sections (VSCO 2, CC0) as three `val:Sampler` voices, violins, violas and cellos, chosen by one Voice control through `val:AudioSelect`. Each recording loops, so a note can be held past the 9 to 15 seconds of the file, and a shared Attack and Release set all three. Tests: `tests/engine/SampledStringsTest.cpp`.
- `latin-perc.ttl`: one circuit played by note number (36 bass drum, 38 snare, 39 snare body, 41 floor tom, 49 crash,
  56 cowbell, 60 and 61 bongos, 62 conga slap, 63 and 64 congas, 69 cabasa, 70 maracas, 73 and 74 guiro, 75 claves).
  Struck sounds are a short noise burst into a `val:ModalBank` (a circular membrane, wooden bars or a plate); shaken
  sounds are `val:Shaker`.
- `dub-bass.ttl`: a heavy bass guitar for dub, from `six-string-bass.ttl` with a dark soft pick, a low pickup, more drive and no chorus.
  Play it from C1 to C3. Measured only; the neural check of it did not finish (see the dub-metal report).
- `dub-delay.ttl`: an echo return for a send: a dotted eighth at 85 bpm (529 ms) on a bucket-brigade delay, with a low cut, wet only.
- `free-bell.ttl`: eight resonances written one by one in a `val:FreeModalBank` (`val:modes`, hertz:seconds:gain), struck
  by a noise burst. The pattern for a measured object: run `fit_modes` on a recording and paste the string it returns.
- `vocal-guide.ttl`: a plain "ah" voice for a guide track. Deliberately synthetic; keep it out of the final mix.

## Nursery instruments for a 140 bpm dance track

Eight instruments, one for each job in a dance track, each built from a toy or a nursery object and not from the usual
oscillator and filter. They are written by `scripts/nursery/nursery.py` and tested by `tests/engine/NurseryInstrumentsTest.cpp`.

- `nursery-humpty-kick.ttl`: a kick. A sine and a six-mode drumhead resonator (`val:ModalBank`, mode 1) fall in pitch together, so the thump has a tuned, wooden wobble.
- `nursery-black-sheep-bass.ttl`: a mono bass that says "baa". `val:Wavetable` on its Vowels set is swept from an 'o' to an 'a' on every note; a sine keeps the fundamental and `val:OctaveDivider` adds a square sub.
- `nursery-twinkle-lead.ttl`: a mono lead. Two detuned wavetables go through `val:FreqShifter` with feedback, so the tone climbs and shimmers, and each note strikes a marimba-bar glint two octaves up.
- `nursery-incy-wincy-arp.ttl`: an arpeggio voice. A plucked, dispersed string, a sympathetic comb a fifth above, and a 321 ms echo (a dotted eighth at 140 bpm) whose repeats are shifted up an octave.
- `nursery-rock-a-bye-pad.ttl`: a pad. A root, fifth and octave sound together, and a filter rocks open and shut at 1.1667 Hz (one swing every two beats at 140 bpm), with a phaser, an octave shimmer and a six-second reverb.
- `nursery-rattle-snare.ttl`: a snare. A noise-struck membrane, a snap, and 140 colliding beads from `val:Shaker` for a rattle, bit-reduced.
- `nursery-ding-dong-hat.ttl`: a hi-hat made of two plate resonators pushed apart by a frequency shifter. Note velocity sets how long it rings, so a soft note is closed and a hard one open.
- `nursery-wind-up-riser.ttl`: a riser. One envelope speeds a guiro's scrape from 3 to 180 ticks a second, opens a noise filter with a rising frequency shifter, and lifts a saw four octaves.
- `nursery-voice-vocoder.ttl`: an effect, not an instrument. A 24-band `val:Vocoder` whose modulator is the input (a speech synthesiser) and whose carrier is built inside: five saws on A3, E4 and A4 with a veil of high noise for consonants, so the voice sings the track's chord. Written by `scripts/nursery/voice-vocoder.py`.

## Inferno Choir: a scream and a wail

- `inferno-choir.ttl`: a synthesised human scream and wail, played from MIDI notes (about A2 to A5) and four controllers. It is a
  [source-filter voice](https://en.wikipedia.org/wiki/Source%E2%80%93filter_model): three glottal-pulse oscillators for the vocal folds, a ring
  modulator at half the pitch for a screamed subharmonic, four parallel `val:StateVariable` band-pass formants for the throat (morphing
  from "ooh" to "aah"), white noise for breath, a tanh drive and wavefolder for the strained larynx, a brown-noise furnace roar and
  random crackles for the fire, then a chorus and a long hall. The vocal folds are three `val:Glottal` oscillators (a detuned pair and one an
  octave down) with a Pulse shape selector (Rosenberg by default), whose open quotient and Rd tighten as the scream grows. About 50 knobs in
  seven sections (Voice, Controllers, Pitch, Voice source, Throat, Fire, Output).
  The controllers are read by the new `val:MidiCC`: CC1 (mod wheel) wails (vibrato rate and depth), CC2 (breath) screams (pitch lift,
  higher formants, brightness, roughness and breath), CC3 opens the vowel and CC4 lights the fire (drive, fold, roar, crackle). Each is added to
  its knob (Wail, Scream, Vowel, Inferno), and the Controllers section sets how far each reaches. Written by `scripts/inferno/inferno.py`;
  tested by `tests/engine/InfernoChoirTest.cpp` (silent until played, in tune across four octaves, each controller moves what it should).
  Measured, not heard: the neural check reads the wail as "a human voice" (share 0.60, a yes; it was 0.28 with saw oscillators), and the scream
  as a distorted guitar or a synthesizer preset.

## Demonstrations of one element

Circuits whose job is to show an element, with its controls on the panel. Written by `scripts/elements-demo/`.

- `distortion-bench.ttl`: the five shaping curves side by side on a note you play (a saw by default, or a sine, square or triangle).
  A drive stage feeds `val:HardClip`, `val:SoftSine`, `val:SinArcTan`, `val:Diode` and `val:Triode`, and a Curve selector picks which
  one is heard; each curve has its own controls (threshold, gain, ideality, bias and mu), a level trim so the loudness stays put, a
  dry / wet blend, and a spectrum and waveform on the panel. Tested by `tests/engine/DistortionBenchTest.cpp`, on a sine: the symmetric
  curves give odd harmonics only, the diode and triode give even ones, and the hard clip is close to a square wave
  ([harmonics of a square wave](https://en.wikipedia.org/wiki/Square_wave)) and the brightest of the symmetric three.
- `bassman-amp.ttl`: a plucked string through the Fender '59 Bassman signal path. `val:Pluck` into `val:StiffString`, then the
  amplifier's passive treble / middle / bass network (`val:ToneStack`), a stage to make up what it loses, a valve preamp
  (`val:Triode`, with bias and mu on the panel), a soft power stage (`val:SoftSine`) and a speaker rolloff. The stack comes before the
  valve, as in the amplifier, so the tone controls change what is distorted. An envelope gates the output while idle, because a biased valve
  stage makes a start-up transient. Tested by `tests/engine/BassmanAmpTest.cpp`: in tune at E2 and E4, rings after release, each
  tone control moves its own band by 4 to 11 dB, the middle control also moves the treble (the network interacts), and driving the input
  squeezes the peaks. Measured, not heard.
- `lofi-codec.ttl`: a pad (two detuned saws and some hiss) through a spectral gate and then a codec, with the spectrum on the panel so
  the missing frequencies can be seen. The gate (`val:SpectralGate`) lifts the hiss out from between the harmonics, which a filter cannot
  do. The codec (`val:Codec`) has Bandwidth (everything above it goes), Floor (partials more than that far below the loudest go, so
  what is left warbles) and Step (the level of each bin is rounded to steps of that many decibels). With Floor and Step at 0 and
  Bandwidth at the top it passes the sound unchanged but delayed, and Codec mix puts the original back. The codec adds 44 ms of latency, which the
  host compensates for. Tested by `tests/engine/LofiCodecTest.cpp`: each control does what is said above (bandwidth and floor each take the
  energy above 4 kHz down by more than 15 dB; step puts an error of -26 dB at 2 dB steps and -10 dB at 12 dB steps against the clean sound; the gate takes
  more than 10 dB of hiss from between the harmonics and leaves the fundamental). Measured, not heard.
- `guacharaca.ttl`: the scraped, notched wooden tube of cumbia and vallenato, as its own instrument. Note 73 is the down stroke and 74 the
  up stroke; each is a `val:Shaker` with its teeth made regular, so a stroke is a run of evenly spaced ticks that thins as the energy dies.
  The Controls view has the teeth per second, length and level of each stroke, the Rasp section (Regularity, Tick length, Rasp pitch, Ring) and
  Tone (Brightness, Tube body, Level). Tested by `tests/engine/GuacharacaTest.cpp`: silent until played, the down stroke is stronger than the up,
  50 teeth per second give more than 1.5 times the ticks of 20, regularity 1 spaces the ticks more evenly than 0. Measured, not heard. It replaces
  the guiro notes in `latin-perc.ttl`, which have no controls of their own.
- `arp-quantiser.ttl`: a sixteen-step sequence that uses all twelve pitch classes, forced into a key and sent out of the plugin as MIDI
  as well as played. Two copies of a `val:StepSequencer` run the same pattern; a `val:Quantise` reads the first, and the difference
  between each note and its quantised self moves the second. A `val:NoteOut` sends the second's notes to the host. The Root and Scale
  selectors (Free, Semitones, Octaves, Fifths, Major, Minor, Pentatonic) change the key while it plays, and Step length, Gate length
  and Transpose belong to the sequence. Like any sequencer circuit it plays whenever the plugin runs, from the host's musical position or
  free-running at the last tempo. Tested by `tests/engine/ArpQuantiserTest.cpp`: for five scales and three roots every note sent is in
  the scale, each note-off ends its note-on, the channel and transpose are honoured, and `valis-render --midi-out` writes the same
  thirty-two notes to a file, a step apart. Measured, not heard.
- `vocoder-speak.ttl`: a talk box. A note played on MIDI is a sawtooth (two, slightly apart, with high-passed hiss for the consonants)
  and the voice on the plugin's audio input shapes it, two ways that a Method selector switches between on the same voice and note:
  `val:LPC`, which fits one smooth curve to the voice's spectrum (the LPC order says how much of the vowel survives), and `val:Vocoder`,
  a bank of bands (how many, and how quickly each follows), and the plain carrier for comparison. Presence lifts the voice above
  2.5 kHz first. Silent unless a note is held and a voice is present. Tested by `tests/engine/VocoderSpeakTest.cpp` with a synthetic
  voice (a pulse train through the formants of "ah" and of "ee"): the balance between the bands near 2.3 kHz and 730 Hz moves by 12.5 dB
  under LPC and 21 dB under the vocoder when the voice changes from "ah" to "ee", and by 0.0 dB with the plain carrier. Intelligibility
  of real speech is not measured; whether it is understood is for a listener.

## Not examples

- `temp.ttl` is a variant of `subsynth.ttl` with the same title.
