# Valis for a newcomer

This is a short, practical guide to the tools in this repository. It assumes you can use a terminal and have never
used Valis. It does not explain sound design; it shows how to make a sound, change it, and check it.

## What Valis is

Valis builds the sound of electronic instruments and effects from a text description. You describe a circuit (a
filter, a string, a drum) in a plain text file, and Valis turns it into audio. The same circuit can run as a
plugin in a music program, as a standalone app, or from the command line to write a sound file.

Nothing here needs a music program. The command line is the fastest way to learn.

## Words you will meet

| Word | Meaning |
|---|---|
| **Circuit** | A text file that lists parts and how they are joined. Every file in `examples/` is one. |
| **Element** | One part of a circuit: an oscillator, a filter, a reverb, a plucked string. There are 85, listed in [`docs/manual/elements.md`](manual/elements.md). |
| **Arc** | A connection from one element's output to another's input. |
| **Turtle** | The text format circuits are written in ([W3C Turtle](https://www.w3.org/TR/turtle/)). You can read it like a list of "this thing is a filter with a cutoff of 800". |
| **Render** | Run a circuit offline and write the result to a `.wav` file. |
| **MIDI** | A file or message that says which notes to play and when ([MIDI](https://en.wikipedia.org/wiki/MIDI)). Instruments in `examples/` are played by MIDI notes. |
| **Stem** | The rendered sound of one instrument on its own, before it is mixed with the others. |
| **Mix** | Several stems added together with levels and pan positions. |

## 1. Build it

You need a C++ compiler, CMake and [JUCE](https://juce.com) (the audio framework). The paths are set up for the
author's machine, so read the Requirements section of the [`README`](../README.md) first. Then:

```sh
./build.sh                     # builds, runs all the tests, then builds the release plugin
```

If you only want the command line tools and the tests, build the debug configuration alone:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVALIS_BUILD_TESTS=ON
cmake --build build
```

The tools end up here:

| Tool | Path | What it does |
|---|---|---|
| `valis-render` | `build/valis_render_artefacts/Debug/valis_render` | Runs a circuit and writes a `.wav` file. |
| `valis-shot` | `build/valis_shot_artefacts/Debug/valis_shot` | Measures a `.wav` file, compares two, and (optionally) describes one with neural models. |
| `./valis` | the repository root | Opens the standalone app, where you can edit circuits and play them. |

Always build everything (`cmake --build build` with no target) before measuring a sound. Building a single test
leaves the tools linked to older code, and they will report on the old sound.

## 2. Hear something in two minutes

Run a simple filter on a 110 Hz tone and write two seconds of it:

```sh
R=build/valis_render_artefacts/Debug/valis_render
$R examples/basic.ttl --tone 110 --seconds 2 -o basic.wav
```

Open `basic.wav` in any audio player. The `--tone` option feeds a sine wave into the circuit's input. That works
for effects and filters. An instrument has no input; you play it with a note instead:

```sh
$R examples/nylon-guitar.ttl --note 52 --velocity 0.8 --seconds 3 --gate-off 0.5 -o guitar.wav
```

That plays MIDI note 52 (E3) at 80% velocity, holds it for half a second, and records three seconds so the string
can ring out. Note numbers: 60 is middle C, and each step up is one semitone ([note numbers](https://en.wikipedia.org/wiki/Scientific_pitch_notation)).

Some other things to render: `examples/flute.ttl`, `examples/cello.ttl`, `examples/harp.ttl`,
`examples/trumpets.ttl`, `examples/808-bd.ttl`. [`docs/examples.md`](examples.md) says what each one is.

## 3. Read a circuit

Open `examples/basic.ttl`. It is the smallest useful circuit, and it has four parts:

```turtle
:basic a val:Circuit ;
    val:element :in , :vcf , :drive , :out ;      # the parts
    val:arc :a1 , :a2 , :a3 .                     # the wires

:in  a val:Input .
:vcf a val:Ladder ; val:cutoff 800.0 ; val:resonance 0.4 .    # a low-pass filter
:drive a val:DiodePair .                                      # a soft clipper
:out a val:Output .

:a1 a val:Arc ; val:from [ val:node :in  ; val:port "out" ] ;
                val:to   [ val:node :vcf ; val:port "in"  ] .
```

Each line says one thing. The sound goes `in`, then `vcf` (the filter), then `drive`, then `out`. The block of
`val:Param` lines at the bottom makes some settings (cutoff, resonance, drive) available as knobs in a plugin.

To see what an element accepts, read its entry in [`docs/manual/elements.md`](manual/elements.md). Every port is
listed with its range and what it does.

## 4. Change it and check the change

Edit `val:cutoff 800.0` to `200.0`, render again to `dark.wav`, and compare the two files:

```sh
S=build/valis_shot_artefacts/Debug/valis_shot
$S compare basic.wav dark.wav
```

You get one line of numbers. The first, `spectralDistanceDb`, is how different the two sounds are: 0 means the same,
a few dB is a small change, tens of dB is a very different sound. `loudnessDifferenceLu` is how much louder or
quieter the second is.

This is how you tell whether a change did anything when you cannot, or do not want to, listen to every version.

## 5. Play a tune

`--midi` plays every note in a MIDI file instead of a single note:

```sh
$R examples/harp.ttl --midi tune.mid --tempo 120 --rolling --seconds 20 -o tune.wav
```

`--rolling` tells circuits that follow the beat that the song is playing. Without it they behave as if stopped.
Make a MIDI file in any music program, or see [`.claude/skills/write-a-song/SKILL.md`](../.claude/skills/write-a-song/SKILL.md)
for the script-based way songs are made here: one MIDI file per instrument, rendered to one stem each, then mixed.

A finished example lives in the separate `valis-artifacts` repository. "Lights On, Nobody Home" was made with the
instruments in `examples/` (the nylon guitar, harp, guitarron, trumpets, violins and percussion).

## 6. Check a sound without listening

`measure` prints one JSON object about a file:

```sh
$S measure guitar.wav
```

Read these parts first:

| Field | What it tells you | A warning sign |
|---|---|---|
| `signal.notes` | Plain-language findings: silence, clipping, a DC offset, values that are not numbers. | Anything listed here. Read it before the numbers. |
| `signal.pitch` | The note it found, and how far off in cents. | `found: false` for an instrument that should be pitched. It can also land an octave off on a plucked string whose second harmonic is strong: the guitar note above, played as E3, is reported as E4. Check against the note you asked for. |
| `loudness.integratedLufs` | Overall loudness ([LUFS](https://en.wikipedia.org/wiki/LKFS)). Streaming services aim near -14. | Above about -9 is very loud. |
| `loudness.truePeakDbtp` | The highest level, including the peaks between samples. | Above -1 may distort when converted to MP3. |
| `envelope.attackMs` | How fast the sound starts. A drum is a few ms; a bowed string is hundreds. | A pluck with a slow attack is not a pluck. |
| `envelope.decayMs` | How long until the sound is 30 dB down. `-1` means it never gets there. | A short note that ends in 80 ms when you wanted it to ring. |

These numbers describe the sound. They cannot say whether it is pleasant. Treat them as a check that something is
not broken, not as a judge of quality.

## 7. Ask neural models what it sounds like (optional)

`valis-shot listen` uses two neural models to put the sound into words. It needs a separate build option and a
one-time download of about 1.1 GB. Nothing downloads unless you run the script yourself.

```sh
cmake -S . -B build -DVALIS_WITH_ONNX=ON       # fetches the ONNX Runtime library, pinned and checked
cmake --build build
scripts/shot/fetch-models.sh                   # downloads the models into ~/.local/share/valis/models
```

Then give it a file and some sentences to compare:

```sh
$S listen guitar.wav "a plucked nylon string guitar" "a harp" "a synthesizer keyboard preset"
```

The output has two parts:

* `tags`: the most likely labels from [AudioSet](https://research.google.com/audioset/), a list of 527 sound
  classes, each with a probability. This is the [Audio Spectrogram Transformer](https://arxiv.org/abs/2104.01778).
* `matches`: for each sentence, a `cosine` (how close the sound and the sentence are, from -1 to 1) and a `share`
  (the sentences compete, and the shares add up to 1). This is [CLAP](https://github.com/LAION-AI/CLAP), which
  puts sounds and sentences in the same space.

Read the **share**, not the cosine. A cosine on its own means little; it only means something against another
sentence. For example, on the rendered nylon guitar the plucked-guitar sentence took 99% of the share, and AST's
own labels were Music, Musical instrument, Guitar. On the trumpets of the same song the "synthesizer keyboard
preset" sentence took 75% against 24% for "a trumpet", which agreed with a listener who said the same thing.

Limits to know about:

* The models see about ten seconds at a time. For a long file, AST reads the loudest ten seconds.
* CLAP's input stops at 14 kHz, so it cannot hear very high "air".
* A model resembles a human ear only roughly. If it disagrees with your ears, trust your ears.
* This is work in progress. See [`docs/shot.md`](shot.md).

## 8. Other ways in

| If you want to | Use |
|---|---|
| Edit circuits visually, with a graph and knobs | `./valis` (the standalone app) |
| Drive Valis from another program or an AI assistant | The built-in MCP server: [`docs/manual/mcp.md`](manual/mcp.md) |
| Look up an element | [`docs/manual/elements.md`](manual/elements.md) |
| Write a new instrument from a short description | The `generate-circuit` skill in `.claude/skills/` |
| Check that an instrument is in tune and rings properly | The `test-an-instrument` skill |
| Make a whole song | The `write-a-song` skill, then the `reaper-from-stems` skill to mix it in [Reaper](https://www.reaper.fm) |
| Make lyrics and a sleeve for a finished track | The `release-pack` skill |

If you use Claude Code in this repository, the skills are available as commands. Otherwise they are ordinary
Markdown files you can read.

## 9. When something goes wrong

| What you see | Likely cause |
|---|---|
| The render is silent | An instrument with no note. Add `--note 60`. Or an effect with no input. Add `--tone 220`. |
| `valis-render` says an element is unknown | A typo in the circuit. The error names the line; check the spelling against `docs/manual/elements.md`. |
| A warning "arc joins a control output to a audio input" | You connected a control signal (like an envelope) to an audio input. Use `val:ControlMultiply` for control signals, not `val:VCA`. |
| An instrument is cut off the moment the note ends | The release time is shorter than the sound needs. Plucked and struck sounds need a release at least as long as their ring. |
| Measurements describe an old sound | You built one target only. Run `cmake --build build` with no target. |
| `valis-shot listen` says models are missing | Run `scripts/shot/fetch-models.sh`. |
| A test fails after you edit a circuit | Run `ctest --test-dir build -R Lint`. It checks that every part is connected and no fixed value is hidden by a control. |

## 10. Where to read next

* [`README.md`](../README.md): the whole project, status, and how to build.
* [`docs/examples.md`](examples.md): every shipped circuit.
* [`docs/features.md`](features.md): what Valis can do.
* [`docs/shot.md`](shot.md): the measuring and listening tools in detail, and where they come from.
* [`MISTAKES.md`](../MISTAKES.md): errors made while building this, and how to avoid them. Worth skimming before you write your first circuit.
