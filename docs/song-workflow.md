# From a prompt to a rendered mix

This is the process an agent follows after being asked to make a song with Valis, from reading the request to
delivering a mixed file. It is the process used for the tracks in [valis-artifacts](https://github.com/danja/valis-artifacts), including
"Street Hammer", "All Fall Down" and "Cumbia Cuántica". It is written as a working order, with the check that
ends each step.

The detail behind each step is elsewhere:

* [Making a track, from circuits to Reaper](making-a-track.md) is the reference route, with every command and the
  faults met on the way.
* [Songs as data](songs.md) is the vocabulary of the song and score files.
* [Measuring a sound](measuring.md) and [valis-shot](shot.md) cover the measurements.
* The `write-a-song`, `model-a-machine`, `generate-circuit`, `test-an-instrument` and `hear-a-sound` skills are
  the short forms of the same work.

Everything here is **measured, not heard**. Say so in the reply, and say what is still waiting for a listener.

## The route

1. Read the prompt and fix the brief.
2. Research the reference, if there is one.
3. Plan the piece as numbers.
4. Find or make the circuits.
5. Write the score and the song.
6. Play the score into MIDI.
7. Render one stem for each part.
8. Check every stem.
9. Mix, and check the balance.
10. Keep the result and deliver it.
11. Hand it to Reaper, if asked.

## 1. Read the prompt and fix the brief

Write down what the request fixes and what it leaves open: the title, genre or reference, length, tempo, key,
form, instrumentation, whether there is a vocal, and what the deliverable is (a mix, stems, a Reaper project, a
release pack). Ask the user only about a choice that would change the work. Do not stop to ask about things you
can decide and state.

Decide the intended result in one sentence, then keep it visible. Every later choice should be checkable against
it: "a three minute dub techno track at 140 bpm with a live bass, a sparse kit and one long delay return".

If the request names a recording or an artist, the brief also needs the limits: what is borrowed (method,
instrumentation, production style, form) and what is not (melodies, patterns as written, samples, names and
logos). See step 2.

## 2. Research the reference, if there is one

When the prompt names a machine, a record or a style, research it before writing anything. The `model-a-machine`
skill has the method: read the maker's or band's own account first, then reference pages, then retrospectives,
and record each claim with its source. Keep the sources when they disagree. Tempo and key databases are estimates
and often wrong; do not use them to fix a key.

Turn the research into requirements: the piece (length, tempo, form, mix) and a table of circuits. For each
circuit, say what it models, what Valis already has to build it from, and what is missing. A missing element gets
a short specification and a `TODO.md` entry. Do not build it unless the user asked for it; make the sound from
what exists instead.

An original piece in a style is the normal answer. A copy of a recording is not: samples of someone else's record
are not used, and a synthesised stand-in is made instead.

## 3. Plan the piece as numbers

Fix these before writing code or Turtle:

* tempo, key and metre;
* the sections and their lengths in bars, for example intro 16, build 8, drop 16, break 16, build 8, drop 24,
  outro 16;
* the chord loop and the tunes;
* the parts, and which part plays in which section;
* the instrumental ranges, so no note falls outside the range of the circuit that plays it.

Keep the plan as one table in the generator script or score file, so a changed arrangement is a changed table and
not a rewrite. A conventional dance track is a useful default, but the form should follow the brief.

If a tune is quoted, check that it is out of copyright and say so in the README. Traditional tunes such as
nursery rhymes are. Write a melody on the scale of the key, so that a tune from a major key becomes minor by
moving every note to the same degree of the minor scale ("Twinkle, Twinkle" is degrees 1 1 5 5 6 6 5 and so on).

## 4. Find or make the circuits

Look in `circuits/` first. Most tracks need a kit, a bass, one or two chord or lead voices and a reverb return,
and an existing circuit is usually closer than an empty file. [Examples](examples.md) describes every circuit,
and the `generate-circuit` skill covers writing one from a short Python description.

A new or changed circuit has to pass these checks before it is used in a song:

* It is silent until played. Render with no note and look at the peak.
* It lints clean. A control arc **replaces** the value of the port it drives, so a fixed value on a port that an
  arc reaches is unreachable; put the resting value in the control source.
* It has a DC-blocking high-pass, a `val:Gain` and a `val:Limiter` where the signal leaves.
* Pitched instruments are in tune across their range, not only at the note used for the test.
* It passes `check_stability`, and an engine test covers the thing it is for.
* It declares a `trn:PluginProfile` with a role, what it accepts and what it produces.

After changing anything under `circuits/`, run the whole suite. Tests pin counts and lint results of shipped
files that have nothing to do with the circuit changed, and a targeted build can leave a tool linked against old
code. Build everything with `cmake --build build` before measuring.

## 5. Write the score and the song

Put the track in the artifacts repository, not in the Valis repository. Its `docs/dir-layout.md` fixes the shape:
`scripts/<track>/make.py` writes `artifacts/<track>/` with `circuits/`, `midi/`, `wavs/` (ignored by git),
`mixes/` and a `README.md`. Copy `scripts/all-fall-down/make.py` or `scripts/dub-metal/track.py`.

A song has two halves.

**The score** decides which notes each part plays. Fixed patterns, chords, feels and the plan of who plays when
can be written as data in `score.ttl`, with `val:Pattern`, `val:Chord`, `val:Feel` and `val:Play`; then
`valis-song score` writes the MIDI. Generated parts, such as a walking bass, a seeded solo or a rain of random
hits, stay in the Python script and write their own MIDI file. A song can use both.

Points that have caught real mistakes:

* Seed every random choice, so a run gives the same MIDI.
* A pattern restarts at the first bar of each play. Use this to give a promise of a hook before it arrives.
* A play can carry its own `val:progression`, to change the chords under one section.
* A mono instrument needs a gap between notes. Give each note a gate of about 0.9 of its length, or a note that
  touches the next will not retrigger an envelope.
* Anything at a point in time is placed by stream position, never by the block index.

**The song** fixes the tempo, the sections and their levels, the parts (circuit, level, pan, high-pass) and the
returns. `val:level` on a part is the RMS in dB the stem is levelled to over the windows where it plays. On a
part with `val:follows`, the level is instead an offset in dB added to the leader's gain, which is the right
shape for sparse percussion that must keep the balance it was played at. A mix target is `val:loudness`.

## 6. Play the score into MIDI

```sh
valis-song score artifacts/<track>/song.ttl
```

This writes one MIDI file for each part the score plays. A part the score does not play keeps its own file, so a
generated part is not lost. Use `--only a,b` to redo named parts.

## 7. Render one stem for each part

```sh
valis-song render artifacts/<track>/song.ttl
```

This writes `wavs/<part>.wav` for each part and return. Each stem is a private copy of its circuit, so several
render at once and the live circuit in a running Valis is not touched. A stem that will not load, or that holds
a NaN, is named and not written; the others still are.

Use the Release tool for the long render, and rebuild it after a code change or it is stale:

```sh
cmake --build build-release --target valis_song valis_render valis_shot
```

The render needs the whole song plus its tail, so it takes minutes. Run it in the background from a small shell
script and poll the log, so the session stays free. Read the log: `nan`, `exceeds full scale` and `arc joins`
mean a failed stem.

## 8. Check every stem

```sh
valis-song stems artifacts/<track>/song.ttl
valis-shot measure artifacts/<track>/wavs/<part>.wav
```

`stems` compares each stem with its MIDI file bar by bar and reports silence where the score has notes, clipping,
non-finite samples and an empty MIDI file. The JSON goes to stderr when it reports problems.

Then judge each part on its own, because other parts hide a fault:

* `measure` for silence, clipping, DC offset, pitch, loudness, attack and decay;
* `is` for each pitched or struck part, for example `valis-shot is wavs/guitar.wav "a plucked nylon string
  guitar"`. Read `closest`, not only the verdict. A synthetic-sounding instrument shows up as a synthesizer
  preset, which is a reason to change the excitation or the physical model, not the level;
* pitch across the instrument's range for any pitched part, with a measurement window that excludes echoes and
  tails.

A number that does not move when the inputs move means the wrong binary is running. Fix stems before mixing them.

## 9. Mix, and check the balance

```sh
valis-song mix artifacts/<track>/song.ttl --version v1 --arc
```

The mix high-passes each stem, levels it to its `val:level`, pans it with a constant-power law, applies the
section levels (`--arc`), adds a fade and a synthetic room, soft-clips, sets the peak to -1 dBFS and brings the
mix to the song's loudness while holding the true peak at or under -1 dBTP. The reply gives the loudness, the
loudness range, each section's level and each stem's gain.

**A mix is never replaced.** Use a new version number each time and keep the old one, so the listener can compare.

The report does not say whether the parts balance. Check it by applying each stem's gain and printing its RMS
over a drop. The WAVs are 32-bit integer, so divide by 2^31. This is how the first "All Fall Down" mix showed
the snare 25 dB and the hat 45 dB under the kick. A loudness range above about 12 LU for a dance track usually
means the intro and break are too low: raise their section levels.

Listen to the result if you can, and report what you measured if you cannot.

## 10. Keep the result and deliver it

* A FLAC of the final mix in `mixes/`:
  `ffmpeg -i mixes/<track>-v3.wav -compression_level 8 mixes/<track>-v3.flac`. The WAV mixes and all stems are
  ignored by git.
* A `README.md` for the track: what it is, its structure, the tunes and their sources, the mix numbers, what was
  measured and not heard, what to listen for, and the notes from making it.
* A row in the artifacts repository's `README.md`.
* Anything that went wrong in `MISTAKES.md`, and a `TODO.md` entry for what needs a listener.

The reply to the prompt should point at the mix, give the measured numbers and name the parts that are still
unheard. A confident description of an unheard balance is the one thing not to deliver.

## 11. Hand it to Reaper, if asked

```sh
valis-song reaper artifacts/<track>/song.ttl --midi --output <track>-midi.rpp --overwrite
```

`--midi` writes a track for each part holding its MIDI file, with the volume and pan from the mix. Each track
also holds a Valis instrument with the part's circuit and the MCP server switched on. The first track is on port
7677 and the rest follow in track order.

With the user's confirmation that an empty project is open in a running Reaper:

1. Load the project with `reaper-mcp`, passing `project_path`.
2. Check each instrument on `localhost:<port>/health`, and check the parameters with `list_params`.
3. Ask the user to press play. Say that Reaper plays the raw instruments and not the normalised stems, so the
   balance can differ from the mix, and that a reverb return is not a track in the project.

Two things to warn about. If the user saves the project in Reaper, Reaper writes the instruments' current state
back, including any wrong parameters, so regenerate the `.rpp` rather than saving over it. And `reaper-mcp` can
drop its connection; retry once, then ask the user to open the file by hand.

## When something fails

| Symptom | Likely cause | What to do |
|---|---|---|
| A stem is missing from `wavs/` | it failed to load, or its MIDI file is absent | read the render log, run `valis-song check`, render that part alone |
| A bar is silent where the score has notes | a gate that overlaps, too few `--seconds`, a part that played once | check the gate, the pattern restart and the stem with `stems` |
| A stem sits at the limiter on every note | gain staging in the circuit | lower a gain before the limiter, render again |
| A part is inaudible in the mix | its level, or a follower set to a target | print the per-stem RMS over a drop |
| A measured number does not move | the wrong binary is running | rebuild everything, then measure again |
| The mix is too quiet or too loud | the loudness target, or the true peak ceiling | read the reply: it says when the ceiling set the gain |
| `is` calls the instrument a synthesizer preset | the model, not the level | change the excitation or physical model, measure again |

## Checklist

* Brief fixed, references researched, what is borrowed and what is not written down.
* The plan is one table: tempo, key, sections, chords, parts and who plays when.
* Circuits: lint and stability pass, silent with no note, tuned across the range, engine tests added.
* Score and song: one generator, seeded, gates under the note length, levels and followers set on purpose.
* Stems: no `nan`, no silent bars, nothing at the limiter on every note, each stem measured and named.
* Mix: a new version, reasonable loudness range, per-stem balance printed, FLAC kept, README written.
* Reaper: project regenerated after any change, ports checked, and the user told it is measured and not heard.
