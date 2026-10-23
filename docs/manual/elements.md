# Element reference

Generated from `vocabs/valis.ttl` by `valis-render --elements`.
Do not edit by hand; run `scripts/generate-docs.sh` instead.

The Filter/Transfer split is memory versus no memory. Linearity is a
separate property: a ladder filter has memory and is nonlinear.

## val:AsymClip

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `drive` | Drive | 1 | 0.1 to 100 |  |
| `posVf` | Positive Vf | 0.3 | 0.01 to 2 | V |
| `negVf` | Negative Vf | 0.6 | 0.01 to 2 | V |

## val:Biquad

Linear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `mode` | Mode | 1 | 0 to 5 |  |
| `frequency` | Frequency | 1000 | 20 to 20000 | Hz |
| `gain` | Gain | 0 | -24 to 24 | dB |
| `q` | Q | 0.7071 | 0.1 to 20 |  |

## val:Bow

Nonlinear.

**Audio in**: `drive`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 220 | 20 to 4000 | Hz |
| `pressure` | Pressure | 0.5 | 0 to 1 |  |
| `velocity` | Velocity | 0.4 | 0 to 1 |  |
| `position` | Position | 0.12 | 0.02 to 0.5 |  |
| `damping` | Damping | 0.3 | 0 to 1 |  |

## val:Choke

Nonlinear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `gate` | Gate | 0 | 0 to 1 |  |
| `choke` | Choke | 0 | 0 to 1 |  |

## val:Chorus

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `rate` | Rate | 0.5 | 0.01 to 10 | Hz |
| `depth` | Depth | 2 | 0 to 3.5 | ms |
| `mix` | Mix | 0.5 | 0 to 1 |  |

## val:CombFilter

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 220 | 10 to 20000 | Hz |
| `feedback` | Feedback | 0.95 | 0 to 0.99 |  |
| `damping` | Damping | 0.1 | 0 to 1 |  |

## val:Compressor

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `threshold` | Threshold | -6 | -60 to 0 | dB |
| `ratio` | Ratio | 4 | 1 to 40 |  |
| `knee` | Knee | 6 | 0 to 24 | dB |
| `attack` | Attack | 5 | 0.01 to 500 | ms |
| `release` | Release | 100 | 1 to 5000 | ms |
| `upward` | Upward | 0 | 0 to 1 |  |
| `boost` | Max boost | 18 | 0 to 60 | dB |

## val:ControlMultiply

Linear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `a` | A | 0 | -1e+06 to 1e+06 |  |
| `b` | B | 0 | -1e+06 to 1e+06 |  |

## val:Crossover

Linear.

**Audio in**: `in`

**Audio out**: `low`, `high`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 1000 | 20 to 20000 | Hz |

## val:Delay

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `time` | Time | 250 | 0 to 5000 | ms |
| `feedback` | Feedback | 0 | 0 to 0.99 |  |

## val:Diode

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `saturationCurrent` | Is | 2.52e-09 | 1e-15 to 0.001 | A |
| `emissionCoefficient` | n | 1.752 | 1 to 2 |  |
| `thermalVoltage` | Vt | 0.02585 | 0.02 to 0.03 | V |

## val:DiodePair

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `saturationCurrent` | Is | 2.52e-09 | 1e-15 to 0.001 |  |
| `emissionCoefficient` | n | 1.752 | 1 to 2 |  |
| `seriesResistance` | Rs | 1000 | 1 to 1e+06 | ohm |

## val:Drift

Nonlinear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `speed` | Speed | 1.5 | 0.01 to 100 |  |
| `amount` | Amount | 1 | 0 to 1000 |  |
| `centre` | Centre | 0 | -1e+06 to 1e+06 |  |
| `seed` | Seed | 1 | 0 to 65535 |  |

## val:DryWet

Linear.

**Audio in**: `dry`, `wet`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `mix` | Mix | 1 | 0 to 1 |  |

## val:Envelope

Nonlinear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `attack` | Attack | 10 | 0.1 to 5000 | ms |
| `decay` | Decay | 200 | 0.1 to 5000 | ms |
| `sustain` | Sustain | 0.7 | 0 to 1 |  |
| `release` | Release | 300 | 0.1 to 10000 | ms |
| `gate` | Gate | -1 | -1 to 1 |  |

