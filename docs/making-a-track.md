# Making a track, from circuits to Reaper

This is the whole route from an idea to a track open in [Reaper](https://www.reaper.fm/), as it was done for "Street Hammer" and
"All Fall Down" (both in the artifacts repository, `~/github/valis-artifacts`). Each step says which tool does it and what to
check before moving on. The vocabulary of the song and score files is in [songs.md](songs.md), and the `write-a-song` skill
is the short version of this page. Everything here is measured and not heard, and any report should say so.

The route is:

1. Plan the track as numbers.
2. Make the instrument circuits.
3. Write one generator script that produces the score and the song.
4. Play the score into MIDI files.
5. Render a stem for each part.
6. Check the stems.
7. Mix, and check the balance.
8. Keep the result.
9. Open it in Reaper.

## 1. Plan the track as numbers

Fix these before writing anything: tempo, key, the sections and their lengths in bars, the chord loop, the tunes, and which part
plays in which section. A conventional dance track is intro 16, build 8, drop 16, break 16, build 8, drop 24, outro 16 bars.
Keep the plan in one table in the generator script (step 3), so a changed arrangement is a changed table.

If a tune is quoted, check that it is out of copyright and say so in the README. Traditional tunes such as nursery rhymes are.
Write a melody on the scale of the key, so that a tune from a major key becomes minor by moving every note to the same degree of the minor
scale ("Twinkle, Twinkle" is degrees 1 1 5 5 6 6 5 and so on). Notes outside the range of the circuit that plays them are a
mistake, so check each instrument's stated range.

## 2. Make the instrument circuits

A circuit is a Turtle file in `examples/` of the Valis repository. Hand-written Turtle needs one block for each connection and runs to hundreds
of lines, so circuits are written by a short Python script that expands a list of nodes and arcs. Copy
`scripts/nursery/nursery.py` (eight circuits) or `scripts/electro83/moog-source.py` (one), which use `scripts/halloween/circuits/gen.py`.
The `generate-circuit` and `new-element` skills cover the details. The points that have caught real mistakes:

* Look up every element before using it: `describe_element` (MCP) or `grep` the ontology `vocabs/valis.ttl`. A port's unit and range
  matter (a `val:Tape` hiss of 0 dB is full-scale noise).
* **A control arc replaces the value of the port it drives.** Put the resting value in the control source (the `min` of a `val:Scale`),
  not on the element. The lint test fails a circuit that sets a value an arc overwrites; it caught this in the nursery hat.
* A circuit must be silent until played. Render with no note and look at the peak.
* Finish with a DC-blocking high-pass, a `val:Gain` and a `val:Limiter`.
* Declare a `trn:PluginProfile` (role, accepts, produces) or the lint test fails the file.

Then run the script, build everything and check each circuit:

```sh
python3 scripts/nursery/nursery.py
cmake --build build                      # the whole build, not one target
build/valis_render_artefacts/Debug/valis_render examples/nursery-humpty-kick.ttl --note 36 --seconds 1 -o /tmp/kick.wav
build-release/valis_shot_artefacts/Release/valis_shot measure /tmp/kick.wav
```

Read the log of every render. `nan`, `exceeds full scale` and `arc joins` mean a failed render. For a pitched instrument measure the
pitch at several notes across its range (a few cents is the limit). Measure with a window that excludes echoes and tails, or the
reported pitch is wrong: the arp read 13 cents sharp until the window was cut to the note itself.

Add an engine test, `tests/engine/NurseryInstrumentsTest.cpp` is the pattern: silent until played, peak in range, tuned where pitched,
silent after release, and one check for the thing each instrument is for. Register it in `tests/CMakeLists.txt`, add each circuit to
`docs/examples.md`, and run the suite (`cd build && ctest -E OpDispatcher -j8`; the OpDispatcher test is slow and runs when ops change).
Neural checks are in the `hear-a-sound` skill.

## 3. Write one generator script for the score and the song

Put it in the artifacts repository, `scripts/<track>/make.py`, and its output in `artifacts/<track>/`. The layout is in
`docs/dir-layout.md` there: `circuits/`, `midi/`, `wavs/` (ignored by git), `mixes/`, a `README.md`. Copy `scripts/all-fall-down/make.py`.
It writes three things:

* `circuits/`: copies of the Valis example circuits the track uses, so the track does not change if the examples do.
* `score.ttl`: chords, patterns (a string of steps for each lane, `x` a hit, `X` an accent, `.` a rest), "feel" (small random timing and
  velocity), and plays (which part plays which pattern in which bars of which section). A chord has `val:root` and `val:pickup` for
  a bass, and `val:tones`, three keys lowest first, for the voices `low` and `top`. A tune is a list of
  `(step, note, length)` and becomes one lane for each distinct note and length.
* `song.ttl`: the tempo, the sections (with a level in dB for the whole mix while it plays), the parts (circuit, level, pan,
  high-pass) and the returns (effects fed by a part).

Rules that matter:

* A pattern restarts at the first bar of each play, so a play covering bars 5 to 8 of a section plays the first four bars of an eight-bar
  tune. Use this to give a promise of a hook before it arrives.
* A play may carry its own `val:progression`, to change the chords under a section without changing the others.
* A mono instrument needs a gap between notes: give each note a gate of about 0.9 of its length. A note that touches the next
  does not retrigger an envelope.
* Anything at a point in time is placed by stream position, not by the block (see CLAUDE.md).
* **`val:level` on a part** is the rms in dB the stem is levelled to over the windows where it plays. On a part with
  **`val:follows`**, the level is instead an offset in dB added to the leader's gain, and cannot be above 0. For a sparse part that
  must be louder than its leader's gain would make it, do not use `follows`: level it alone.
* Section levels (`val:level` on a Section) are applied by `mix --arc`. Quiet sections made only of sparse parts come out far below the
  drops unless their section level is raised; compare the section levels in the mix report.

## 4. Play the score into MIDI

```sh
valis-song score artifacts/<track>/song.ttl          # one MIDI file for each part the score plays
```

`valis-song` is `build/valis_song_artefacts/Debug/valis_song` (or the Release one, which is several times faster for rendering; rebuild it
with `cmake --build build-release --target valis_song valis_render valis_shot` after any change to the code, or it is stale). The same
operations are MCP tools (`score_song`, `render_song`, `check_stems`, `mix_song`, `reaper_project`).

## 5. Render the stems

```sh
valis-song render artifacts/<track>/song.ttl         # wavs/<part>.wav for each part and return
```

Use the Release tool. A stem that fails to load or holds a NaN is named and not written. Several stems render at once. The render needs the
whole of the song's length plus its tail, so it takes a few minutes. Run it in the background if the session should stay free.

## 6. Check the stems

```sh
valis-song stems artifacts/<track>/song.ttl          # compares each stem with its MIDI bar by bar
valis-shot measure artifacts/<track>/wavs/<part>.wav # peak, rms, clipping, pitch, brightness
```

The JSON for `stems` goes to stderr when it reports problems. Look for silence in a bar where the score has notes, a stem that sits at
the limiter on every note, and a number that does not move when the inputs move (the wrong binary is running).

## 7. Mix, and check the balance

```sh
valis-song mix artifacts/<track>/song.ttl --version v1 --arc
```

The mix levels each stem, pans it, adds a synthetic room and a soft clip, applies the section levels (`--arc`), and brings it to the
song's `val:loudness` (-10 LUFS) while holding the true peak at -1 dBTP. The reply gives the loudness, the loudness range, the level of
each section and the gain of each stem. **A mix is never replaced**: use a new version number each time (`v2`, `v3`) and keep the old.

The report does not say whether the parts balance. Check it by applying each stem's gain and printing its rms over a drop (the WAVs are
32-bit integer, so divide by 2^31). In the first "All Fall Down" mix this showed the snare 25 dB and the hat 45 dB under the kick
(see MISTAKES.md, "A follower set to a target level"). A loudness range above about 12 LU for a dance track usually means the intro and
break are too low: raise their section levels.

