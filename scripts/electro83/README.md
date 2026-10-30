# scripts/electro83

The Python behind "Tenement Lights" and the circuits it uses. Nothing here is needed to build or test
Valis; the circuits in `examples/` are the product and can be edited directly. The reasoning is in
`docs/new-order-track.md`.

- `moog-source.py`, `six-string-bass.py`, `poly-synths.py` (Prophet-5, Emulator choir, Quadra strings) and
  `speaker-room.py` each write their `.ttl` into `examples/`, using `scripts/halloween/circuits/gen.py`.
  They write over the shipped files, so edit a script or its file, not both.
- `track.py` writes the score as one MIDI file per part, renders each with `valis-render` and mixes the
  stems. Run `python3 track.py`, or one step at a time: `score`, `render`, `mix`. It needs numpy and scipy,
  a Debug build of `valis_render` and a few minutes. Output goes to `~/Music/tenement-lights/`.
- `reverb-return.py` writes the wet-only reverb used on the high percussion.
- `python3 track.py --short` makes the three-minute arrangement with a vocal part, in
  `~/Music/tenement-lights/short-circuit/`. `vocal-guide.py` writes a guide melody for its lyrics
  (`docs/short-circuit-lyrics.md`) as `midi/vocal.mid`; render it with `examples/emulator-choir.ttl` if a
  recording is wanted. The sleeve is described in `docs/short-circuit-sleeve.md`.
