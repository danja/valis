# Ideas from earshot (`src/shot/`)

[earshot](https://github.com/Leonxlnx/earshot) is a Python tool that gives a coding agent measurements of a sound
in place of ears. It also makes sounds in code and fits models to recordings. Most of it needs neural audio models
(CLAP and AST, https://github.com/LAION-AI/CLAP) and Python, which Valis does not link, so only the parts that are
plain arithmetic are ported. They live in `src/shot/`, with headers in `include/valis/`.

## Ported

| Class | From earshot | What it is for |
|---|---|---|
| `Loudness` (`measureLoudness`) | `features.py`: `_kw`, `loudness_curve`, `true_peak_dbtp` | Integrated loudness in LUFS, loudness range in LU and true peak in dBTP, after [ITU-R BS.1770-4](https://www.itu.int/rec/R-REC-BS.1770) and [EBU R 128](https://tech.ebu.ch/docs/r/r128.pdf). The filter coefficients are derived for any sample rate, not fixed at 48 kHz. This is what a mix is judged by when mastering. |
| `EnvelopeTimes` (`measureEnvelope`) | `features.envelope_times` | Attack time (10% to 90% of the first rise), decay time (peak to 30 dB down), noise floor and dynamic range. Tells a key press from a pluck from a pad without listening. |
| `SpectralDistance` (`spectralDistance`) | `forge/fit/modal.mel_distance` | How far apart two renders are, in dB, at three time resolutions on a mel scale. Zero for the same sound. Answers "did this change alter the sound?" and "which render is nearer the target?". |

`valis-shot` wraps them: `valis-shot measure file.wav` prints one JSON object (the existing `analyseSignal` report plus
loudness and envelope), and `valis-shot compare a.wav b.wav` prints the spectral distance and the loudness difference.
Tests are in `tests/shot/`, each against a signal whose answer is known (a 997 Hz sine at -23 dBFS reads -23.0 LUFS;
a tone at a quarter of the sample rate has a true peak 3 dB above its sample peak).

On the v3 mix of "Lights On, Nobody Home": -12.3 LUFS integrated, 7.5 LU range, true peak -0.97 dBTP. Streaming services
normalise to about -14 LUFS, so that mix is louder than it needs to be before mastering.

## The neural ears

earshot's other half is two neural models: CLAP, which puts sounds and sentences in one space so that the cosine of a
sound and a sentence says how well they fit, and AST, which labels a sound with the 527 [AudioSet](https://research.google.com/audioset/)
classes. Both run here in C++ through [ONNX Runtime](https://onnxruntime.ai), with no Python at any point.

```
cmake -S . -B build -DVALIS_WITH_ONNX=ON      # fetches the pinned ONNX Runtime 1.31.1 (11 MB, SHA-256 checked)
scripts/shot/fetch-models.sh                   # about 1.1 GB, once, into ~/.local/share/valis/models
build/valis_shot_artefacts/Debug/valis_shot listen render.wav "a plucked nylon guitar" "a synthesizer" "a harp"
```

`listen` prints the eight most likely AudioSet labels and, for each sentence given, its cosine with the sound and its
share among the sentences (a softmax at CLAP's temperature, 38.6647, which is `exp(logit_scale_a)` read from the published weights, so the shares sum to 1). Compare candidate
descriptions with each other: a single cosine on its own means little.

What is in C++: the audio front ends (`ClapFrontend`, `AstFrontend`: the log-mel features each model was trained on),
the byte-pair tokenizer for CLAP's text (`BpeTokenizer`) and the ONNX wrapper (`NeuralEars`, the only file that includes
ONNX Runtime). The front ends and the tokenizer build without ONNX and are tested without a model; the tokenizer
reproduces the known RoBERTa ids (`Hello world` is 0, 31414, 232, 2).

### What it said about "Lights On, Nobody Home"

Run on the rendered stems (measured by the models, not heard by a person). Shares are among the sentences given in each run, so only compare within a row. AST tags are read from the loudest ten seconds.

| Stem | Sentences compared | Result |
|---|---|---|
| guitar | plucked nylon guitar, harp, synthesizer keyboard preset | nylon guitar 99% |
| congas | conga drums against other percussion | congas 98% |
| harp | harp, violin section, trumpet section, synthesizer keyboard preset | harp 39%, synthesizer 36%, trumpet 19%, violin 7% |
| violins | the same four | violin section 45%, trumpet 23%, harp 23%, synthesizer 9%; AST leans to organ and accordion |
| trumpets | the same four | synthesizer keyboard preset 73%, trumpet section 27%; AST says bell |

So the models agree with the listening: the guitar and congas pass as the real thing, the trumpets do not, and the
harp and violins sit in between. They also point to the next work: the trumpets (see `TODO.md`). One caution: AST
reads only one ten-second window, and until it was changed to take the loudest one it tagged quiet openings as
"Silence".

### Weights

The weights are not in git and not downloaded by a build or a test. `scripts/shot/fetch-models.sh` fetches them when
you run it, from pinned commits, and checks each file's SHA-256 before keeping it; a bad download is discarded.

| Model | Source (ONNX export of the original) | Licence | Size |
|---|---|---|---|
| CLAP audio encoder | [Xenova/larger_clap_general](https://huggingface.co/Xenova/larger_clap_general) @ d78c399, from [laion/larger_clap_general](https://huggingface.co/laion/larger_clap_general) | Apache-2.0 | 282 MB |
| CLAP text encoder | the same repository | Apache-2.0 | 502 MB |
| AST | [Xenova/ast-finetuned-audioset-10-10-0.4593](https://huggingface.co/Xenova/ast-finetuned-audioset-10-10-0.4593) @ 249a1fb, from [MIT/ast-finetuned-audioset-10-10-0.4593](https://huggingface.co/MIT/ast-finetuned-audioset-10-10-0.4593) | BSD-3-Clause | 347 MB |

The original models carry the licences shown; the ONNX repositories state none of their own, and are conversions of
those weights. Set `VALIS_MODELS` to keep them elsewhere. The tests skip themselves when the weights are absent.

Known limits: CLAP's front end covers 50 Hz to 14 kHz, so it cannot hear "air" above that (the loudness and spectral
tools can). Sounds longer than ten seconds are embedded as ten-second windows every five seconds and averaged.
CLAP does not know Valis: a cosine says how a sound resembles a described sound, not whether a circuit is well made.

## Not ported, and why

| Idea | Why it is useful | Cost |
|---|---|---|
| **Modal fit** (`fit/modal.py`): a struck recording becomes damped sinusoids found by ESPRIT, then a model that re-renders it | Would turn a recording of a real conga or cowbell into a circuit. Valis's `val:ModalBank` has six modes at fixed ratios, so a free-mode bank (frequency, decay and amplitude per mode, set from the fit) would be needed first. | Large. ESPRIT on sub-bands plus least squares, and a new element. The best of these ideas. |
| **Search by listening** (`forge/critic.py`, `search.py`): vary a recipe's knobs and keep what scores nearest a target | `SpectralDistance` is the missing score. A loop that renders a circuit with `ValisEngine`, scores it against a target wav and moves the exposed `val:Param` values would tune a circuit to match a sound. | Medium. Needs an optimiser and a headless engine loop; no new DSP. |
| **Sound bank as recipes** (`forge/recipes`, 252 UI and film sounds) | Short sounds (clicks, whooshes, risers, drips) described as code. Valis circuits for these would be a useful example set. | Medium, and mostly circuit writing. |
| **Realness** (`realness.py`): real recording or code-synthesised | Would score how acoustic a circuit sounds. | A classifier trained on earshot's own data (`realness_model.json` sits on CLAP features). Could be ported now that CLAP runs. |
| **Calibrated match** (`match.py`, `vocab.py`): a z-score among 322 reference descriptions, so a cosine reads as strong or weak | `listen` gives raw cosines and shares among the sentences you supply. | Small: port `vocab.json` and its statistics. |
| **Timeline and sync** (`timeline.py`): events against picture cuts | Only matters for scoring to video. | Not relevant yet. |
| **Stripping a bed**, **gesture/spectral fitters** | Fitting noisy sounds such as whooshes. | Large, narrow use. |

The two worth doing next are the free-mode modal bank with a fitter, and a parameter search that uses
`SpectralDistance`. Both are listed in `TODO.md`.
