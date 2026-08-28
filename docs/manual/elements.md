# Element reference

Generated from `vocabs/valis.ttl` by `valis-render --elements`.
Do not edit by hand; run `scripts/generate-docs.sh` instead.

The Filter/Transfer split is memory versus no memory. Linearity is a
separate property: a ladder filter has memory and is nonlinear.

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

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `attack` | Attack | 10 | 0.1 to 5000 | ms |
| `decay` | Decay | 200 | 0.1 to 5000 | ms |
| `sustain` | Sustain | 0.7 | 0 to 1 |  |
| `release` | Release | 300 | 0.1 to 10000 | ms |

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

## val:Fold

Nonlinear.

**Audio in**: `in`

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `gain` | Gain | 1 | 0.1 to 20 |  |

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

## val:Mixer

Linear.

**Audio in**: `in`

**Audio out**: `out`

## val:Noise

Nonlinear.

**Audio out**: `out`

**Controls**

| symbol | name | default | range | unit |
|---|---|---|---|---|
| `colour` | Colour | 0 | 0 to 1 |  |

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

## val:Output

Linear.

**Audio in**: `in`

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

## val:UnitDelay

Linear.

**Audio in**: `in`

**Audio out**: `out`

