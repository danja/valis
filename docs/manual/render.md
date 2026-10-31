# valis-render: making a sound without a window

`valis-render` plays a circuit offline and writes a wav file. It needs no window, audio device or host, so it is the
tool for batch work, tests and rendering one part of a song. The same render is available to an agent as the `render`
tool on the [MCP surface](mcp.md).

```sh
valis_render examples/nylon-guitar.ttl --note 52 --seconds 3 -o guitar.wav
```

The tool is built with `cmake --build build` and sits at `build/valis_render_artefacts/Debug/valis_render`. A Release
build (`-DCMAKE_BUILD_TYPE=Release`) is about five times faster and gives the same samples. See [building](building.md).

## What the circuit hears

| You want | Give it |
|---|---|
| An instrument to play one note | `--note 60` (MIDI note number; 60 is middle C) |
| An instrument to play a part | `--midi part.mid --tempo 120` |
| An effect to process a tone | `--tone 110` (a sine at that frequency) |
| An effect to process a recording | `-i input.wav` (mixed down to mono) |

An instrument given none of these renders silence, and so does an effect given no input.

## Options

| Option | Meaning | Default |
|---|---|---|
| `-o FILE` | output wav, 24 bit stereo | `out.wav` |
| `-i FILE` | input wav | silence |
| `--tone HZ` | a sine as the input, in place of `-i` | off |
| `--seconds S` | length of the render. With `-i` the input sets the length. | 2 |
| `--rate HZ` | sample rate | 48000 |
| `--block N` | block size in samples. The output should not depend on it. | 512 |
| `--vocabs FILE` | ontology to load | `vocabs/valis.ttl` |
| `--elements` | print the element reference as markdown and exit | |

### Notes

| Option | Meaning | Default |
|---|---|---|
| `--note N` | send a note-on for MIDI note N | none |
| `--velocity V` | 0 to 1 | 1 |
| `--gate-on S` | when the note starts, in seconds | 0 |
| `--gate-off S` | when it is released | 80% of `--seconds` |
| `--midi FILE` | play every note of a standard MIDI file, in place of `--note` | none |

Release comes before the end so the tail of the note is part of the render. A plucked or struck sound keeps ringing
after release, so allow enough `--seconds`.

### Transport

Some elements take their timing from the host: `val:LFO` with `sync`, `val:StepSequencer`, `val:Transport`.

| Option | Meaning | Default |
|---|---|---|
| `--tempo BPM` | tempo reported to the circuit, and the tempo a `--midi` file is placed at | 120 |
| `--rolling` | report the transport as playing from zero | stopped |

Without `--rolling` a tempo-locked circuit sees a stopped host and holds still.

## Playing a MIDI file

`--midi` places each note where a host playing at `--tempo` would put it: the file's ticks over its resolution give
quarter notes, and `--tempo` turns those into seconds. A note starts on its own sample, not at the start of a block.

Limits today:

* Only note-on and note-off are read. Tempo changes, controllers and pitch bend in the file are ignored.
* The tempo is one fixed number from `--tempo`. A song with tempo changes is rendered at the wrong times.
* SMPTE time files are refused.
* Render one part per file and mix the wavs afterwards; see the `write-a-song` skill.

`tests/console/RenderMidiTest.cpp` holds these behaviours.

## What it prints

Two lines: the node and buffer count and the latency, then the number of samples with the peak and RMS. A peak above 1
prints a warning, because the wav is clipped and the circuit needs gain staging. Output that is not finite is an error and
the exit status is 1.

For more than a peak, run `valis-shot measure` on the wav; see [shot.md](../shot.md).

## When it fails

| Message | Cause |
|---|---|
| `ontology: ...` | the vocabulary would not load |
| `file:line:column ...` | the Turtle did not parse |
| `engine: ...` | the circuit did not compile, or an element refused an option |
| `cannot read MIDI file ...` | the path is wrong or the file is not MIDI |
| `unknown option ...` | a misspelt option |

Exit status is 0 on success and 1 otherwise, with no wav written for a failed load.
