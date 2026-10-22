# FM synthesis: the DX7 way

The Yamaha DX7 made its sounds by wiring six sine oscillators into each other.
Valis has the two parts that needs, and the wiring is written in the circuit like
any other arc. Three examples show it: `examples/dx7-epiano.ttl`,
`examples/dx7-bass.ttl` and `examples/dx7-bells.ttl`.

Background: [frequency modulation synthesis](https://en.wikipedia.org/wiki/Frequency_modulation_synthesis)
and the [DX7](https://en.wikipedia.org/wiki/Yamaha_DX7).

## The two elements

**`val:FMOperator`** is a sine oscillator whose phase is driven by an audio input,
`pm`. The signal on `pm` is the modulator. How far it bends the phase is its
amplitude times `val:depth`, called the modulation index. An operator with nothing
on `pm` is a plain sine, which is how a carrier is made. Several arcs on `pm` are
summed.

Strictly this is phase modulation rather than frequency modulation, and so is the
DX7. The two produce the same sidebands, and phase modulation does not drift in
pitch.

| port | meaning |
|---|---|
| `frequency` | connect `val:MidiPitch` here |
| `ratio` | multiplies the frequency. A whole number is a harmonic of the note; anything else is inharmonic |
| `fixed` | above 0, ignores the keyboard and sounds this many hertz |
| `detune` | in cents |
| `level` | output level on the DX7's 0 to 99 scale |
| `amp` | 0 to 1, for an envelope to drive |
| `depth` | radians of phase shift at full scale on `pm` |
| `feedback` | 0 to 7, the operator's own output fed back into its phase |

**`val:RateLevelEnvelope`** is the DX7's envelope. On key down the level goes to
`l1` at rate `r1`, then to `l2` at `r2`, then to `l3` at `r3`, where it stays until
the key comes up. Then it goes to `l4` at `r4`. Levels and rates are the DX7's 0 to
99. Levels are linear in decibels, so each step down is about 0.75 dB. A rate is a
speed, so a segment takes longer the further it has to travel.

Connect its `out` to an operator's `amp`. On a carrier that is the loudness. On a
modulator it is the modulation index, so a modulator whose envelope falls makes a
note that starts bright and softens, which is most of what FM sounds like.

`velocity` and `velsens` make harder playing louder. Connect `val:MidiVelocity` to
`velocity`. The modulators usually take more of it than the carriers, so a hard
note is brighter as well as louder.

## An algorithm is a set of arcs

The DX7's 32 algorithms are fixed ways of wiring its six operators. Here nothing
is fixed. A modulator's `out` goes to another operator's `pm`, and a carrier's `out`
goes to the mix. The examples use two of the DX7's:

- Algorithm 5: three pairs, `2 -> 1`, `4 -> 3` and `6 -> 5`. Used by the electric piano.
- Algorithm 1: `2 -> 1` and a chain `6 -> 5 -> 4 -> 3`. Used by the bass and the bells.

Each example defines one `:Op` subcircuit, holding an operator, its envelope, and
the pitch and velocity sources that feed them. A voice is six instances of it with
their ratios and envelopes set, and the circuit is that voice stamped out as a
pool with `val:voices`. See [writing circuits](circuits.md).

## Feedback that spans operators

Some DX7 algorithms feed an operator's output back into an earlier one, through
more than one operator. Separate `val:FMOperator` elements cannot do this. An arc
that returns to an earlier element is a cycle, and `val:UnitDelay`, the only thing
that breaks one, delays a loop by a control slice and a sample, 33 samples, which is far too long
for FM. A loop has
to be closed a sample at a time, so it has to be inside one element.

**`val:FMMatrix`** is that element. It holds six operators and a modulation
matrix: `m<d><s>` is how much operator `s` modulates operator `d`, as a fraction of
`depth`. The entries on the diagonal, `m11` to `m66`, are the DX7's own feedback
control from 0 to 7. `carrier1` to `carrier6` choose which operators are heard, and
`amp1` to `amp6` are for envelopes, one `val:RateLevelEnvelope` each.

Operators are evaluated from 6 down to 1, as on the DX7. A modulator numbered higher
than its target is used on the same sample. One numbered lower is used as it was a
sample ago. So the usual wiring costs nothing, and a loop has one sample of delay on
the arc that runs against the order. An algorithm is a choice of matrix entries and
carriers: algorithm 5 is `m12`, `m34` and `m56` at 1 with carriers 1, 3 and 5.

`examples/dx7-brass.ttl` uses it for a loop between operators 4 and 6. The matrix
keeps the whole voice in one element, so it is also cheaper than six separate ones,
and an operator whose level has fallen to nothing is skipped.

## What is not modelled

- Keyboard level scaling and keyboard rate scaling.
- The pitch envelope and the LFO. A `val:LFO` can be wired to `frequency` for vibrato.
- Envelope timings are close to the machine's but are not matched to it.

The patches are written in the spirit of the factory sounds and are not copies of
them.
