# Case study: granular synthesiser

Granular synthesis builds sound out of short overlapping fragments, called
grains, cut from a longer recording. Each grain is a few milliseconds to a few
hundred milliseconds of the source, faded in and out by a window, played at its
own speed and placed at its own point in the stereo image. Where the grains come
from, how long they are, how often they start and how far they scatter are all
separate controls, so the same material can be a faithful time stretch, a
shimmering pad, or a cloud of unrecognisable particles.

The technique is set out in Curtis Roads,
[Microsound](https://mitpress.mit.edu/9780262681544/microsound/) (MIT Press,
2001).

The full Turtle is at `examples/granular.ttl`, and the sample it loads is
`examples/samples/bell.wav`, itself rendered from `examples/rings-modal.ttl`.

## Signal path

```
SampleLoad ──► Granulator ─┬─► Ladder L ──► VCA L ──► DryWet L ──► Output.left
                           └─► Ladder R ──► VCA R ──► DryWet R ──► Output.right

Input ───────────────────────────────────────► DryWet L.dry, DryWet R.dry

Transport.trigger ──► clockGate (Scale) ─────► Granulator.trigger
MidiInterval.semitones ──► pitchGate ────────► Granulator.pitch
midi (Select) ──► Envelope.gate, thru ───────► pitchGate.b
Envelope ────────────────────────────────────► VCA L.cv, VCA R.cv
Envelope ──► toneScale (Scale) ──────────────► Ladder L.cutoff, Ladder R.cutoff
mixScale (Scale) ────────────────────────────► DryWet L.mix, DryWet R.mix
```

Play a note to hear it: with MIDI In on, the envelope gates the VCAs, so the
circuit is silent until a note-on arrives.

## The sample slot

`:sample` is a `val:SampleLoad`. It plays `samples/bell.wav` on a loop into the
granulator's audio input, and the granulator records what arrives there, so the
buffer holds the sample.

The Controls view draws it as a file slot: the name of the file it is playing,
and a Load button that opens a file chooser. Choosing another file reads it on
the message thread and reinstalls the circuit, so the swap happens through the
same preallocate-then-hand-over path as any other change. If the new file will
not read, the old one keeps playing and the box says why.

The choice is a session setting, not a document edit, in the same way a turned
knob is: the Turtle keeps declaring `val:file "samples/bell.wav"`, and what the
session has chosen sits on top of it. It is saved with the plugin state, so a
project reopens with the sample it was using. Over MCP the same thing is
`get_sample` and `set_sample`.

```turtle
:sample a val:SampleLoad ;
    val:file  "samples/bell.wav" ;
    val:loop  1 ;               # a switch in the Controls view, not a dial
    val:speed 1.0 .
```

## Where the material comes from

`val:Granulator` owns one circular buffer, and there are three ways to fill it.

**A sound file.** `val:file` names one, loaded on the message thread when the
circuit is installed, summed to mono and resampled to the engine's rate. A
relative path resolves against the working directory first, then `examples/`,
then the repository root. A path that does not resolve fails the load with a
located error rather than leaving the element silent.

```turtle
:gran a val:Granulator ;
    val:file    "samples/bell.wav" ;
    val:seconds 8.0 .
```

**Live audio.** Anything arriving at the audio input is written at the write
head, which is how the example fills its buffer from `:sample`. Point the
`:aInGran` arc at `:in` instead and the granulator records the plugin's own
input, which is how to granulate a live instrument or another track.

**Frozen.** `val:freeze` decides between the two, and the Controls view draws it
as a two-position switch rather than a dial, because there are two places to be
and nothing in between:

| Freeze | What the buffer does |
|---|---|
| Record | Writes whatever reaches the audio input into the buffer. |
| Freeze | Holds what is already there, ignoring the input. |

An input with no arc reaching it records nothing at all. The engine points an
unconnected input at a shared block of silence, and the element can see that, so
a circuit that loads `val:file` and wires nothing to the input keeps its
material without having to say so. An input that is connected but quiet still
records, because that is what freezing is for.

Freeze is worth hearing rather than reading about: in `granular.ttl` the buffer
is 1.2 seconds against a 2.6 second sample, so on Record the buffer is a window
sliding along the loop and the texture keeps moving, and on Freeze the window
stops and one moment repeats. What is heard lags what is arriving by about a
buffer, because at `position` 0 a grain reads forward from the write head, which
is the oldest sample in the buffer.

`val:position` is measured forward from the write head. With a file loaded the
write head sits at the start of the file, so 0 is the beginning and 1 the end.
While recording, the write head is the present moment, so values near 1
granulate the most recent audio and lower values reach further back.

## The grain controls

| Control | What it does |
|---|---|
| `position` | Where in the buffer grains are read from. |
| `size` | Grain length in milliseconds. Below about 50 ms the onset rate becomes a pitch of its own; above it, grains blur into a pad. |
| `density` | Grain onsets per second while free-running. Independent of size, so it sets how deeply grains overlap. |
| `pitch` | Transposition in semitones. Changes how fast each grain is read, not how often grains start. |
| `spray` | Random spread of each grain's start point. |
| `jitter` | Random spread of the interval between onsets, which breaks up the periodicity a fixed density produces. |
| `pitchJitter` | Random detune per grain. A little gives chorus; an octave gives shimmer. |
| `shape` | The grain window, from nearly rectangular to a full rise and fall. |
| `spread` | Random stereo placement per grain. |
| `reverse` | Probability that a grain plays backwards. |
| `scan` | Drift of the read point through the buffer, in buffer lengths per second. |

