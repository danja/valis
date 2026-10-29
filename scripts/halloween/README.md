# scripts/halloween

The Python that made "Dead at Midnight". Kept for reference. Nothing here is needed to build or
test Valis; the circuits in `examples/halloween-*.ttl` are the product and can be edited directly.
The process is described in `docs/halloween-process.md`.

- `song.py` writes the song's notes as one MIDI file per instrument, using `midiw.py`. It writes to
  `~/Music/dead-at-midnight/midi/` (the `OUT` path at the top). Run `python3 song.py`.
- `circuits/gen.py` expands a short description of nodes, arcs and parameters into the long Turtle
  form the examples use, for a monophonic circuit (`build`) or a polyphonic one (`build_poly`).
- `circuits/dl.py`, `dh.py`, `bass.py`, `organ.py`, `guitar.py`, `theremin.py`, `orn1.py` and `orn2.py`
  each describe one instrument and write its `.ttl` into `examples/`. Run one from its own
  directory, for example `cd circuits && python3 bass.py`.

The circuit scripts reproduce the shipped files byte for byte (checked when they were copied here).
They write over the files in `examples/`, so any later hand edit to a circuit is lost if its script
is run again; edit one or the other, not both.
