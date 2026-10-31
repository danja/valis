---
name: test-an-instrument
description: Check a pitched instrument circuit for pitch, silence, release and re-triggering, by measurement and by an engine test. Use after adding or changing an instrument in examples/, a waveguide or string element, or any circuit played from a sequence.
---

# Test an instrument

## Measure tuning

- Render one held note: `valis_render <circuit> --note <n> --seconds 3 --gate-off 2.8 -o x.wav`.
- Take the second half, apply a Hann window, FFT with a long zero-padded length (2^19), search only within
  0.7 to 1.4 times the expected frequency, and interpolate the peak (parabola on the log magnitude). The strongest
  peak over the whole spectrum is often a harmonic and reports a false error of hundreds of cents.
- Do it across the range the part is played in, and report cents. A bin is 23 cents wide at 220 Hz for 2^14
  samples at 48 kHz, so a short FFT hides a real error.
- A waveguide or comb loop is tuned by its total delay: subtract the phase delay of every filter in it
  (`waveguide::Loss::phaseDelay`) and read the line at a fractional position. `tests/dsp/StiffStringTest.cpp` is
  the pattern: eight pitches across the range, then damping and dispersion varied.

## Engine test

`tests/engine/ElectroInstrumentsTest.cpp` loads each circuit as the plug-in would and asserts:

1. silent before a note (below -60 dB; tape hiss is allowed),
2. peak between 0.02 and 1.0 on a held note, and the note's frequency stands out from one 3.5% above it,
3. silent after release (render 14 s, last 100 ms below 0.001),
4. a second note after a short gap speaks again. This is the legato check and it caught a bass that played once.

Add the file name to `kInstruments`. Effects are tested with a burst in and silence in.

## Check a short note

Play a note much shorter than the instrument rings (an eighth note for a guitar) and measure the level a second
after note-off. A plucked or struck instrument should still be ringing, with the high harmonics already gone. A
pluck that stops at note-off is a release shorter than the ring: see MISTAKES.md.

## Check a rendered stem

Print the rms of each bar of the stem and compare it with the score. A part that is silent where it has notes is a
gate or envelope problem, and a stem that is the same loudness everywhere is a limiter problem. See MISTAKES.md.

## Check it sounds like the instrument

After tuning and release are right, ask the `hear-a-sound` skill's question: `valis_shot is note.wav "a harp"` says whether a neural
model hears the instrument or something else (a piano, a synthesizer preset). Measured, not heard.