Overlapping grains sum, so a dense cloud would otherwise be much louder than a
sparse one. The element divides its output by the square root of the overlap
count, which keeps density usable as a texture control rather than a level one.

Up to 64 grains sound at once. When they are all busy, a new onset is dropped:
the alternative on the audio thread is to allocate, and one missing grain in a
cloud of sixty-four is inaudible.

## Taking timing from the host

`val:Transport` turns the host's timeline into control signals. `val:division`
is measured in quarter notes, so 0.25 is a sixteenth and 4 is one bar of
four-four. `phase` ramps from 0 to 1 across each division, `trigger` is 1 for the
one control block in which it wraps, and `tempo`, `rate` and `playing` report
the rest.

```turtle
:clock a val:Transport ;
    val:division 0.25 .
```

The engine carries the host's position forward one control block at a time from
the tempo, so `phase` is continuous inside a buffer instead of stepping once per
block. When the host is stopped, or is a host with no timeline at all, `phase`
free-runs at the same rate from the last tempo reported, so the circuit still
behaves in the standalone app.

The granulator's `trigger` port follows the same convention as
`val:TwinTBridge`: at -1 it free-runs at the density rate, and at any other
value each rising edge fires one grain. The example makes that a parameter by
passing the transport's pulse through a `val:Scale`, whose `min` is the port's
resting value:

```turtle
:clockGate a val:Scale ;
    val:min -1.0 ;      # Grain Clock parameter: -1 free-runs, 0 locks to tempo
    val:max  1.0 .
```

With Grain Clock at 0, the port rests at 0 and only the transport's pulses fire
grains, so onsets land on the beat. With it at -1 the port rests at -1, the
element free-runs, and Density is back in charge.

## The dry/wet mix

The granulated signal is the wet side of a `val:DryWet` per channel, and the
plugin's own audio input is the dry one. The Mix knob runs from the input alone
at 0 to the granulator alone at 1, and rests in the middle, so the same circuit
is an instrument, an effect, or any blend of the two.

One knob reaches both channels because the parameter is bound to a `val:Scale`
whose output feeds each DryWet's `mix` port. A parameter binds one slot to one
property, so anything stereo needs a control path that fans out rather than two
knobs that have to be kept in step.

## Playing it from a keyboard

`val:MidiPitch` answers "what frequency?", which is what an oscillator needs.
A granulator needs "how far from the root?", which is `val:MidiInterval`:

```turtle
:key a val:MidiInterval ;
    val:root 60.0 .     # middle C plays the material untransposed
```

`semitones` drives the granulator's `pitch` port through `:pitchGate`. `ratio`
carries the same interval as a playback speed multiplier, for anything that
wants one.

**MIDI In** is a switch, drawn as a two-position rocker rather than a dial,
because it chooses between two things and a dial would invite a sweep between
them. It is a `val:Select`:

```turtle
:midi a val:Select ;
    val:a  1.0 ;        # off: hold the envelope open
    val:b -1.0 ;        # on:  follow the host's note gate
    val:select 1.0 .
```

With MIDI In on, `out` is -1, which `val:Envelope` reads as "use the host MIDI
gate", and `thru` is 1, so the played interval reaches the granulator. With it
off, `out` is 1, which holds the envelope open so the circuit drones and works
as an effect on whatever is at the audio input, and `thru` is 0, so the material
stays at its recorded pitch however hard the keyboard is played.

`thru` is what lets one switch do both jobs: a parameter binds one slot to one
property, so a switch that has to reach two places has to carry its position
along a control arc.

Note that a control arc **replaces** the value of the port it reaches every
block. `val:pitch`, `val:trigger`, `val:cutoff` and `val:cv` therefore carry no
fixed values in `granular.ttl`: their resting values live in the control path,
in the `val:Scale` elements and in MidiInterval's root.

## Rendering it offline

`valis-render` reports a transport, so a tempo-locked circuit renders exactly as
it plays:

```sh
./build/valis_render_artefacts/Debug/valis_render examples/granular.ttl \
  -o granular.wav --seconds 4 --note 60 --gate-off 3.0 --tempo 120 --rolling
```

Without `--rolling` the transport is reported as stopped, which is what a host
does when it is not playing.

## Things to try

- Size around 20 ms with Density at 100 and Jitter at 0: the grain rate itself
  becomes an audible pitch, and the source becomes its timbre.
- Freeze at 1, Position slowly swept, Scan at 0: a drone made from one moment of
  the recording.
- Detune at 7 with Density high: grains land on the fifth as often as the root,
  so the cloud harmonises with itself.
- Reverse at 0.5 with a long Size: half the grains run backwards, which removes
  the sense of direction from the material entirely.
- Grain Clock at 0 with Division at 0.25 and Size at 60 ms: a rhythmic stutter
  locked to the host.
- MIDI In off, Mix around 0.7, and a track playing into the plugin: the circuit
  becomes a granular effect on whatever arrives, with the dry signal still
  audible underneath.
- Load a spoken word recording through the sample slot, Size at 300 ms and
  Density at 4: the words come apart into overlapping syllables.