## val:EnvelopeFollower

Nonlinear.

**Audio in**: `in`

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `attack` | Attack | 1 | 0.01 to 1000 | ms |
| `release` | Release | 100 | 0.1 to 5000 | ms |
| `mode` | Mode | 0 | 0 to 1 |  |

## val:Expander

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `amount` | Amount | 0 | 0 to 1 |  |
| `threshold` | Threshold | -60 | -140 to 0 | dB |
| `ratio` | Ratio | 2 | 1 to 20 |  |

## val:FMMatrix

FM matrix. Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `m45` | 5 to 4 | 0 | 0 to 1 |  |
| `m46` | 6 to 4 | 0 | 0 to 1 |  |
| `m51` | 1 to 5 | 0 | 0 to 1 |  |
| `m52` | 2 to 5 | 0 | 0 to 1 |  |
| `m53` | 3 to 5 | 0 | 0 to 1 |  |
| `m54` | 4 to 5 | 0 | 0 to 1 |  |
| `m55` | Feedback 5 | 0 | 0 to 7 |  |
| `m56` | 6 to 5 | 0 | 0 to 1 |  |
| `m61` | 1 to 6 | 0 | 0 to 1 |  |
| `m62` | 2 to 6 | 0 | 0 to 1 |  |
| `m63` | 3 to 6 | 0 | 0 to 1 |  |
| `m64` | 4 to 6 | 0 | 0 to 1 |  |
| `m65` | 5 to 6 | 0 | 0 to 1 |  |
| `m66` | Feedback 6 | 0 | 0 to 7 |  |
| `frequency` | Frequency | 440 | 0.01 to 20000 | Hz |
| `depth` | Depth | 12.5664 | 0 to 50 |  |
| `ratio1` | Ratio 1 | 1 | 0 to 32 |  |
| `detune1` | Detune 1 | 0 | -100 to 100 | ct |
| `level1` | Level 1 | 99 | 0 to 99 |  |
| `amp1` | Amp 1 | 1 | 0 to 1 |  |
| `carrier1` | Carrier 1 | 0 | 0 to 1 |  |
| `ratio2` | Ratio 2 | 1 | 0 to 32 |  |
| `detune2` | Detune 2 | 0 | -100 to 100 | ct |
| `level2` | Level 2 | 99 | 0 to 99 |  |
| `amp2` | Amp 2 | 1 | 0 to 1 |  |
| `carrier2` | Carrier 2 | 0 | 0 to 1 |  |
| `ratio3` | Ratio 3 | 1 | 0 to 32 |  |
| `detune3` | Detune 3 | 0 | -100 to 100 | ct |
| `level3` | Level 3 | 99 | 0 to 99 |  |
| `amp3` | Amp 3 | 1 | 0 to 1 |  |
| `carrier3` | Carrier 3 | 0 | 0 to 1 |  |
| `ratio4` | Ratio 4 | 1 | 0 to 32 |  |
| `detune4` | Detune 4 | 0 | -100 to 100 | ct |
| `level4` | Level 4 | 99 | 0 to 99 |  |
| `amp4` | Amp 4 | 1 | 0 to 1 |  |
| `carrier4` | Carrier 4 | 0 | 0 to 1 |  |
| `ratio5` | Ratio 5 | 1 | 0 to 32 |  |
| `detune5` | Detune 5 | 0 | -100 to 100 | ct |
| `level5` | Level 5 | 99 | 0 to 99 |  |
| `amp5` | Amp 5 | 1 | 0 to 1 |  |
| `carrier5` | Carrier 5 | 0 | 0 to 1 |  |
| `ratio6` | Ratio 6 | 1 | 0 to 32 |  |
| `detune6` | Detune 6 | 0 | -100 to 100 | ct |
| `level6` | Level 6 | 99 | 0 to 99 |  |
| `amp6` | Amp 6 | 1 | 0 to 1 |  |
| `carrier6` | Carrier 6 | 0 | 0 to 1 |  |
| `m11` | Feedback 1 | 0 | 0 to 7 |  |
| `m12` | 2 to 1 | 0 | 0 to 1 |  |
| `m13` | 3 to 1 | 0 | 0 to 1 |  |
| `m14` | 4 to 1 | 0 | 0 to 1 |  |
| `m15` | 5 to 1 | 0 | 0 to 1 |  |
| `m16` | 6 to 1 | 0 | 0 to 1 |  |
| `m21` | 1 to 2 | 0 | 0 to 1 |  |
| `m22` | Feedback 2 | 0 | 0 to 7 |  |
| `m23` | 3 to 2 | 0 | 0 to 1 |  |
| `m24` | 4 to 2 | 0 | 0 to 1 |  |
| `m25` | 5 to 2 | 0 | 0 to 1 |  |
| `m26` | 6 to 2 | 0 | 0 to 1 |  |
| `m31` | 1 to 3 | 0 | 0 to 1 |  |
| `m32` | 2 to 3 | 0 | 0 to 1 |  |
| `m33` | Feedback 3 | 0 | 0 to 7 |  |
| `m34` | 4 to 3 | 0 | 0 to 1 |  |
| `m35` | 5 to 3 | 0 | 0 to 1 |  |
| `m36` | 6 to 3 | 0 | 0 to 1 |  |
| `m41` | 1 to 4 | 0 | 0 to 1 |  |
| `m42` | 2 to 4 | 0 | 0 to 1 |  |
| `m43` | 3 to 4 | 0 | 0 to 1 |  |
| `m44` | Feedback 4 | 0 | 0 to 7 |  |

