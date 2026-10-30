# An unreleased New Order track: requirements

The aim is a new, original piece that could pass for a track New Order left unreleased in 1983 or 1984.
It is not a copy of "Blue Monday". It is written in the same way, on the same kinds of machine, and
the machines are modelled as circuits in Valis. This page records what the research found, what the
track and its circuits therefore need, and what Valis lacks.

Nothing from the record is reproduced. That rules out its bass sequence, Peter Hook's lead line, the
choir sample and its drum pattern as written. What is borrowed is method, instrumentation, sound and form.

## What the research found

Sources: the band's own account in the
[Song Exploder transcript](https://songexploder.net/new-order) (episode 252, 2023),
[Wikipedia](https://en.wikipedia.org/wiki/Blue_Monday_(New_Order_song)), the
[Music Week](https://www.musicweek.com/talent/read/the-story-behind-new-order-s-blue-monday-the-uk-s-biggest-selling-12-inch-single/087537)
interview with the engineer Michael Johnson, and the Wikipedia pages for the
[Oberheim DMX](https://en.wikipedia.org/wiki/Oberheim_DMX) and the
[Moog Source](https://en.wikipedia.org/wiki/Moog_Source). Where sources disagree, both are given.

### How it was made

- It began as an instrumental that machines could play by themselves, so the band could leave the stage
  before an encore. Everything was therefore programmed, and had to run as one long loop.
- This was before [MIDI](https://en.wikipedia.org/wiki/MIDI). A sync circuit built by the engineer Martin
  Usher made the sequencer, the synthesisers and the drum machine run together. The 22-track tape machine
  had no timecode, two tracks carried sync pulses, and many parts shared a track.
- Drums, bass and most synths were programmed in the rehearsal room and the overdubs were added in the
  studio. Recording was at Britannia Row, Islington. The year is given as 1981 by Hook and 1982 by Wikipedia.
- Bernard Sumner drove the project. His aim was a disco record, from clubs heard in New York: a sharp
  sound, a lot of sub bass, and a 12-inch cut loud and punchy for a club system.
- The sequence was entered as numbers, written out by Gillian Gilbert on taped-together paper. She left
  in one extra rest, which pushed a synth line off the grid. The band kept it. Their view is that the mistakes
  are what give the record its edge.
- The form is not verse and chorus: a long intro, two contiguous verses, a short run of effects, a third
  verse, a short breakdown and a long outro. The 12-inch is 7:29. Tempo is about 130 beats per minute
  (several tempo databases agree; their keys disagree, so no key is taken from them).

### The instruments

| Part | What was used | What is known about the sound |
|---|---|---|
| Drums | [Oberheim DMX](https://en.wikipedia.org/wiki/Oberheim_DMX) | Played 8-bit companded samples from ROM cards, with a tuning trimmer on each card. The kick pattern is sixteenth notes, said to follow Donna Summer's "Our Love". Sumner describes three layers: steady closed hi-hats, triplets, and a sharp clap in the manner of 1970s disco, over a kick that is "the engine". |
| Kick | DMX through a studio monitor, re-recorded by a microphone | Adds the room and a rounded, longer low end. |
| Bass synth | [Moog Source](https://en.wikipedia.org/wiki/Moog_Source) played by a sequencer | Two oscillators, a 24 dB per octave low-pass filter, two ADSR envelopes. The sequencer is described as home-built by Sumner and, in other accounts, as a Powertran model. |
| Bass guitar | Hook's six-string bass | Tuned like a guitar, an octave down (E A D G B E). Played high on the neck with chorus, so it was often taken for a guitar. It sits over the machines, which was new at the time. |
| Synth line | Sequential Prophet (the Prophet-5) | An analogue poly synth. |
| Strings | A Quadra, a string machine and synthesiser in one (Johnson's account) | Not heard clearly; part of the pad. |
| Choir | E-mu Emulator I, a sampler | Two seconds of sample at most, then it loops. The choir sounded to the band like a Mellotron. The same sampler held a thunderstorm and other sounds. |
| Voice | Sumner, through a vocoder for the backing | Sent through a little spring reverb, played through a speaker and recorded again. |
| Speech | An Apple II speech synthesiser, played by Stephen Morris | A stuttering vocal effect. |

## What the new track needs

### The piece

- Instrumental first. A vocal is out of reach, so the voice parts are vocoded or formant pads.
- About seven minutes, at 128 to 132 beats per minute, built as the 12-inch was: a long intro that starts
  from the kick alone, sections that join without a break, a run of effects, a breakdown and a long outro.
- One thing deliberately off the grid, in the way of the extra rest, but made on purpose and found by ear.
- A bass sequence on the Source circuit, a melodic bass guitar part over it, a Prophet line, a choir and a
  string pad. New melodic material throughout.
- Mixed for a club: loud, a clear sub, the kick and clap forward.
- Rendered offline with `valis-render --tempo --rolling`, and playable from the host's transport.

### The circuits

Each circuit models one machine and should be usable on its own. The order is the order of the work.

| Circuit | Models | Builds on | Missing in Valis |
|---|---|---|---|
| DMX kit | The drum machine | `examples/dmx.ttl`, which plays captures of a real DMX | A kick sent through a "monitor" (a resonant low end plus a short room); a per-card tuning; shuffle or swing on the sequencers |
| Source bass | The Moog Source | `val:Oscillator`, a ladder filter, `val:Envelope` | Two oscillators with three waveforms and three octave settings each; a patch matched to what the Source can do |
| Sequencer | The home-built sequencer | `val:StepSequencer` | A pattern that is a different length from the bar, so a line drifts across it |
| Bass guitar | Hook's six-string | `val:StiffString` (now in tune), `val:Chorus` | A plucked-string voice and a pickup, played high on the neck |
| Prophet line | The Prophet-5 | `examples/polysynth.ttl` | A patch with two oscillators, a sub and a four-pole filter |
| Choir | The Emulator I | `val:Redux`, `val:SampleLoad` | A choir to sample. No recording is in the repository, and the record's sample is not ours to use, so one is made by filtering a sawtooth and noise through vowel formants, then bit-reduced, cut to two seconds and looped |
| Strings | The Quadra | `val:Chorus`, `val:Phaser` | An ensemble effect on a bright sawtooth |
| Vocoder | The vocoder on Sumner's backing | A filter bank | `val:Vocoder` does not exist |
| Speech stutter | The Apple II synthesiser | `val:Granulator` | Optional; a formant stutter would stand in |
| Spring reverb | The re-recorded reverb | `val:Reverb`, `val:Tape` | A speaker-and-microphone stage, a resonant band-limit and a short delay |

## Open questions

- The key is not taken from the record. Choose one for the new piece, in a minor mode that suits a
  Source bass, and say so in the file's header.
- Whether the Emulator choir is better as a looped synthetic sample or a live-synthesised pad. The
  record's choir is a sample, so a looped one is the more faithful.
- Whether to include a vocal at all. An instrumental fits how the original began.

## Status

Built: the track "Tenement Lights" (A minor, 130 beats per minute, 224 bars, about 6:53) and these circuits.

| Circuit | File | Notes |
|---|---|---|
| DMX kit | `examples/dmx.ttl` | The existing captures of a real DMX. The kick is also played through `speaker-room.ttl` and mixed under itself. The score has the machine's shuffle: the second sixteenth of each pair sits at 54%. The high percussion goes to `reverb-return.ttl`. |
| Source bass | `examples/moog-source.ttl` | Saw and octave-down square, 24 dB ladder, cutoff from an envelope plus keyboard tracking. |
| Bass guitar | `examples/six-string-bass.ttl` | `val:StiffString` with chorus; tuned within a cent from E2 to E5. |
| Prophet line | `examples/prophet-5.ttl` | Eight voices, with a soft-clip distortion. |
| Choir | `examples/emulator-choir.ttl` | Synthesised vowel, bit-reduced; not a recording. |
| Strings | `examples/quadra-strings.ttl` | Eight voices with two choruses and a phaser. |
| Speaker and room | `examples/speaker-room.ttl` | An effect. |

The score and mix are `scripts/electro83/track.py`. The Prophet arpeggio is deliberately a sixteenth late
from the start of the peak section, which is the extra rest in this track. `tests/engine/ElectroInstrumentsTest.cpp`
checks every instrument for silence before a note, pitch, release and a second note after a gap.

Not built: `val:Vocoder`, the speech stutter (specified below), and a sequencer whose
pattern length differs from the bar (the late arpeggio is done in the score). The track has never been
listened to; it was checked by measurement only.

## The speech stutter

The record has a stuttering vocal effect made by Stephen Morris on a speech synthesiser driven by an
Apple II, according to the engineer Michael Johnson in the
[Music Week](https://www.musicweek.com/talent/read/the-story-behind-new-order-s-blue-monday-the-uk-s-biggest-selling-12-inch-single/087537)
interview. The same account says the part was accidentally wiped during another session. No source found
says which synthesiser it was or what it said, so what follows describes the effect in general, as a
specification for a plug-in or an element.

- **Source.** A speech synthesiser of the period turns a short phrase, or a string of phonemes, into a flat,
  robotic voice. The voice is monotone or nearly so, thin, with hard consonants and little breath. Output was 8-bit
  and low-rate, so it is grainy and has aliasing in the upper range.
- **The stutter.** The phrase is cut off just after its first sound and started again, many times in a row,
  before it is allowed to finish. The repeats are locked to the beat, usually at a sixteenth or thirty-second
  note, so the first syllable chatters in time with the hi-hats and then runs on into the whole word.
- **Controls it needs.** The phrase; a repeat rate in note values; how many repeats before the word runs on, or a
  gate length; a pitch, with an optional glide so the pitch can fall or rise across the repeats; a bit depth and
  sample rate to set the grain; and a level and a send to the reverb.
- **Where it sits.** It is a sound effect between sections, at the end of a break or over a drum fill, and not a
  lead voice. It is dry and close, with a short reverb at most.
- **How Valis could make it.** A starting point is a `val:Granulator` or a `val:SampleLoad` playing a recorded
  phrase, retriggered on the beat by a `val:StepSequencer`, with a `val:Redux` for the grain. A plug-in that speaks
  the phrase can be placed before the same chain. The shuffle and the reverb used on the rest of the track apply
  to it in the same way.
