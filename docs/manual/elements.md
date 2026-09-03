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

## val:ControlMultiply

Linear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `a` | A | 0 | -1e+06 to 1e+06 |  |
| `b` | B | 0 | -1e+06 to 1e+06 |  |

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

**Audio out**: `out`

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

## val:Reed

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `frequency` | Frequency | 220 | 20 to 5000 | Hz |
| `pressure` | Pressure | 0.5 | 0 to 1 |  |
| `stiffness` | Stiffness | 0.5 | 0 to 1 |  |
| `damping` | Damping | 0.2 | 0 to 1 |  |

## val:Scale

Linear.

**Control out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `in` | In | 0 | 0 to 1 |  |
| `min` | Min | 0 | -1e+06 to 1e+06 |  |
| `max` | Max | 1 | -1e+06 to 1e+06 |  |

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

## val:StateVariable

Linear.

**Audio in**: `in`

**Audio out**: `lp`, `bp`, `hp`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `cutoff` | Cutoff | 1000 | 20 to 20000 | Hz |
| `resonance` | Resonance | 0.7071 | 0.05 to 20 |  |

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