## val:FMOperator

FM operator. Nonlinear.

**Audio in**: `pm`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 440 | 0.01 to 20000 | Hz |
| `ratio` | Ratio | 1 | 0 to 32 |  |
| `fixed` | Fixed | 0 | 0 to 10000 | Hz |
| `detune` | Detune | 0 | -100 to 100 | ct |
| `level` | Level | 99 | 0 to 99 |  |
| `amp` | Amp | 1 | 0 to 1 |  |
| `depth` | Depth | 12.5664 | 0 to 50 |  |
| `feedback` | Feedback | 0 | 0 to 7 |  |

## val:Flanger

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `rate` | Rate | 0.5 | 0.01 to 10 | Hz |
| `depth` | Depth | 2 | 0 to 4 | ms |
| `feedback` | Feedback | 0.5 | -0.95 to 0.95 |  |
| `mix` | Mix | 0.5 | 0 to 1 |  |

## val:Flute

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 440 | 20 to 5000 | Hz |
| `pressure` | Pressure | 0 | 0 to 1 |  |
| `offset` | Offset | 0.65 | 0 to 1 |  |
| `breath` | Breath | 0.05 | 0 to 1 |  |
| `jet` | Jet | 0.5 | 0.35 to 0.65 |  |
| `damping` | Damping | 0.3 | 0 to 1 |  |

## val:Fold

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `gain` | Gain | 1 | 0.1 to 20 |  |

## val:FreqAnalyzer

Linear.

**Audio in**: `in`

**Audio out**: `out`

**Control out**: `low`, `mid`, `high`, `centroid`

## val:Gain

Linear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `gain` | Gain | 0 | -60 to 24 | dB |

