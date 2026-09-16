# Case study: flute

A flute is a tube open at both ends, blown by a ribbon of air aimed at a sharp
edge across the mouth hole. `val:Flute` models exactly that, and everything
about the tone comes out of the physics rather than from filtering an
oscillator. The full Turtle is at `examples/flute.ttl`.

## What the model is

Two delay lines and a nonlinearity:

```
                 ┌─────────── air column (delay) ◄──── jet flow
                 │                                        ▲
                 ▼                                        │
            loss filter ──► DC blocker ──┬──► jet delay ──┘
                                         │    (the gap the jet crosses)
                                         └──► straight back down the tube
```

The air column is a delay line a wavelength long, open at both ends, so a round
trip inverts twice and comes back the way it left. That is why a flute supports
every harmonic where a clarinet, closed at the mouthpiece, supports only the odd
ones. `val:Reed` is the same idea with the other boundary condition.

The jet takes time to cross the mouth hole, which is the second delay line. What
the tube pushes back deflects the jet, and where the jet lands decides how much
air goes into the tube rather than past it:

    flow = Uj · tanh((η − y0) / b)

`Uj` is the jet's speed, `η` how far the acoustic field has deflected it, `y0`
how far it already sits off the edge and `b` its width. After Verge and Fabre;
see [Physical Audio Signal
Processing](https://ccrma.stanford.edu/~jos/pasp/Flutes_Recorders_Flue_Organ.html).

Two things fall out of that expression, and both are audible.

## Blowing harder, not just louder

`Uj` goes with the square root of blowing pressure, which is Bernoulli's. The
instrument does not speak at all below a threshold, and above it grows both
louder and brighter, because a faster jet drives the nonlinearity further.
Measured between the softest and loudest settings, the level rises by a factor
of a hundred and the third harmonic by a factor of four.

This is why `examples/flute.ttl` sends the envelope to blowing pressure as well
as to the amplifier. A flute's loudness is not a volume control.

## The offset is the timbre

`y0` breaks the symmetry, and it is the most important control on the element.

A jet centred on the edge is deflected equally either way. The flow is then an
odd function of the deflection, and an odd function produces only odd harmonics:
a strong fundamental, a strong third, a strong fifth, and nothing in between.
That is a stopped pipe. It is a real sound, but it is a panpipe's, not a flute's.

Moving the jet off centre makes the flow asymmetric, and asymmetry is what puts
the even harmonics in. At A4 played softly, `val:offset` at 0 gives a second
harmonic of 0.0004 of the fundamental; at 0.7 it gives 0.16, above the third at
0.11. The second harmonic leading is what a flute sounds like.

A player does this by rolling the instrument towards or away from themselves. It
is the first control to reach for.

| Control | What it is |
|---|---|
| `pressure` | Blowing pressure. Below about a third of the travel the instrument does not speak and only wind noise is heard. |
| `offset` | How far the jet sits off the edge. 0 is centred and hollow; around two thirds of the way up is where a flute sits. |
| `jet` | How long the jet takes to cross the mouth hole, as a fraction of the air column. The column is retuned to match, so the pitch holds while the tone changes. |
| `breath` | Turbulence in the breath. It is also what starts the note. |
| `damping` | What the air column loses on each round trip. A metal tube against a wooden one. |

## Two voicings

The same element, four controls apart:

| | Breath | Damping | Offset | Jet |
|---|---|---|---|---|
| **Orchestral** | 0.04 | 0.25 | 0.7 | 0.5 |
| **Bamboo** | 0.45 | 0.7 | 0.45 | 0.42 |

The bamboo setting is a shakuhachi or a bansuri: soft walls that lose the upper
partials, a great deal of air in the sound, and a jet nearer the centre of the
edge, which leaves the tone hollower. Both are in the header of
`examples/flute.ttl`, and every one of those controls is a host parameter.

## Tuning

A waveguide plays at a pitch set by its total loop delay, and three things in
this loop are not the delay lines: the loss filter, the jet's own response, and
the sample the loop spends outside them. The first is taken out exactly, because
a one-pole filter's phase delay is known in closed form, which is what stops the
damping control from detuning the instrument.

The rest cannot be derived, because the loop is nonlinear and where it settles
is not something the delay lengths alone predict. They are calibrated instead:
the sounding pitch was measured across the jet range and across the playing
range, and a curve fitted to each. Measured end to end, rendering MIDI notes
through the engine:

| Note | Sounded | Error |
|---|---|---|
| D4 293.66 | 294.48 | +4.8 cents |
| A4 440.00 | 440.37 | +1.4 cents |
| E5 659.26 | 657.53 | −4.5 cents |

`tests/dsp/FluteTest.cpp` measures the tuning at eight pitches, across the
embouchure range and across the damping range, so a change that breaks a
calibration fails the build rather than going quietly out of tune.

## Starting a note

A waveguide instrument has no oscillator to start. It begins from the turbulence
in its own breath and takes a moment for the oscillation to build, exactly as a
real one does, which is why the envelope in the example has a slow attack. A
test that looked at the first block of audio would see silence and conclude the
element was broken.
