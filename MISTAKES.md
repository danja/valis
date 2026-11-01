# Mistakes log

Short entries: what happened, why, and what prevents it. Rules that came out of
an entry live in `CLAUDE.md`, which is where they get read.

## A part that played once

**What happened.** The bass-guitar stem of "Tenement Lights" rendered with a normal peak and an rms that looked
low but plausible. Only the first note of each section sounded; the rest of the part was exact digital silence.
The mix went ahead, and the fault showed only when the stem's rms was printed for each bar.

**Root cause.** The score wrote each note to end at the instant the next began. The engine's gate stays high
across touching notes, so an envelope with sustain 0 (a pluck) was never started again. The monophonic Source
bass hid the same fault because its envelope sustains.

**Prevention.** The score shortens every note in a sequenced part by a fifth of a step so the gate drops, and
`tests/engine/ElectroInstrumentsTest.cpp` plays a second note after a short gap on every instrument. Before mixing,
print the rms of each bar of each stem and compare it with the score (`.claude/skills/test-an-instrument`).

## Plucked notes cut off at note-off

**What happened.** The first acoustic guitar, harp and guitarron for "Lights On, Nobody Home" measured in tune and
in range, but sounded like chiptunes. Every pluck stopped about 0.3 s after its note ended, and every harmonic faded
together.

**Root cause.** The amplitude envelope had a release of 90 to 140 ms and the string's own loop was left almost
undamped, so the envelope, not the string, set the decay. Tests played one long note, so the gate stayed on for
longer than the ring and the truncation never showed. The song's notes are short.

**Prevention.** Give a plucked or struck instrument a release at least as long as its ring time, and let the string's
damping, not the envelope, shape the decay (high harmonics should fade first). Test it with a note much shorter than
the ring and check the level one second after note-off (`test-an-instrument` skill).

## A test that returned before testing anything

**What happened.** `tests/dsp/FluteTest.cpp` began its `main` with `probeReed(); return 0;`, a debugging probe for the clarinet that
printed some figures to stderr and exited with success. It was committed in `db43ed6` and stayed for many commits: the flute's tests
(pitch, harmonics, tuning across the controls, damping, breath noise) ran in none of them, yet CTest reported the test as passed.
It was found only by reading the file while adding a sample-rate test to it. With the probe removed all seven tests pass, so the
flute was sound; the test had simply stopped saying so.

**Root cause.** A probe was added to `main` for a quick look and the line that ran it was not taken out. Nothing checks that a test
runs what it names: the harness only sees an exit code, and `return 0` early is a pass.

**Prevention.** A probe goes in a scratch program, not in a test's `main`. When adding to a test file, read its `main` first. The
recurring checks in `TODO.md` now include looking for an early `return 0;` or a probe called first. A stronger check would have each
test print its name at the end and CTest require it with `PASS_REGULAR_EXPRESSION`; not done.

## Instruments tuned at 48 kHz and played at other rates

**What happened.** The multi-rate conformance report (`ElementConformanceTest`) showed that most waveguide and resonator elements
sounded different at 44.1, 88.2, 96 and 192 kHz: `val:Brass` fell silent at 88.2 kHz, `val:Bow` was twice as bright, `val:Reed` was
55% brighter and 14 cents flat, `val:StiffString`'s partials were 18% off, `val:Shaker` and `val:ModalBank` were 5 dB louder, the
reverb's highs lasted 32 dB longer, and the flute was 44 cents sharp at 110 Hz. The report had existed for some time and was read as
noise; two of its alarms were its own artefacts (a window of fixed samples, not seconds).

**Root cause.** Filter poles, DC-blocker coefficients, allpass coefficients, a fixed "one sample" of loop delay and the level of
per-sample noise were all written as numbers for 48 kHz. Each is a time or a frequency that a number per sample does not hold across
rates. The worst case hid behind `Loss::setPole`, which clamped a pole to 0.98: a pole carried to a higher rate, or a corner set low
against the rate, is near one, and the clamp quietly moved the filter's corner several times higher. Nothing reported the clamp.

**Prevention.** A design value is a design value for `waveguide::kDesignRate`; carry it with `poleAtRate`, `poleKeepingLowLoss`,
`DcBlocker::prepare(rate)` or an allpass scaled by the rate ratio. A fixed loop delay is `rate / kDesignRate` samples. Noise into a
resonator needs its level scaled for the rate. Give each element a test that plays it at 44.1, 48, 96 and 192 kHz and compares pitch
and level, and check that it fails on the old code. Before trusting a report, check that its own window measures the same seconds at
every rate. A clamp on a parameter that a caller can legitimately push past it needs a note saying who reaches it.

## MP3 listed as supported and never tried

**What happened.** `CLAUDE.md` and `audio/README.md` said sound files load in WAV, AIFF, FLAC, Ogg and MP3, and a TODO item said the MP3
reading in the app was "not tried". The first circuit to name an MP3 (`val:Sampler`'s example) failed to load with "unreadable audio
file". JUCE builds its MP3 decoder only with `JUCE_USE_MP3AUDIOFORMAT`, which is off by default, and Valis never sets it.

**Root cause.** A capability was written down from the name of the library's module, not from loading a file. Five MP3s were added to
`audio/` on that basis.

**Prevention.** A format a document promises has a test that loads one file of it. The sampler's tests write WAV files; `audio/` now
carries FLAC copies and the docs say MP3 does not load. Turning the decoder on is a decision for the owner, because JUCE ships it
with a warning about third-party patents and code.

