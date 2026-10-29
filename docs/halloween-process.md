# How "Dead at Midnight" was made

This records how the Halloween set in `examples/halloween-*.ttl` became a three minute
track, so the work can be repeated, changed or done differently. The brief was in
`docs/halloween-examples.md`: build a set of instruments with MCP tools, then use them
to make a Halloween song in Reaper.

The result is in `~/Music/dead-at-midnight/`. The sleeve is
`docs/dead-at-midnight-sleeve.svg` and the words are in `docs/dead-at-midnight-lyrics.md`.

## The short version

1. Each instrument was designed and checked through the Valis MCP server (`describe_element`,
   `load_file`, `lint`, `render`, `analyze`, `check_stability`) and saved as a Turtle file in
   `examples/`.
2. The song was written as a Python script that writes one [MIDI](https://en.wikipedia.org/wiki/MIDI)
   file per instrument.
3. Each circuit was rendered offline against its MIDI file with `valis-render`, giving one audio
   stem per instrument.
4. `reaper-mcp` built a Reaper project, imported the stems and set the mix.

Reaper does not run the instruments. It holds and mixes recordings of them. The reason is in
[Why the stems are not live plug-ins](#why-the-stems-are-not-live-plug-ins).

## The instruments

| file | what it is | MIDI notes |
|---|---|---|
| `halloween-drums-low.ttl` | kick with a long sub tone, two low toms into a dark hall, a noise slam, through tape and a limiter | 36 kick, 39 slam, 41 and 45 toms |
| `halloween-drums-high.ttl` | closed hat, open hat through a feedback frequency shifter, shaker, wood block into an echo, clap into a hall | 39 clap, 42, 46, 70, 75 |
| `halloween-bass.ttl` | three layers: sine sub an octave down, detuned saws through a ladder filter with a tempo-synced wobble, clipped wavetable growl | play from E2 (40) up; the sub sounds an octave below |
| `halloween-organ.ttl` | eight voices of six sine drawbars at the [Hammond](https://en.wikipedia.org/wiki/Hammond_organ) footages, key click, overdrive, chorus as a scanner vibrato, large room | polyphonic |
| `halloween-guitar.ttl` | a plucked string (a comb filter with allpass dispersion, close to [Karplus-Strong](https://en.wikipedia.org/wiki/Karplus%E2%80%93Strong_string_synthesis)) with pickup, amp clip, spring-style reverb and a dotted-eighth echo | in tune near G3 to G4 |
| `halloween-theremin.ttl` | sine with a scoop into each note, vibrato and drift, phaser, echo, hall | one note at a time |
| `halloween-ornaments1.ttl` | two drifting vowel wavetables and a modal glass bell, with chorus, spectral delay and hall | one note at a time |
| `halloween-ornaments2.ttl` | a falling-saucer dive, wavefolded, bit-crushed and smeared by a feedback frequency shifter, with a stuttering echo | short notes |

Each header comment says how to play it. All of them lint clean, pass `check_stability`, and are
covered by the existing tests that run over every file in `examples/`.

### Design notes worth keeping

- **A control arc replaces the value it drives.** Every resting value (a filter's base cutoff,
  an oscillator's pitch) lives in the control path, for example in a `val:Scale`. `lint` caught
  none here because it was kept in mind from the start, but it is the commonest mistake.
- **Pitch ratios use `val:ControlMultiply`.** A sub-oscillator, a drawbar footage and a detune
  are each the keyboard pitch times a fixed ratio. There is no control-rate add, so a detune in
  cents or a fixed offset in hertz cannot be written yet.
- **The theremin's glide is faked.** There is no slew element, so each note scoops up from about
  a semitone below through an envelope. See `TODO.md`.
- **The guitar string is flat and gets flatter as it rises** (about 25 cents at E3, 104 at E5),
  because the loop filters add delay that the tuning does not subtract. The circuit raises the
  pitch by 2.5 per cent as a stopgap, which centres it near G3 to G4. The song keeps the guitar
  in that range.
- **Polyphony is a `val:Subcircuit` with `val:voices`.** The organ is written once as a voice and
  stamped out eight times. See `docs/manual/circuits.md` and `examples/polysynth.ttl`.
- **Levels.** Several instruments came out too hot or too quiet on the first render. Fix levels
  with a `val:Gain` before the limiter, then confirm with `render` and `analyze`.
- **DC offset.** The first renders of the slam, bass, guitar and ornaments2 had a DC offset that
  `analyze` reported. A `val:OnePole` set to high-pass at 22 to 45 Hz removes it.

## The song

140 BPM, 4/4, E minor, half-time feel (kick on the first beat, snare on the third). Chords change
every two bars: Em, D, C, B. 104 bars, about 2:58. A bar lasts 1.714 seconds.

| section | bars | time | what plays |
|---|---|---|---|
| intro | 1 to 8 | 0:00 | organ, ghost choir, theremin from bar 5, a few toms |
| verse 1 | 9 to 24 | 0:14 | low drums, hats, guitar riff, bass from bar 13, organ, ghost choir every four bars |
| build | 25 to 32 | 0:41 | kick speeds up, hat and snare crescendo, saucer stabs |
| drop 1 | 33 to 48 | 0:55 | full bass, full drums, guitar, organ |
| break | 49 to 56 | 1:22 | organ, theremin, ghost choir, ornaments, sparse drums |
| verse 2 | 57 to 72 | 1:36 | as verse 1, with the theremin from bar 65 |
| drop 2 | 73 to 88 | 2:03 | as drop 1, with the theremin lead |
| outro | 89 to 104 | 2:31 | layers fall away |

The notes are written by `score/song.py`, which places every note on a sixteenth-note grid, one
bar at a time. Hi-hat velocities use a fixed random seed, so a run always gives the same MIDI.

## Making the stems

`valis-render` gained a `--midi <file>` option for this. It reads every note-on and note-off in a
standard MIDI file and places each on the sample timeline at `--tempo`. An event fires in the block
that contains it, at its own offset, never by comparing against a block start (see the rule on
this in `CLAUDE.md`). Only note events and one fixed tempo are read.

```sh
cmake --build build                       # build everything first; see below
D=~/Music/dead-at-midnight
build/valis_render_artefacts/Debug/valis_render examples/halloween-bass.ttl \
    --midi $D/midi/bass.mid --tempo 140 --rolling --seconds 186 -o $D/stems/bass.wav
```

- `--rolling` reports a playing transport, which the tempo-synced wobble and any other timing
  that follows the host need. Without it they free-run.
- `--seconds 186` is the song plus a tail for the reverbs and echoes.
- The output is 24-bit stereo at 48 kHz. The eight renders took about twelve seconds each on a
  Debug build and ran in parallel.
- Build everything (`cmake --build build`, no target) before rendering, as `CLAUDE.md` says. A
  targeted build leaves the tool linked against old code.

## The Reaper project

`reaper-mcp` created `dead-at-midnight.rpp` at 140 BPM with eight audio tracks, imported each stem
at 0:00, and set these levels and pans:

| track | volume | pan |
|---|---|---|
| Drums Low | -6 dB | centre |
| Drums High | -4 dB | 25% right |
| Bass | -3 dB | centre |
| Organ | +4 dB | centre |
| Guitar | -6 dB | 30% left |
| Theremin | -6 dB | 20% right |
| Ornaments 1 Ghost Choir | -8 dB | centre |
| Ornaments 2 Saucer | -5 dB | centre |

The master is at -5 dB with a ReaLimit limiter. The summed stems peak above 0 dB, so the master
level and limiter matter. The master has two ReaLimit plug-ins because of a `reaper-mcp` bug
(`TODO.md` in that repository).

Reaper's own render through `reaper-mcp` failed, so the first mix was made by summing the stems in
Python with the same gains and pans (`dead-at-midnight-reference-mix.wav`). The project was then
rendered by hand from Reaper's own Render dialog, which worked: `dead-at-midnight-stems.wav`, 186
seconds, 24-bit stereo at 48 kHz, peak -0.01 dBFS, loudness about -11 dB RMS with nothing clipped.
That is the file to use. Saving the project with its media copied gave `dead-at-midnight-stems.RPP`
and a `Media/` folder holding the stems, so that project is self-contained and `dead-at-midnight.rpp`
(which points at `stems/`) is the lighter one. The render is 186 seconds because the stems are; trim
the silent tail after the last echo if a release needs an exact length.

## Why the stems are not live plug-ins

Each Valis plug-in instance starts its own MCP server, all on the same port (7676 by default), and
a circuit chosen over MCP lives only in that instance. So an agent cannot give eight instances in
one Reaper session eight different circuits. Setting a circuit from a file in the plug-in's saved
state, and letting a second instance take the next free port, would fix it. Until then, a
project holds recordings.

## Setting up the tools

What had to be true before any of this worked. None of it is obvious from the error messages.

**Valis MCP server.**
- It is off by default. Turn it on in Settings, MCP Server, in the plug-in or Standalone, or start
  the program with `VALIS_MCP_PORT` set.
- Check it with `curl -s localhost:7676/health`.
- Register it with Claude Code: `claude mcp add --transport http valis http://localhost:7676/mcp`.
- A new Claude Code session is needed before the tools appear.

**reaper-mcp** (`/chalet/github/reaper-mcp`).
- Install it in a virtualenv. The system Python is externally managed.
- It needs `mcp` below version 2, because version 2 removed `mcp.server.fastmcp`. The pin is in
  its `pyproject.toml`.
- Configure Python in Reaper: Preferences, Plug-ins, ReaScript, enable Python, and set the library
  path to the shared library for the same Python version as the virtualenv.
- Run `scripts/enable_reapy.py` once from Reaper (Actions, Show action list, Load ReaScript), then
  restart Reaper. It enables the distant API used by [reapy](https://github.com/RomeoDespres/reapy).
  The script looks for a `venv` directory in the checkout. Reaper does not define `__file__`, so it
  falls back to `~/github/reaper-mcp`.
- Register it with Claude Code: `claude mcp add --scope user reaper -- /path/to/.venv/bin/reaper-mcp-server`.
- Keep Reaper running while it is used.

## Things that went wrong

All are in `TODO.md` in this repository or in `reaper-mcp`'s `TODO.md`. The ones that cost time:

- No Valis or Reaper MCP tools were visible because neither server was registered with Claude Code.
- The Valis server was not running, because it is off by default.
- `reaper-mcp` crashed on start-up with `mcp` 2.x, and the enable script failed inside Reaper.
- `add_master_fx` and `apply_limiter` add the effect and then report an error, so retrying adds
  another copy. `render_project` outran the MCP timeout and wrote no file; rendering from Reaper's own dialog worked.
- `get_diagnostics` reports a sample rate of 8000 for the live Standalone while `render` uses 48000.
- `valis-render --midi` first used a relative path without resolving it against the working
  directory, which tripped a JUCE assertion. It is recorded in `MISTAKES.md`.
- `analyze` named a 1850 Hz wood block as 925 Hz, an octave low.

## Where everything is

In this repository:

- `examples/halloween-*.ttl`: the eight instruments.
- `docs/examples.md`: a short description of each, under "Halloween set".
- `docs/halloween-examples.md`: the original brief.
- `docs/dead-at-midnight-sleeve.svg` and `.png`: the sleeve, in the style of a medieval woodcut.
- `docs/dead-at-midnight-lyrics.md`: the lyrics, laid out against the bars above.
- `src/tools/RenderMain.cpp`: the `--midi` option.

In `~/Music/dead-at-midnight/`:

- `dead-at-midnight.rpp`: the Reaper project.
- `stems/`, `midi/`: the audio and the notes it was made from.
- `score/song.py`, `score/midiw.py`: the composition script and its MIDI writer.
- `score/circuits/`: the scripts that generated the circuit Turtle files (`gen.py` is the shared
  helper). The files in `examples/` are the product and can be edited directly; the scripts are
  kept only for reference and are not needed to build or test anything.
- `dead-at-midnight-stems.wav`: the mix, rendered from Reaper by hand. `dead-at-midnight-stems.RPP` and
  `Media/` are the same project with its audio copied in.
- `lyrics.md` and `dead-at-midnight-reference-mix.wav` (the Python sum of the stems, kept for comparison).

## To change something

- **A different part or pattern:** edit `score/song.py`, run it, then re-render only the stems that
  changed and re-import them. Reaper does not notice a changed file until the item is reloaded.
- **A different sound:** edit the circuit in `examples/`, then run `lint`, `render` and
  `check_stability` on it through the MCP server (or `ctest`), and re-render its stem.
- **A different tempo:** change `BPM` in `score/song.py`, and the `--tempo` value in every render.
  The song is written in beats, so the arrangement scales.