## 8. Keep the result

* A FLAC of the final mix in `mixes/` (`ffmpeg -i mixes/<track>-v3.wav -compression_level 8 mixes/<track>-v3.flac`). The WAV mixes and
  all stems are ignored by git.
* A `README.md` for the track: what it is, structure, the tunes and their sources, the mix numbers, what is measured and not heard,
  what to listen for, and the notes from making it. Add a row to the artifacts repository's `README.md`.
* Log anything that went wrong in `MISTAKES.md`, and a TODO entry for what needs a listener.

## 9. Open it in Reaper

```sh
valis-song reaper artifacts/<track>/song.ttl --midi --output <track>-midi.rpp --overwrite
```

`--midi` writes a track for each part holding its MIDI file, with the volume and pan from the mix. Each track also holds a Valis
instrument (VST3, "Valis (danja)") that already has the part's circuit and the MCP server switched on, as the instrument's own
saved state. The first track is on port 7677 and the rest follow in track order (the ports start above 7676 because the Valis MCP
server in the development environment uses that one). `curl localhost:<port>/health` names the track.

Then, with the user's confirmation that an empty project is open in a running Reaper:

1. Call `mcp__reaper__load_project` with `project_path` (not `path`).
2. Check the instruments: `curl -s localhost:<port>/health` for each port, and `list_params` over `POST /mcp` to see that the
   parameters hold the circuit's declared values. Valis tools reach each instance by its own port, so call them with `curl` and not
   the `mcp__valis__` tools, which are bound to one.
