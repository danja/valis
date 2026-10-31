# valis-shot: checking a sound without listening

`valis-shot` measures a sound file. Use it when you cannot listen, or want a number to compare. It is a port of ideas
from [earshot](https://github.com/Leonxlnx/earshot), written in C++ under `src/shot/`. It is a work in progress.

Everything it reports is **measured, not heard**. A number or a model can say a sound is odd. It cannot say a sound is good.

## The four questions

| You ask | Command | MCP tool | Needs models |
|---|---|---|---|
| Is anything wrong with this file? How loud, how fast is the attack? | `valis-shot measure FILE` | `measure` | no |
| Did my change alter the sound? | `valis-shot compare A B` | `compare` | no |
| Is this "a trumpet"? | `valis-shot is FILE "a trumpet"` | `sounds_like` | yes |
| What is it? Which of my sentences fits best? | `valis-shot listen FILE "a harp" "a piano"` | `listen` | yes |

Start with `measure`. Use `is` for the "does it sound right" question. Use `listen` only when you want to
choose the competing sentences yourself.

### measure

Prints one JSON object. Read `signal.notes` first: plain words about silence, clipping, DC offset or values that are not
numbers. Then `signal.pitch` (the note and how many cents off), `loudness` and `envelope`.

| Field | Meaning | Warning sign |
|---|---|---|
| `loudness.integratedLufs` | overall loudness, [LUFS](https://en.wikipedia.org/wiki/LKFS) | above about -9 is very loud; streaming sits near -14 |
| `loudness.truePeakDbtp` | highest level including between samples | above -1 may distort after MP3 |
| `envelope.attackMs` | how fast it starts: a drum a few ms, a bowed string hundreds | a pluck with a slow attack is not a pluck |
| `envelope.decayMs` | time to fall 30 dB; `-1` if it never does | a note that dies when you wanted a ring |

### compare

`spectralDistanceDb` is 0 for the same sound, a few dB for a small change, tens of dB for a different sound. A
sound 6 dB louder reads about 5.6. `loudnessDifferenceLu` is the loudness change, second file minus first.

### is

Give it a file and one sentence. It scores the sentence against twenty common rivals (other instruments, a
synthesizer preset, noise, a sine) and answers:

```json
{"sentence":"a trumpet","verdict":"no","share":0.196,"rank":2,"of":20,
 "closest":[{"sentence":"a synthesizer keyboard preset","share":0.600},{"sentence":"a trumpet","share":0.196}, ...]}
```

`verdict` is yes at a share of 0.5 or more, maybe from 0.2, else no. Those cut-offs are a judgement. The useful part
is `closest`: what the sound was mistaken for. A synthetic-sounding instrument shows up as a synthesizer preset.

### listen

The same models without the rivals: the eight most likely [AudioSet](https://research.google.com/audioset/) labels, and a
`cosine` and `share` for each sentence you give. Read the share, not the cosine; a cosine alone means little.
Give two or three real alternatives.

### Matching a recording with the circuit's own knobs

`fit_params` (MCP) or `OpDispatcher::fitParams` plays one note, scores the render against a recording by spectral
distance, and moves the circuit's `val:Param` values to lower it. It reports the distance before and after and the value
found for each knob, and changes nothing. The search is a seeded evolution strategy, so the same call gives the same
answer; a render takes a fraction of a second, so 60 evaluations is a few seconds to a minute. In the test it takes an
SH-101 whose filter peak is wrong by 6500 Hz from 19.6 dB away to 0.2 dB and finds 1517 Hz where the answer is 1500.
Give it `slots` to search a few knobs; searching all of them needs more evaluations. A small distance means the spectra
agree, not that the sound is right.

### Aiming a mix at a loudness

`scripts/shot/lufs.py` asks `valis-shot measure` for a mix's loudness and returns the gain that reaches a target in
LUFS, held back so the true peak stays at or under -1 dBTP. A mix script writes the mix, calls
`lufs.gain_to_target(path, -14.0)`, scales by the result and writes again. `lights-on-nobody-home/track.py mix
--lufs=-14` does this; on the v3 mix it takes -12.3 LUFS to -14.0 LUFS with a true peak of -2.67 dBTP. Without the flag
the mix is as before.

## Setting up the neural questions (`is` and `listen`)

```sh
cmake -S . -B build -DVALIS_WITH_ONNX=ON      # fetches ONNX Runtime 1.31.1, pinned and SHA-256 checked
cmake --build build
scripts/shot/fetch-models.sh                   # about 1.1 GB, once, into ~/.local/share/valis/models
```

Nothing downloads unless you run the script. A build without `VALIS_WITH_ONNX`, or without the models, answers `is` and
`listen` with that reason and nothing else breaks. The first call in a process loads the models (a few seconds, about a
gigabyte of memory); later calls are fast. Set `VALIS_MODELS` to keep the models elsewhere.

## What it said about "Lights On, Nobody Home"

`is` on each rendered stem, with the sentence that should fit:

| Stem | Sentence | Verdict | What it was closest to |
|---|---|---|---|
| guitar | a plucked nylon string guitar | yes 92% | itself |
| vihuela | a plucked nylon string guitar | yes 88% | itself |
| congas | conga drums | yes 66% | itself |
| guitarron | a bass guitar | yes 55% | nylon guitar 44% |
| trumpets | a trumpet | no 20% | **a synthesizer keyboard preset 60%** |
| violins | a violin section | no 5% | **an electric organ 47%**, a flute 25% |
| harp | a harp | no 7% | **a piano 53%**, a bell 14% |
| shakers | maracas shaking | no 7% | **conga drums 62%** |

The plucked and struck parts pass. The trumpets, violins, harp and shakers do not, and the models say what they sound like
instead. That agreed with a listener who called the first trumpets a cheap keyboard. The next work is in `TODO.md`.

## Limits

* The models hear about ten seconds at a time. For a long file AST reads the loudest ten seconds and CLAP averages windows.
* CLAP stops at 14 kHz, so it cannot hear "air"; `measure` and `compare` can.
* CLAP does not know Valis. A score says how much a sound resembles a described sound, not whether a circuit is well made.
* The audio front ends have no reference implementation to compare with (that needs Python and torch). The tests use
  known answers instead: AST must name a sine `Sine wave` and hiss `White noise`, and CLAP must put a low tone nearer
  "a low, deep rumble" and a high tone nearer "a high pitched whistle". That rules out a gross mistake, not a small shift.
* If a model disagrees with your ears, trust your ears.

## How it is built

The work is in `include/valis/ShotReport.h` and `src/shot/ShotReport.cpp`. The command line tool, the operation surface
(`OpDispatcher::measure`, `compare`, `soundsLike`, `listen`) and the MCP server all call it, so there is one implementation.

| Part | What it does | From earshot |
|---|---|---|
| `Loudness` | integrated loudness, range and true peak after [ITU-R BS.1770-4](https://www.itu.int/rec/R-REC-BS.1770) and [EBU R 128](https://tech.ebu.ch/docs/r/r128.pdf); coefficients derived for any sample rate | `features.py` |
| `EnvelopeTimes` | attack (10% to 90%), decay (to 30 dB down), noise floor, dynamic range | `features.envelope_times` |
| `SpectralDistance` | distance in dB between two renders at three time resolutions on a mel scale | `fit/modal.mel_distance` |
| `ClapFrontend`, `AstFrontend` | the log-mel features each model was trained on | the models' own extractors |
| `BpeTokenizer` | the byte-pair tokenizer for CLAP's text (reproduces the known RoBERTa ids) | the model's tokenizer |
| `NeuralEars` | the ONNX wrapper; the only file that includes ONNX Runtime | `models/clap.py`, `models/ast.py` |

Tests are in `tests/shot/`, each against a signal whose answer is known: a 997 Hz sine at -23 dBFS reads -23.0 LUFS,
and a tone at a quarter of the sample rate has a true peak 3 dB above its sample peak. On the v3 mix, loudness
is -12.3 LUFS with 7.5 LU range and a -0.97 dBTP true peak, louder than streaming services need.

The CLAP temperature used for shares, 38.6647, is `exp(logit_scale_a)` read from the published weights.

### Weights

The weights are not in git and not downloaded by a build or a test. `scripts/shot/fetch-models.sh` fetches them from
pinned commits and checks each file's SHA-256; a bad download is discarded. The tests skip themselves when the weights
are absent.

| Model | Source (ONNX export of the original) | Licence | Size |
|---|---|---|---|
| CLAP audio encoder | [Xenova/larger_clap_general](https://huggingface.co/Xenova/larger_clap_general) @ d78c399, from [laion/larger_clap_general](https://huggingface.co/laion/larger_clap_general) | Apache-2.0 | 282 MB |
| CLAP text encoder | the same repository | Apache-2.0 | 502 MB |
| AST | [Xenova/ast-finetuned-audioset-10-10-0.4593](https://huggingface.co/Xenova/ast-finetuned-audioset-10-10-0.4593) @ 249a1fb, from [MIT/ast-finetuned-audioset-10-10-0.4593](https://huggingface.co/MIT/ast-finetuned-audioset-10-10-0.4593) | BSD-3-Clause | 347 MB |

The original models carry the licences shown. The ONNX repositories state none of their own and are conversions of those weights.

## Not ported, and why

| Idea | Why it is useful | Cost |
|---|---|---|
| **Modal fit** (`fit/modal.py`): a struck recording becomes damped sinusoids found by ESPRIT, then a model that re-renders it | Would turn a recording of a real conga or cowbell into a circuit. `val:ModalBank` has six modes at fixed ratios, so a free-mode bank would be needed first. | Large. The best of these ideas. |
| **Search by listening** (`forge/critic.py`, `search.py`): vary a recipe's knobs and keep what scores nearest a target | Done in a small form: `fit_params` (see below). Still missing: searching the structure of a circuit, and scoring by more than spectral distance. | |
| **Sound bank as recipes** (`forge/recipes`, 252 UI and film sounds) | Short sounds (clicks, whooshes, risers) described as code; a useful example set. | Medium, mostly circuit writing. |
| **Realness** (`realness.py`): real recording or code-synthesised | Would score how acoustic a circuit sounds. | A classifier on CLAP features; could be ported now that CLAP runs. |
| **Calibrated match** (`match.py`, `vocab.py`): a z-score among 322 reference descriptions | A fairer verdict than the fixed cut-offs in `is`. | Small: port `vocab.json` and its statistics. |
| **Timeline and sync** (`timeline.py`) | Only for scoring to video. | Not relevant yet. |
| **Stripping a bed**, **gesture and spectral fitters** | Fitting noisy sounds such as whooshes. | Large, narrow use. |

The two worth doing next, the free-mode modal bank and a parameter search, are in `TODO.md`.
