# Case study: clarinet

`val:Reed` is a cylindrical bore, stopped by the mouthpiece at one end and open
at the other, driven by a pressure-controlled valve. It is the same machinery as
[the flute](flute.md) with the opposite boundary condition, and almost
everything that distinguishes the two instruments follows from that one
difference. The full Turtle is at `examples/clarinet.ttl`.

## What a stopped bore does

A wave travelling down the tube reflects at the open end with its sign inverted,
and at the closed end without. One round trip therefore inverts once, so the
tube supports only those frequencies that fit an odd number of quarter
wavelengths into it.

Three familiar facts come straight out of that:

- **The even harmonics are missing.** Measured at D4, the third harmonic comes
  out at 0.33 of the fundamental and the fifth at 0.19, while the second, fourth
  and sixth are all under 0.005. That hollow, woody tone is not a filter setting;
  it is the geometry.
- **A clarinet sounds an octave below a flute of the same length**, because a
  quarter wavelength fits where the flute needs a half.
- **It overblows to the twelfth**, not the octave: the next mode up is three
  times the fundamental, not twice.

`tests/dsp/ClarinetTest.cpp` measures the first of those directly, and compares
it against `val:Flute` at the same pitch: 0.005 against 0.144, a factor of
thirty, from nothing but the boundary condition.

## The reed

The reed is a valve that the pressure across it closes. The bore's returning
pressure works against the player's, and the reed's opening is very nearly a
straight line in that difference until it slams against the lay and shuts.
Clipped like that, it is the whole nonlinearity of the instrument, and where the
cycle spends its time against the limit is what decides the tone.

That gives a reed a band of mouth pressure it works over, and the two ends of
the band are always a factor of two apart whatever the reed is like. Below the
band nothing sounds. Above it the reed is held shut and nothing sounds either,
which is what happens when a beginner blows too hard. The `pressure` control is
mapped onto that band, so most of its travel plays while it still passes through
zero, and an unblown instrument is silent.

`stiffness` moves the band rather than the tone:

| Reed | At very little air | Blown hard |
|---|---|---|
| Soft (`stiffness` 0) | speaks | chokes |
| Hard (`stiffness` 1) | will not start | takes everything it is given |

That is what a player feels when they change reeds, and it is measured in
`testStiffnessChangesWhatItTakesToPlay`.

## Tuning

A waveguide plays at a pitch set by its total loop delay, and the loss filter in
the loop is part of that delay. Its phase delay is known in closed form and is
taken out of the delay line exactly, which is what stops the `damping` control
pulling the instrument flat as it is turned up: measured across the whole range,
the pitch moves by six cents while the third harmonic falls from 0.33 to 0.19.

What is left over measures as a straight line in frequency, about 0.079 cents
per hertz, and is taken out by a calibration fitted to measurements at eight
pitches. Rendering MIDI notes through the engine:

| Note | Sounded | Error |
|---|---|---|
| D3 146.83 | 146.79 | −0.5 cents |
| D4 293.66 | 294.48 | +4.8 cents |
| D5 587.33 | 585.37 | −5.8 cents |

The upper register comes out purer than the lower one, which is also true of the
instrument: at D5 the third harmonic is 0.18 where at D3 it is 0.34.

## The circuit

```
MidiPitch ─────────────────────► Reed.frequency
Envelope ──► blowScale ────────► Reed.pressure
Envelope ──────────────────────► VCA.cv

Reed ──► OnePole (bell) ──► VCA ──► Output
```

The envelope drives blowing pressure as well as the amplifier, because pressure
is not a volume control: the Blow parameter sets how hard the note is played and
the envelope's attack is the time the player takes to get there. The one-pole
after the reed stands for the bell, which radiates the high partials more
readily than the low ones.