3. Tell the user to press play. Say that Reaper plays the raw instruments and not the normalised stems, so the balance can differ from the
   mix, and that a reverb return is not a track in the project.

Things that went wrong, and why:

* The saved state of an instrument has to carry a `PARAM` value for each of the 64 slots, or none. A state with none now keeps the
  circuit's declared defaults (`ValisProcessor::setStateInformation`); before that fix every parameter dropped to the bottom of its
  range and the bass was dull and quiet. The plugin must be installed (`./install.sh`) and Reaper restarted for the fix to apply.
* **If the user saves the project in Reaper, Reaper writes the instruments' current state back**, including any wrong parameters. Regenerate
  the `.rpp` with the command above, and ask the user to open it with File, Open and not to save over it.
* `reaper-mcp` can drop its connection ("Broken pipe"). Retry once, then ask the user to open the file. Do not retry in a loop.
* `render_stems` through `reaper-mcp` ran for ten minutes with no output. Render stems with `valis-song render` instead.
* **A speech track** is added by hand in the live project. Put the words in the generator (a table of section, bar, beat, words), write
  them as one word per step with `_` for rests, and load that into a speech synth such as SAM (`mcp__sam__set_text`, Transport mode).
  Add a Valis instance after it with `mcp__reaper__add_fx`, ask the user to switch its MCP on (a new instance has it off), then load a
  vocoder circuit over that port with `load_file`. The voice is not in the stems or the mix. See `scripts/nursery/voice-vocoder.py`.
* The track's circuit is embedded as text at the time the project is written. Change a circuit, then regenerate the project.

The writer is `src/ops/SongReaper.cpp`; the test, `tests/ops/SongReaperTest.cpp`, decodes a written instrument state and checks its sizes,
its XML and its escaping. The state is Reaper's VST3 wrapper (a header of sizes) around the plugin's own JUCE block (`VC2!`, a size, an XML
text, private data), encoded as standard base64 (not `juce::Base64`, whose alphabet differs).

## Checklist

* Circuits: script run, lint and the suite pass, silent with no note, pitch measured across the range, engine test added.
* Generator: one table of sections, a score and a song written from it, sources of any quoted tunes named.
* Stems: no `nan`, no silent bars, nothing at the limiter on every note.
* Mix: a new version, loudness range reasonable, per-stem balance printed, FLAC kept, README written.
* Reaper: `--midi` project regenerated after any change, ports checked, the user told it is measured and not heard.
