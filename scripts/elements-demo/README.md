# Demonstrations of one element

Each script writes a circuit into `circuits/` that shows what one element (or a few that belong together) does, with the
element's controls on the panel. They are the answer to the element catalogue listing an element that no circuit uses.
Run a script with `python3 scripts/elements-demo/<name>.py`; it needs only Python 3. The circuit, not the script, is what ships.
Edit the script and regenerate, not both.

| Script | Circuit | Elements shown |
|---|---|---|
| `distortion-bench.py` | `circuits/distortion-bench.ttl` | HardClip, SoftSine, SinArcTan, Diode, Triode |
| `bassman-amp.py` | `circuits/bassman-amp.ttl` | ToneStack (with Triode, SoftSine) |
| `lofi-codec.py` | `circuits/lofi-codec.ttl` | Codec, SpectralGate |
| `arp-quantiser.py` | `circuits/arp-quantiser.ttl` | Quantise, NoteOut, StepSequencer |
| `vocoder-speak.py` | `circuits/vocoder-speak.ttl` | LPC, Vocoder |

The method, from `distortion-bench`: put the source through the elements side by side, choose between them with one selector
(`val:AudioSelect` has three inputs, so two are chained), give each its own controls and a level trim, and measure the trims by
rendering one note per setting. Test the sound by what it must contain: a sine through a symmetric curve has no even harmonics.
The Param for a selector needs `lv2:enumeration` and a scale point per position, which `gen.py` does not write, so the script adds
them to the generated file.
