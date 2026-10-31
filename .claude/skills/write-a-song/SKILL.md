---
name: write-a-song
description: Write, render and mix a song from Valis circuits. Use when asked to make a track, an arrangement or an alternate edit, or to change a part of one that exists. Covers the score script, one MIDI file per part, offline stems with valis-render, and a Python mix.
---

# Write a song from Valis circuits

Two songs were made this way: `scripts/halloween/` ("Dead at Midnight", see `docs/halloween-process.md`) and
`scripts/electro83/` ("Tenement Lights", see `docs/new-order-track.md`). `scripts/electro83/track.py` is the
newer and tidier pattern; copy it.

## Method

1. **Fix the plan in numbers first.** Tempo, key, bars per section, the eight-bar harmony, and which part plays in
   which section. A section table in the script (`SECTIONS`) and one function that answers "what plays in this
   bar" keep an alternate arrangement a change of table, not a rewrite. `track.py --short` is the example.
2. **One part, one circuit, one MIDI file.** The score script writes `midi/<part>.mid` for each part with
   `scripts/halloween/midiw.py`. Put sparse or very different sounds in their own part (the wood block and the
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
8. **Check every stem before mixing.** Print the rms of each bar and look for silence where the score has notes.
   Then ask `valis_shot is stem.wav "a harp"` (the `hear-a-sound` skill) for each pitched or struck part: a stem that comes
   out as a synthesizer preset, or as the wrong instrument, is worth fixing before the mix.
   Check tuning of any pitched part (see the `test-an-instrument` skill). Check `isnan`.
9. **Mix in Python (numpy, scipy), not by ear.** Normalise each stem to a target rms measured over the stretches
   where it plays (windows above -60 dB), so a part that rarely plays sits as loud as one that always does.
   Sparse effect stems (percussion, reverb returns) take the kit's gain plus an offset, to keep the balance they
   were played at. Pan with a constant-power law, high-pass pads, soft-clip the sum with `tanh`, normalise the peak
   to -1 dB.
10. **Trim and fade** a short edit to its exact length, with the last seconds faded.

## Notes

- Do not stop a render with `pkill -f <script name>`: it matches the shell that runs the command. Use
  `pkill -x valis_render` or a process id.
- Render in the background from a small shell script and poll its log, so the session is not blocked.
- To aim a mix at a loudness, use `scripts/shot/lufs.py` (see `docs/shot.md`); it holds the true peak at -1 dBTP.
- The mix is a reference. Vocals and mastering are done in Reaper (`reaper-from-stems`).
- Report honestly that a song has been measured and not heard.
- Save each mix under a new version number (`name-v3.flac`) and keep the previous one, so the listener can compare.
  Do not overwrite.
- Add small random timing and velocity variations per part (a fixed seed), clamping every start at 0, and put a
  synthetic room on the mix. Physically modelled instruments (`val:Brass`, `val:Bow`, `val:Pluck` with
  `val:StiffString`, `val:ModalBank`, `val:Shaker`) sound far less like a preset than filtered oscillators.
- Keep a guide voice out of the listening mix: it is deliberately synthetic and reads as a cheap keyboard.
- The stems are large. Gitignore the WAVs and keep a FLAC of the mix (GitHub refuses files over 100 MB).