## val:Granulator

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`, `left`, `right`

**Control out**: `grains`, `onset`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `position` | Position | 0 | 0 to 1 |  |
| `size` | Size | 120 | 1 to 2000 | ms |
| `density` | Density | 20 | 0.01 to 200 | Hz |
| `pitch` | Pitch | 0 | -48 to 48 |  |
| `spray` | Spray | 0 | 0 to 1 |  |
| `jitter` | Jitter | 0 | 0 to 1 |  |
| `pitchJitter` | Pitch Jitter | 0 | 0 to 24 |  |
| `shape` | Shape | 0.5 | 0 to 1 |  |
| `spread` | Spread | 0.5 | 0 to 1 |  |
| `reverse` | Reverse | 0 | 0 to 1 |  |
| `scan` | Scan | 0 | -4 to 4 |  |
| `freeze` | Freeze | 0 | 0 to 1 |  |
| `trigger` | Trigger | -1 | -1 to 1 |  |
| `sizeJitter` | Size jitter | 0 | 0 to 1 |  |
| `curve` | Curve | 0 | 0 to 1 |  |
| `range` | Range | 0 | 0 to 24 |  |
| `direction` | Direction | 2 | 0 to 2 |  |
| `scale` | Scale | 1 | 0 to 6 |  |
| `seed` | Seed | 0 | 0 to 65535 |  |

## val:HardClip

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `threshold` | Threshold | 1 | 0.01 to 2 |  |

## val:Input

Linear.

**Audio out**: `out`, `left`, `right`

## val:LFO

Nonlinear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `rate` | Rate | 2 | 0.01 to 100 | Hz |
| `shape` | Shape | 0 | 0 to 4 |  |

## val:Ladder

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `cutoff` | Cutoff | 1000 | 20 to 20000 | Hz |
| `resonance` | Resonance | 0 | 0 to 1 |  |
| `drive` | Drive | 1 | 1 to 20 |  |

## val:Limiter

Nonlinear.

**Audio in**: `in`, `link`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `ceiling` | Ceiling | -0.3 | -24 to 0 | dB |
| `release` | Release | 60 | 1 to 1000 | ms |

## val:MidiInterval

Nonlinear.

**Control out**: `semitones`, `ratio`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `root` | Root | 60 | 0 to 127 |  |

## val:MidiPitch

Nonlinear.

**Control out**: `out`

## val:MidiVelocity

Nonlinear.

**Control out**: `out`

## val:Mixer

Linear.

**Audio in**: `in`, `left`, `right`

**Audio out**: `out`, `left`, `right`

## val:ModalBank

Linear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 220 | 20 to 5000 | Hz |
| `decay` | Decay | 1 | 0.05 to 10 | s |
| `brightness` | Brightness | 0.5 | 0 to 1 |  |
| `mode` | Mode | 0 | 0 to 3 |  |

## val:NamModel

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `input` | Input | 0 | -24 to 24 | dB |
| `output` | Output | 0 | -24 to 24 | dB |
| `bypass` | Bypass | 0 | 0 to 1 |  |

## val:Noise

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `colour` | Colour | 0 | 0 to 1 |  |

## val:NoteGate

Nonlinear.

**Control out**: `gate`, `velocity`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `note` | Note | 60 | 0 to 127 |  |

## val:NoteOut

Nonlinear.

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `gate` | Gate | 0 | 0 to 1 |  |
| `pitch` | Pitch | 440 | 8 to 12544 | Hz |
| `velocity` | Velocity | 0.8 | 0 to 1 |  |
| `channel` | Channel | 1 | 1 to 16 |  |

## val:OnePole

Linear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `cutoff` | Cutoff | 1000 | 20 to 20000 | Hz |
| `mode` | Mode | 0 | 0 to 2 |  |

## val:Oscillator

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 440 | 0.01 to 20000 | Hz |
| `shape` | Shape | 0 | 0 to 3 |  |

## val:Oscilloscope

Linear.

**Audio in**: `in`

**Audio out**: `out`

**Control out**: `peak`, `rms`, `frequency`

## val:Output

Linear.

**Audio in**: `in`, `left`, `right`

## val:Pan

Linear.

**Audio in**: `in`

**Audio out**: `out`, `left`, `right`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `pan` | Pan | 0 | -1 to 1 |  |

## val:Phaser

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `rate` | Rate | 0.5 | 0.01 to 10 | Hz |
| `depth` | Depth | 0.7 | 0 to 1 |  |
| `feedback` | Feedback | 0.5 | 0 to 0.9 |  |
| `mix` | Mix | 0.5 | 0 to 1 |  |

## val:PitchShift

Linear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `pitch` | Pitch | 0 | -24 to 24 |  |
| `mix` | Mix | 1 | 0 to 1 |  |

## val:Quantise

Nonlinear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `in` | In | 0 | -96 to 96 |  |
| `root` | Root | 0 | 0 to 11 |  |
| `scale` | Scale | 4 | 0 to 6 |  |

## val:RateLevelEnvelope

Rate and level envelope. Nonlinear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `r1` | Rate 1 | 99 | 0 to 99 |  |
| `r2` | Rate 2 | 99 | 0 to 99 |  |
| `r3` | Rate 3 | 99 | 0 to 99 |  |
| `r4` | Rate 4 | 99 | 0 to 99 |  |
| `l1` | Level 1 | 99 | 0 to 99 |  |
| `l2` | Level 2 | 99 | 0 to 99 |  |
| `l3` | Level 3 | 99 | 0 to 99 |  |
| `l4` | Level 4 | 0 | 0 to 99 |  |
| `velocity` | Velocity | 1 | 0 to 1 |  |
| `velsens` | Velocity sensitivity | 0 | 0 to 7 |  |
| `gate` | Gate | -1 | -1 to 1 |  |

## val:Redux

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `bits` | Bits | 8 | 1 to 24 |  |
| `downsample` | Downsample | 4 | 1 to 64 |  |
| `jitter` | Jitter | 0 | 0 to 1 |  |

## val:Reed

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 220 | 20 to 5000 | Hz |
| `pressure` | Pressure | 0.5 | 0 to 1 |  |
| `stiffness` | Stiffness | 0.5 | 0 to 1 |  |
| `breath` | Breath | 0.02 | 0 to 1 |  |
| `damping` | Damping | 0.2 | 0 to 1 |  |

## val:Reverb

Linear.

**Audio in**: `in`

**Audio out**: `out`, `right`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `size` | Size | 1 | 0.1 to 2 |  |
| `decay` | Decay | 2 | 0.1 to 20 | s |
| `damping` | Damping | 0.3 | 0 to 1 |  |
| `mix` | Mix | 0.3 | 0 to 1 |  |

## val:SampleLoad

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `speed` | Speed | 1 | -4 to 4 |  |
| `start` | Start | 0 | 0 to 1 |  |
| `loop` | Loop | 1 | 0 to 1 |  |
| `trigger` | Trigger | -1 | -1 to 1 |  |

## val:Scale

Linear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `in` | In | 0 | 0 to 1 |  |
| `min` | Min | 0 | -1e+06 to 1e+06 |  |
| `max` | Max | 1 | -1e+06 to 1e+06 |  |

## val:Select

Linear.

**Control out**: `out`, `thru`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `a` | A | 0 | -1e+06 to 1e+06 |  |
| `b` | B | 1 | -1e+06 to 1e+06 |  |
| `select` | Select | 0 | 0 to 1 |  |

## val:SignalGenerator

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 440 | 1 to 20000 | Hz |
| `amplitude` | Amplitude | 0.5 | 0 to 1 |  |
| `shape` | Shape | 0 | 0 to 5 |  |

## val:SinArcTan

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `gain` | Gain | 1 | 0.1 to 100 |  |

## val:SoftSine

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `gain` | Gain | 1 | 0.1 to 100 |  |

## val:SpectralGate

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `threshold` | Threshold | -60 | -120 to 0 | dB |

## val:StateVariable

Linear.

**Audio in**: `in`

**Audio out**: `lp`, `bp`, `hp`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `cutoff` | Cutoff | 1000 | 20 to 20000 | Hz |
| `resonance` | Resonance | 0.7071 | 0.05 to 20 |  |
| `drive` | Drive | 1 | 1 to 20 |  |

## val:StiffString

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 220 | 10 to 20000 | Hz |
| `feedback` | Feedback | 0.95 | 0 to 0.99 |  |
| `damping` | Damping | 0.1 | 0 to 1 |  |
| `dispersion` | Dispersion | 0.1 | 0 to 1 |  |

## val:Tanh

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `gain` | Gain | 1 | 0.1 to 100 |  |

## val:Transport

Nonlinear.

**Control out**: `playing`, `tempo`, `phase`, `trigger`, `rate`, `period`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `division` | Division | 1 | 0.015625 to 64 |  |

## val:Triode

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `mu` | mu | 100 | 1 to 250 |  |
| `bias` | Bias | -1.5 | -10 to 0 | V |

## val:TwinTBridge

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 55 | 10 to 5000 | Hz |
| `decay` | Decay | 500 | 1 to 5000 | ms |
| `trigger` | Trigger | -1 | -1 to 1 |  |
| `velocity` | Velocity | -1 | -1 to 1 |  |

## val:UnitDelay

Linear.

**Audio in**: `in`

**Audio out**: `out`

## val:VCA

Linear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `cv` | CV | 1 | 0 to 1 |  |

## val:Wavetable

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `position` | Position | 0 | 0 to 1 |  |
| `set` | Set | 0 | 0 to 2 |  |
| `frequency` | Frequency | 220 | 0.01 to 20000 | Hz |

