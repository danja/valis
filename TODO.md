## Pending

### Case study: Mutable Instruments Rings

Rings is a physical modelling resonator: modal bank + string (Karplus-Strong style).
High complexity — requires new elements:

* `val:CombFilter` — feedback comb filter (Karplus-Strong basis): delay line read at a
  tuned period, with a one-pole lowpass in the feedback loop. The preallocated `val:Delay`
  could in principle be used but Karplus-Strong needs its delay length tuned to pitch
  (sample-accurate) and the feedback filter baked in, so a dedicated element is cleaner.
* Optionally `val:ModalBank` — parallel resonators each as a 2-pole bandpass; Rings uses
  up to 6. Could be a single multi-port element or six `val:StateVariable` in parallel
  mixed through a `val:Mixer`.

Use `/new-element` to scaffold each one.

## Done

* `/new-element` skill created at `.claude/commands/new-element.md`
* Delay line (`val:Delay`) implemented and in ontology
* `val:Scale`, `val:VCA`, `val:MidiPitch`, `val:MidiVelocity` implemented and in ontology
* `val:AsymClip` — asymmetric soft clipper for the Klon Centaur clipping stage
* SH-101 subtractive synth: `examples/sh101.ttl` + `docs/manual/sh101.md`
* Klon Centaur guitar overdrive: `examples/klon.ttl` + `docs/manual/klon.md`
* Release workflow on tag push: `.github/workflows/release.yml`
