---
name: hear-a-sound
description: Check a rendered sound without listening, by measurement and by neural models. Use after rendering a circuit or a stem to see if it is silent, clipped, too loud, changed, or does not sound like the instrument it should. Covers valis-shot (measure, compare, is, listen) and the matching MCP tools.
---

# Hear a sound

Say in the answer that a result is **measured, not heard**. A model is a rough ear: if it disagrees with the user's ears, the user is right.

## 1. Render, then measure

- Build everything first (`cmake --build build`, no target), or you measure old code. Render with the Release build when it exists
  (`build-release/valis_render_artefacts/Release/valis_render`).
- `valis_shot measure x.wav` (MCP: `measure`). Read `signal.notes` before the numbers: silence, clipping, DC offset, NaN, and "Dark" (under 3% of the power above 2 kHz, so a hat, lead or metal layer will be buried; `signal.highShare` has the figure).
  Then `signal.pitch` against the note you asked for, `loudness.integratedLufs` (very loud above -9), `loudness.truePeakDbtp`
  (above -1 may distort after MP3), `envelope.attackMs` and `decayMs` (a pluck is fast to start, a note that dies early is cut off).
- Fix anything in `notes` before going further.

## 2. After a change, compare

- `valis_shot compare before.wav after.wav` (MCP: `compare`). 0 dB is the same sound, a few dB a small change, tens a new
  sound. If a change you meant to hear reads near 0, it did nothing (check for a control arc overwriting the value).
- Render both with the same note, length and sample rate.

## 3. Does it sound like the instrument?

- `valis_shot is x.wav "a plucked nylon string guitar"` (MCP: `sounds_like`). Answer: yes, maybe or no, a share, and `closest`: what
  it was mistaken for.
- Read `closest` rather than the verdict alone. `a synthesizer keyboard preset` at the top means it sounds synthetic: change the
  excitation or the physical model, not the level. An instrument that comes out as a different real one (a harp as a piano) has
  the right character and the wrong detail.
- Use `valis_shot listen x.wav "a harp" "a piano"` (MCP: `listen`) to choose the competing sentences yourself. Read the share.
- Name the instrument the way a person would ("a plucked nylon string guitar", not "guitar stem v3").

## 4. If the neural questions fail

They need a build with `-DVALIS_WITH_ONNX=ON` and the models (about 1.1 GB) from `scripts/shot/fetch-models.sh`. The tool says which is
missing. Never run the fetch script without the user's say-so. `measure` and `compare` need neither.

## Rules of thumb

- Judge a stem on its own, not in the mix: other parts hide it.
- A sentence scored once is weak evidence. Try the right sentence and one wrong one, or use `is`.
- Keep the earlier result when you change something, so the next `compare` has something to compare with.
- Details, limits and the song results are in `docs/shot.md`.
