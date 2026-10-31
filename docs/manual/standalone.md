# The standalone app

Valis builds as a plug-in (VST3, LV2 and optionally CLAP) and as a standalone application. Both
run the same engine and show the same window, with the same Controls, Graph and Code views. The
standalone needs no DAW: it opens an audio device and a MIDI input of its own, so it is the quickest
way to try a circuit, and the one to use when an agent is driving Valis over
[MCP](mcp.md).

## Starting it

Build it as described in [Building](building.md), then run the launcher from the repository:

```sh
./valis
```

`./valis` starts the Release build if there is one and the Debug build otherwise, and passes any
arguments through. Without a build it says so and stops. The program itself is
`build-release/valis_plugin_artefacts/Release/Standalone/Valis` (or `build/.../Debug/...`).

Environment variables are read at start-up:

| variable | meaning |
|---|---|
| `VALIS_MCP_PORT` | the first port the [MCP server](mcp.md) tries, 7676 by default |
| `VALIS_MCP_TOKEN` | if set, MCP requests need `Authorization: Bearer <token>` |

## Audio and MIDI

The window is a [JUCE](https://juce.com) standalone wrapper, so audio and MIDI are set from its
Options button: choose the output device (and an input, if the circuit processes audio), the
sample rate and buffer size, and the MIDI input that plays instruments. The choice is saved between
runs in `~/.config/Valis.settings`.

An instrument circuit is played from a MIDI keyboard, or from the on-screen keyboard: the **MIDI
Kbd** button in the editor shows it. An effect circuit processes the audio input, so give it an
input device, or load a circuit that makes its own sound.

## Working in it

- **Load Circuit** and **Save Circuit** in the File menu read and write a Turtle file. Everything
  in `examples/` loads this way.
- The **Code** view is the circuit's source. Edit it and the circuit is rebuilt when the text
  compiles; if it does not, the last working circuit keeps playing and the error is shown with its
  line.
- The **Controls** view shows the knobs a circuit exposes. The **Graph** view shows its signal
  flow and lets you edit it.
- The **Console** view takes commands and talks to a language model. See [Console](console.md).

The standalone keeps its own state between runs (the circuit, the sound files chosen with
`val:SampleLoad`, window position), as a plug-in saves it in a DAW project.

## Driving it from an agent

The MCP server is off by default. Turn it on in **Settings, MCP Server**; the menu then reads
"MCP Server (port 7676)". Check it with:

```sh
curl -s localhost:7676/health
```

An agent such as Claude Code connects to `http://localhost:7676/mcp`. See [The MCP surface](mcp.md)
for the tools and [Guide for agents](guide-for-agents.md) for how to build circuits through them.

Run a second copy of the standalone, or load Valis in a host while the standalone is open, and it
takes the next free port (7677, then 7678, and so on). Each copy's `/health` reply names its port,
and the menu shows it. Several copies in one host work the same way, each with a server of its own;
see [Several instances](mcp.md#several-instances).

The standalone and the plug-in differ only in who supplies audio, MIDI and the transport. In a
DAW the host's timeline and tempo reach the circuit. The standalone has no timeline of its own to
report, so tempo-synced elements (`val:LFO` with `sync`, `val:StepSequencer`, `val:Transport`) run
free at 120 BPM, the tempo Valis assumes when a host gives none.

## Without a window

`valis-render` runs a circuit with no GUI, no audio device and no host, which suits batch work,
tests and rendering a part from a MIDI file:

```sh
valis_render examples/halloween-bass.ttl --midi bass.mid --tempo 140 --rolling \
    --seconds 186 -o bass.wav
```

It gives the circuit a playing transport at the tempo you choose, so tempo-synced elements stay in
time. The [valis-render page](render.md) lists every option. See also the
[Halloween notes](https://github.com/danja/valis/blob/main/docs/halloween-process.md) for a worked
example that makes a whole song this way.
