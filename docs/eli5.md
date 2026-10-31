# Valis in five minutes

Valis turns a short text file into a sound. The file says what an instrument is made of, and Valis plays it.

The commands below use two tools, `valis_render` and `valis_shot`. If you do not have them yet, build them first
([Build the tools](#build-the-tools), at the end).

## 1. Make a sound

```sh
R=build-release/valis_render_artefacts/Release/valis_render
$R examples/nylon-guitar.ttl --note 52 --seconds 3 -o guitar.wav
```

Play `guitar.wav`: a plucked guitar string, note 52 (E3; 60 is middle C). Try `harp.ttl`, `flute.ttl` or `808-bd.ttl`
from `examples/`; [`examples.md`](examples.md) says what each one is.

An effect, such as the filter in `examples/basic.ttl`, needs a sound to work on. Give it a tone with `--tone 110`
instead of `--note`.

## 2. Change it and check

Open `examples/basic.ttl` and change `val:cutoff 800.0` to `200.0`. Render again to `dark.wav`, then:

```sh
S=build-release/valis_shot_artefacts/Release/valis_shot
$S compare basic.wav dark.wav
```

`spectralDistanceDb` says how different the two sounds are. 0 is the same, a few is a small change, tens is a new sound.

To check one file, run `$S measure guitar.wav`. Read `signal.notes` first; it lists anything wrong in plain words.

## Where next

| You want to | Go to |
|---|---|
| Understand a circuit file | [`manual/circuits.md`](manual/circuits.md) |
| Look up a part (filter, string, drum) | [`manual/elements.md`](manual/elements.md) |
| Make a whole song | the `write-a-song` skill |
| Ask an AI model if a sound is a trumpet | the `hear-a-sound` skill, and [`shot.md`](shot.md) |
| Drive Valis from an AI assistant | [`manual/mcp.md`](manual/mcp.md) |
| Avoid old mistakes | [`../MISTAKES.md`](../MISTAKES.md) |

## If it breaks

* **Silent:** an instrument needs `--note`, an effect needs `--tone`.
* **It measures an old sound:** you built one target only. Run `cmake --build` with no target.
* **A note stops when the key lifts:** a plucked or struck sound needs a release as long as its ring.

## Build the tools

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DVALIS_BUILD_TESTS=OFF
cmake --build build-release --target valis_render valis_shot
```

That makes two tools: `valis_render` makes a sound and `valis_shot` checks one. You need a C++ compiler and
[JUCE](https://juce.com); the [README](../README.md) says where JUCE goes.
