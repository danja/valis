# Case study: Klon Centaur

The Klon Centaur (1994) is a guitar overdrive pedal whose sound is dominated
by three things: **asymmetric clipping**, a **clean-signal blend**, and a
**treble-cut tone control**. The full Turtle is at `examples/klon.ttl`.

## Signal path

```
Input ──► preGain (Gain, drive) ──► AsymClip ──► DryWet ──► OnePole ──► Gain ──► Output
                                                    ▲
Input ─────────────────────────── dry ─────────────┘
```

The same guitar signal reaches two places: the clipping chain and the DryWet
`dry` port. Fan-out (one output to multiple destinations) is always fine in
Valis; it is fan-in onto a non-Mixer input that is flagged as an error.

## The asymmetric clipper

`val:AsymClip` models two diodes in antiparallel with different forward
voltages - a positive threshold and a negative one. The default values
approximate the Centaur's original component values:

| Direction | Component | Forward voltage |
|---|---|---|
| Positive | BAT41 Schottky × 1 | 0.3 V |
| Negative | 1N34A Germanium × 2 | 0.4 V (two in series) |

The result is that positive peaks clip earlier than negative ones, producing a
lopsided waveform with stronger even harmonics - the "warmth" commonly
attributed to the pedal.

```turtle
:clip a val:AsymClip ;
    val:drive  1.0 ;
    val:posVf  0.3 ;
    val:negVf  0.4 .
```

`val:drive` scales the signal before clipping. The `preGain` element ahead of
it is set to 20 dB to bring a typical guitar signal (~100 mV peak) up to the
clipping range (~1 V). Increase `drive` for heavier saturation; decrease it
to back off the clipping without losing level.

## The clean blend

`val:DryWet` mixes the unprocessed input (`dry`) with the clipped output
(`wet`). At `mix=0` the pedal is fully clean; at `mix=1` it is fully clipped.
The Centaur sits around `mix=0.5`.

```turtle
:blend a val:DryWet ; val:mix 0.5 .
```

This blend is what distinguishes the Centaur from a pure overdrive - the clean
signal preserves pick attack and low-end definition that would otherwise be
compressed away by the clipping stage.

## Tone control

The original uses a passive treble-cut pot. `val:OnePole` in lowpass mode
approximates this: at full cutoff (8 kHz) the signal is essentially flat; lower
the cutoff to reduce high-frequency fizz.

```turtle
:tone a val:OnePole ; val:cutoff 8000.0 ; val:mode 0 .   # 0 = lowpass
```

## Modifying the circuit

**Harder clipping:** replace `val:AsymClip` with `val:DiodePair` for a symmetric
silicon pair, or with `val:HardClip` for the most aggressive square-wave style.

**Symmetric clipping:** set `val:posVf` and `val:negVf` to the same value.

**Richer saturation:** insert a `val:Tanh` with `val:antialiasing val:ADAA2`
between `preGain` and `clip` to add tube-like compression before the diode stage.

**Stereo:** duplicate the chain for a second channel; the Input and Output
elements each carry one mono bus in the current engine.

## Loading the circuit

Paste `examples/klon.ttl` into the Turtle editor tab or use **File → Load
Circuit…**. Six parameters are exposed: Drive, Positive Vf, Negative Vf, Blend,
Tone and Volume.
