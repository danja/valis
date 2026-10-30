---
name: generate-circuit
description: Write an instrument or effect circuit (a Turtle file in examples/) from a short Python description, the way the Halloween and 1983-style sets were made. Use when adding an example circuit, a polyphonic synth, or a send effect.
---

# Generate a circuit

Hand-written Turtle with one `val:Arc` block per connection runs to hundreds of lines. The circuits in
`examples/halloween-*.ttl` and `examples/moog-source.ttl` and its neighbours are written by short Python
scripts using `scripts/halloween/circuits/gen.py`:

- `build(name, label, header, genre, role, accepts, nodes, arcs, params, path)` for a monophonic circuit or an effect.
  `nodes` are `(id, class, {property: value})`, `arcs` are `(from, port, to, port)`, `params` are
  `(slot, id, property, name, symbol, min, max, section, unit)`.
- `build_poly(...)` for a polyphonic one: a `Voice` subcircuit stamped out `val:voices` times, then shared nodes.
  Its params are by the voice's port symbol, so a control on a shared stage cannot be a parameter.

Examples to copy: `scripts/electro83/moog-source.py` (mono voice), `poly-synths.py` (polyphonic),
`speaker-room.py` and `reverb-return.py` (effects fed with `-i`), `six-string-bass.py` (physical model).

## Rules that have caught real mistakes

- **A control arc replaces the value it drives.** Put the resting value in the control path (`val:Scale` min), not
  on the element. If a note's cutoff is an envelope sweep plus keyboard tracking, join them with `val:ControlAdd`.
- **Units.** Check each port's unit in `vocabs/valis.ttl` before setting it. `val:Tape` `hiss` is in decibels, and
  0 is full-scale noise. Envelope times are milliseconds.
- **DC offset.** A `val:OnePole` high-pass at 22 to 45 Hz after a nonlinear stage or a string.
- **Level.** Finish with a `val:Gain` and a `val:Limiter`, then render and read the peak and rms. Do not leave a
  circuit that sits at the limiter on every note.
- **Silence.** An instrument must be silent until played. Render with no notes, or run the engine test.
- **Header comment.** Say what it models, how to play it, the range, and that it is monophonic if it is.
- Scripts write over `examples/`. Edit the script or the file, not both. Keep scripts in `scripts/<set>/` with a
  README.

## After writing

`python3 <script>.py`, `cmake --build build`, render one note with `valis_render --note`, run `tests/engine/`
tests (`test-an-instrument`), add the file to `docs/examples.md`, and run `ctest`. The ontology and registry must
agree, and every example is linted by the existing tests.
