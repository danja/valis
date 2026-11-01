---
name: write-a-song
description: Write, render and mix a song from Valis circuits. Use when asked to make a track, an arrangement or an alternate edit, or to change a part of one that exists. Covers the score script, one MIDI file per part, offline stems with valis-song (or valis-render), and the reference mix.
---

# Write a song from Valis circuits

A song is a `song.ttl` (see `docs/songs.md`) plus a score script that writes the MIDI. Copy
`scripts/dub-metal/track.py` and its `song.ttl` in the artifacts repo. The first two songs, `scripts/archive/halloween/`
("Dead at Midnight", `docs/halloween-process.md`) and `scripts/archive/electro83/` ("Tenement Lights",
`docs/new-order-track.md`), mixed in Python and are kept only as a record.

## Method

The tempo, sections, parts, returns and mix table can be written once as a `val:Song` file (`docs/songs.md`) and
checked with the `read_song` tool; `valis-song render` and `valis-song mix` (or the MCP tools `render_song` and `mix_song`) render and mix from it, and
`valis-artifacts/scripts/interstate-rain/track.py` keeps only the score. Prefer that to a new table in each script. A mix is
written to a new version each time and never replaces one.

1. **Fix the plan in numbers first.** Tempo, key, bars per section, the eight-bar harmony, and which part plays in
   which section. A section table in the script (`SECTIONS`) and one function that answers "what plays in this
   bar" keep an alternate arrangement a change of table, not a rewrite. `track.py --short` is the example.
2. **One part, one circuit, one MIDI file.** The score script writes `midi/<part>.mid` for each part with
   `scripts/song/midiw.py`. Put sparse or very different sounds in their own part (the wood block and the
   tambourine are not in the kit's file) so each can be levelled and sent to effects alone.
3. **Seed every random choice** (`random.Random(seed)`), so a run gives the same MIDI.
4. **Gates.** A note shorter than its slot, by about a fifth, so the gate drops before the next note. Notes that
   touch play legato, and a plucked string or any envelope with sustain 0 will not start again. See MISTAKES.md,
   "A part that played once".
5. **Shuffle** is applied in the score: the second sixteenth of each pair moves to 54% of the pair, for drum parts
   only. Triplets are written in beats and are not shifted.
6. **Build everything, then render.** `cmake --build build` with no target (CLAUDE.md, step 6), then per part:
   `valis_render <circuit> --midi midi/<part>.mid --tempo <bpm> --rolling --seconds <song + 8> -o <part>.wav`.
   Run from the repository root so sample paths resolve. Four renders in parallel is fine. A Debug build takes
   about a quarter of the song's length per part for a polyphonic circuit; the Release build is about five times
   faster and renders the same samples (checked with `cmp` on a violins note), so the song scripts prefer
   `build-release/valis_render_artefacts/Release/valis_render`. Rebuild it with
   `cmake --build build-release --target valis_render valis_shot` after any change to the code, or it is stale.
7. **Effects on a stem** are circuits fed with `-i`: `speaker-room.ttl`, `reverb-return.ttl`. Mix the send at
   half level before writing the file, so the sum cannot clip.
8. **Check every stem before mixing.** Run `check_stems` (`valis-song stems`), which reports silence where the score has notes, clipping and NaN, bar by bar.
   Then ask `valis_shot is stem.wav "a harp"` (the `hear-a-sound` skill) for each pitched or struck part: a stem that comes
   out as a synthesizer preset, or as the wrong instrument, is worth fixing before the mix.
   Check tuning of any pitched part (see the `test-an-instrument` skill). Check `isnan`.
9. **Mix with `valis-song mix` (or `mix_song`), not by ear.** It normalises each stem to a target rms measured over the
   stretches where it plays (windows above -60 dB), so a part that rarely plays sits as loud as one that always does,
   pans with a constant-power law, high-passes, applies the sections' levels with `--arc`, adds a fade and a room, soft-clips
   with `tanh`, sets the peak to -1 dB and aims at the song's loudness. The levels, pans, high-passes, fade, room and drive
   are in `song.ttl`. Sparse effect stems (percussion, reverb returns) take an offset from the kit's level, to keep the balance
   they were played at. Each mix is a new version; one is never replaced.
10. **Trim and fade** a short edit to its exact length, with the last seconds faded.

## Notes

- Do not stop a render with `pkill -f <script name>`: it matches the shell that runs the command. Use
  `pkill -x valis_render` or a process id.
- Render in the background from a small shell script and poll its log, so the session is not blocked.
- Levelling every stem by its own playing rms makes every section about as loud as the others. Give the mix a gain per
  section (`scripts/dub-metal/track.py mix --arc`, `ARC_DB`) and print each section's level, so a breakdown is quieter and a
  peak louder by a number you can read.
- To aim a mix at a loudness, give the song `val:loudness` or pass `--lufs N` to `valis-song mix`; it holds the true peak at -1 dBTP.
- The mix is a reference. Vocals and mastering are done in Reaper (`reaper-from-stems`).
- Report honestly that a song has been measured and not heard.
- Save each mix under a new version number (`name-v3.flac`) and keep the previous one, so the listener can compare.
  Do not overwrite.
- Add small random timing and velocity variations per part (a fixed seed), clamping every start at 0, and put a
  synthetic room on the mix. Physically modelled instruments (`val:Brass`, `val:Bow`, `val:Pluck` with
  `val:StiffString`, `val:ModalBank`, `val:Shaker`) sound far less like a preset than filtered oscillators.
- Keep a guide voice out of the listening mix: it is deliberately synthetic and reads as a cheap keyboard.
- The stems are large. Gitignore the WAVs and keep a FLAC of the mix (GitHub refuses files over 100 MB).
