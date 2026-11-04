# Measuring a sound

Valis is built for a caller that cannot hear. Every claim it makes about a sound is a measurement, and every
report should say **measured, not heard**. A number can show that a sound is silent, clipped, out of tune,
too loud, too dark or unlike the instrument it should be. It cannot show that a sound is good. If a model and
the listener disagree, the listener is right.

This page is the overview: what is measured, which tool measures it, and in what order to use them. The detail
of the `valis-shot` command is in [valis-shot](shot.md). The short checklist for an agent is the
`hear-a-sound` skill. Rendering is covered in [valis-render](manual/render.md), and songs in
[songs as data](songs.md).

## The valis-shot commands

Most of the work is done by `valis-shot`, which is a port of ideas from
[earshot](https://github.com/Leonxlnx/earshot) under `src/shot/`. It has four commands:

| Question | Command | MCP tool | Needs the neural models |
|---|---|---|---|
| Is anything wrong, and how loud or fast is it? | `valis-shot measure FILE` | `measure` | no |
| Did my change alter the sound? | `valis-shot compare A B` | `compare` | no |
| Does this sound like a trumpet? | `valis-shot is FILE "a trumpet"` | `sounds_like` | yes |
| What is it, and which of my sentences fits best? | `valis-shot listen FILE "a harp" "a piano"` | `listen` | yes |

The same functions are called from the operation surface, so the command line tool and the plugin's MCP server
cannot disagree. The song mix uses the same loudness measurement. Start with `measure`. Use `is` for the
"does it sound right" question. Use `listen` when you want to choose the competing sentences yourself.

`measure` also runs on the live circuit through the MCP `render` tool, which renders the loaded circuit and
returns the same analysis.

## What `measure` reports

One JSON object. Read `signal.notes` before the numbers: it is plain language about the faults that matter most.

| Group | Field | Meaning | Warning sign |
|---|---|---|---|
| `signal` | `peakDb`, `rmsDb` | highest sample and overall level | a peak above 0 dBFS clips the wav |
| | `clipped`, `nonFinite` | samples at full scale, and samples that are NaN or infinite | any non-zero count is a broken render |
| | `dc` | mean of the mono mix | above 0.01 needs a high-pass |
| | `silentFraction`, `firstSoundAt` | share of 50 ms windows below -80 dBFS, seconds to the first window above -60 dBFS | silence where the score has notes, or a late start |
| | `pitch` | [YIN](https://doi.org/10.1121/1.1458024) estimate: frequency, nearest note, cents, and the share of frames that agree | a few cents is the limit; an arpeggio read 13 cents sharp until the measurement window excluded its tail |
| | `bandsDb`, `centroidHz`, `rolloffHz`, `highShare` | level of nine octave bands from 63 Hz to 16 kHz, the spectrum's centre of mass, the frequency below which 85% of the power lies, and the share of power above 2 kHz | `highShare` under 3% on a hat, lead or metal layer means it will be buried |
| | `onsets`, `envelopeDbPerQuarterSecond` | when new sounds begin, and the level every quarter second | a missing onset where a note should attack |
| | `stereoImage` | per-second width and correlation, plus the low, mid and high band widths, for two channels | width that collapses, or channels close to opposite phase |
| | `tempo`, `sections`, `key` | the pulse and its alternatives, where the sound changes, and the key of each section | only found on long enough files (8 s for tempo, 20 s for sections) |
| `loudness` | `integratedLufs` | overall loudness to [ITU-R BS.1770](https://www.itu.int/rec/R-REC-BS.1770) and [EBU R 128](https://tech.ebu.ch/docs/r/r128.pdf) | above about -9 LUFS is very loud; streaming sits near -14 |
| | `rangeLu` | loudness range in LU | above about 12 LU for a dance track usually means the quiet sections are too low |
| | `shortTermMaxLufs`, `momentaryMaxLufs` | the loudest 3 s and 400 ms windows | a short-term maximum far above the integrated value means an uncontrolled peak |
| | `truePeakDbtp` | highest level including between samples (true peak) | above -1 dBTP may distort after MP3 encoding |
| `envelope` | `attackMs`, `decayMs` | time from 10% to 90% of the peak, and time to fall 30 dB | a pluck with a slow attack is not a pluck; `-1` means the note never decays |
| | `attackAtMs`, `peakAtMs`, `noiseFloorDbfs`, `dynamicRangeDb` | where the attack and peak sit, and the floor and range of the whole file | a noise floor that follows the note means the circuit is noisy |

The exact field list and thresholds are in [valis-shot](shot.md). Only the first ten minutes of a file are read,
to bound memory, so measure a stem and not an entire long session.

## Comparing two renders

`valis-shot compare A B` answers whether a change did anything. It reports `spectralDistanceDb` at three time
resolutions on a mel scale, `loudnessDifferenceLu` (B minus A) and `lengthDifferenceSeconds`.

* 0 dB is the same sound.
* A few dB is a small change.
* Tens of dB is a different sound.

A sound 6 dB louder reads about 5.6 dB apart. That is expected and is not a change of character. If a change you
meant to hear reads near 0, it did nothing: check for a control arc that overwrites the value, or for a build
that is older than the change.

For a first file of 20 s or more, `sections` says where the difference lies. Both files are cut at the first
file's section boundaries, so renders that are a different length or whose parts have moved compare unlike music.
`mostChangedSection` names the largest spectral difference.

Compare renders made with the same note, length and sample rate. `compare` refuses two different sample rates,
because the distance would mean nothing.

## Loudness and the target of a mix

Loudness is measured with the same code everywhere: `valis-shot measure`, the MCP tools and `valis-song mix`.
The mix aims at the song's `val:loudness`, or at `--lufs N` / `lufs` given to the mix operation. A final gain
brings the mix to that integrated loudness, held back so the true peak stays at or under -1 dBTP, and the reply
says when the ceiling, not the target, set the gain.

A mix that reaches its loudness target by leaning on the limiter is not the same as one that reaches it with
headroom. Read the true peak and the loudness range together with the integrated loudness.

## Does it sound like the instrument it should?

`is` scores one sentence against twenty common rivals: other instruments, a synthesizer preset, noise and a
sine. It answers yes at a share of 0.5 or more, maybe from 0.2, and no below that. The cut-offs are a
judgement. The useful part is `closest`: what the sound was mistaken for.

A synthetic-sounding instrument shows up as a synthesizer preset. An instrument that comes out as a different
real one (a harp read as a piano) has the right character and the wrong detail. Change the excitation or the
physical model, not the level.

`listen` gives the eight most likely [AudioSet](https://research.google.com/audioset/) labels and, for each
sentence you give, a `cosine` and a `share`. Read the share, not the cosine. Give two or three real
alternatives, and name the instrument the way a person would ("a plucked nylon string guitar", not "guitar
stem v3").

The neural questions use [CLAP](https://huggingface.co/laion/larger_clap_general) for the sentence scores and
[AST](https://huggingface.co/MIT/ast-finetuned-audioset-10-10-0.4593) for the labels. They need a build with
`-DVALIS_WITH_ONNX=ON` and about 1.1 GB of weights from `scripts/shot/fetch-models.sh`. A build without them
says so and nothing else breaks. The weights are pinned and checksummed, are never in git, and a test skips
itself when they are absent.

The models hear about ten seconds at a time. AST reads the loudest ten seconds of a long file; CLAP averages
windows and stops at 14 kHz, so it cannot hear "air", while `measure` and `compare` can. A score says how much a
sound resembles a described sound, not whether a circuit is well made.

## Fitting a sound to a recording

Two operations search for a circuit that matches a recording. Both change nothing: they report values for you to
set.

* `fit_params` (MCP) or `OpDispatcher::fitParams` plays one note through the circuit, scores each render against
  the recording by spectral distance, and searches the circuit's `val:Param` values. It is a seeded evolution
  strategy, so the same call gives the same answer. Give it `slots` to search a few knobs; searching all of them
  needs more evaluations. With 24 evaluations or more it first tries a seeded spread of starting points, so one
  poor start does not leave it at the edge of a knob's range. A small distance means the spectra agree, not that
  the sound is right.
* `fit_modes` (MCP) turns a recording of a struck object, such as a bell, a cowbell or a conga, into the modes of
  a `val:FreeModalBank`: frequency, ring time and level for each. It reports them as a `val:modes` string and a
  line of Turtle to paste.

Both are ports of earshot ideas and are covered in more detail in [valis-shot](shot.md). `fit_modes` matches the
spectrum and ring times, not the attack or the way the sound was played. The tests use known modes; real
recordings are untried.

## Stability and the live circuit

`check_stability` (MCP) runs a circuit on a fresh engine with random settings of its bound parameters, every one
of them jumping partway through a run. It reports any run that goes non-finite, blows up above +40 dBFS, clips
or falls silent, and returns the settings that did it. The first run uses the declared settings; the rest are
seeded, so a failure can be repeated. This is the check for a circuit that is stable at one patch and explodes
at another. It is not a substitute for measuring a render.

`read_outputs` reports every control output the circuit carries at that moment. It is how the Controls view draws
`val:Oscilloscope` and `val:FreqAnalyzer`, and how an agent checks an envelope or an LFO without rendering a wav.

## Checking a song's stems

`valis-song stems song.ttl` (MCP `check_stems`) compares each rendered stem with its MIDI file bar by bar. It
reports silence where the score has notes, clipping, non-finite samples and an empty MIDI file. A note counts as
heard if the stem sounds from its onset to 100 ms past the end of its bar.

Then measure each stem on its own, not in the mix:

```sh
valis-shot measure artifacts/<track>/wavs/kick.wav
valis-shot is artifacts/<track>/wavs/guitar.wav "a plucked nylon string guitar"
```

Other parts hide a fault, so judge a stem alone. Keep the earlier render when you change something, so the next
`compare` has something to compare with.

## A working order

1. **Build everything first** (`cmake --build build`, no target). A targeted test build can leave a tool linked
   against the old library, and then the tool measures code that is not there. Use the Release build for long
   renders.
2. **Render one note or one part** to a wav. Read the log: `nan`, `exceeds full scale` and `arc joins` mean a
   failed render.
3. **Measure it.** Read `signal.notes` first, then pitch, loudness and envelope. Fix anything in the notes before
   going further.
4. **Compare with the previous version** if there is one. A change that reads near 0 did nothing.
5. **Ask whether it sounds like the intended instrument** with `is`. Read `closest`, not only the verdict.
6. **Fit or tune** with `fit_params`, `fit_modes` or a changed control value, then measure again.
7. **Record the numbers** you are relying on, and say that they are measured, not heard. Keep earlier renders so
   the next comparison is possible.

## Limits

* A measurement describes the samples given to it. A render at the wrong sample rate, a stale binary or a file
  from the wrong part gives a confident, wrong answer.
* A fixed analysis window covers different seconds at each sample rate. Check the window before trusting pitch
  or level.
* The spectral distance is sensitive to level as well as timbre. Match levels before comparing character.
* The neural verdict cut-offs are a judgement, and the models do not know Valis. A score says how much a sound
  resembles a description.
* A small distance or a confident label is evidence, not a decision. A person with ears decides.

## Where the code is

| Part | What it does | File |
|---|---|---|
| Signal analysis | level, clipping, DC, pitch, octave bands, brightness, onsets, envelope over time, stereo image, tempo, sections, plain notes | `src/ops/Analysis.cpp` |
| Loudness | integrated loudness, range, short-term and momentary maxima, true peak, to BS.1770-4 and EBU R 128 | `src/shot/Loudness.cpp` |
| Envelope times | attack, decay, noise floor, dynamic range | `src/shot/EnvelopeTimes.cpp` |
| Spectral distance | distance in dB between two renders at three time resolutions | `src/shot/SpectralDistance.cpp` |
| Section comparison | where two renders differ, and how wide each section is | `src/shot/SectionCompare.cpp` |
| Modal fit | damped sines from a struck recording | `src/shot/ModalFit.cpp` |
| Neural ears | CLAP and AST through ONNX Runtime, with their audio front ends and text tokenizer | `src/shot/NeuralEars.cpp` |
| Song stem check | each stem against its MIDI file, bar by bar | `src/ops/SongCheck.cpp` |
| Command line | `valis-shot measure`, `compare`, `is`, `listen` | `src/shot/ShotMain.cpp` |

The tests are in `tests/shot/`, each against a signal whose answer is known: a 997 Hz sine at -23 dBFS reads
-23.0 LUFS, a tone at a quarter of the sample rate has a true peak 3 dB above its sample peak, and the modal fit
recovers modes it was given. The CLAP temperature used for shares, 38.6647, is `exp(logit_scale_a)` read from
the published weights.
