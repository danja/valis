# Mistakes log

Short entries: what happened, why, and what prevents it. Rules that came out of
an entry live in `CLAUDE.md`, which is where they get read.

## A follower set to a target level

**What happened.** In "All Fall Down" the snare and hat were set to `val:follows :kick` with levels of -19 and -27, read as
target levels (or as the difference between two levels, which is what `docs/songs.md` said). The first mix had them 25 and
45 dB under the kick and nearly inaudible. The numbers were in the mix report; the balance was only checked after a
per-stem measurement of each part's level in the drop.

**Root cause.** `songmix` sets a follower's gain to its leader's gain plus the follower's own `val:level`, so the level is
an offset in dB and cannot be above 0. The documentation described something else.

**Prevention.** The sentence in `docs/songs.md` now says what the code does. After a mix, print each stem's rms in a drop
with its gain applied, as was done here, before trusting the balance.

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


## A note on a bar line counted in the bar before it

**What happened.** `valis-song stems` reported "silent in bars that have notes" for a new track at 170 bpm: bars 4 to 8 of the kit, and
the same shape elsewhere. The MIDI was right and the kit really was silent there, but the report named the wrong bars, and a bar that
had no kick showed one note in it. Every earlier song had humanised timing or a tempo whose bar is a whole number of samples (85 and 120 bpm), so the check had never seen a note exactly on a bar line.

**Root cause.** The check placed a note in `floor(sample / barSamples)`. At 170 bpm a bar is 67764.7 samples, and a note on the line is rounded
to a whole sample, which can fall just before the line, so it was counted in the bar before. This is the stream-position rule in
`CLAUDE.md` again, from the other side: a position compared against a boundary without allowing for the rounding of the position.

**Prevention.** `SongCheckTest` now has a 24-bar song at 170 bpm with a note on every bar line and asserts one note in each bar. The
check adds two samples before dividing. A test of anything placed by bar uses a tempo whose bar is not a whole number of samples.

## A drum that fires on velocity, and is silent below half

**What happened.** The first render of a new track had no closed hi-hats: `circuits/909.ttl` closed hat, and the rest of the kit, are
gated by the note's `velocity` output, and an envelope fires only when its gate passes 0.5, so any note under velocity 64 makes no sound.
Hats written at velocity 52 and 60 were silent, with no message.

**Root cause.** Not looked at beyond the wiring: `NoteGate.velocity` feeds each `val:Envelope`'s `gate`, which was a way to get a level
and a gate from one signal. It was never tried with a soft hit.

**Prevention.** Fixed 1 November 2026: every drum gates from `gate`, the toms take velocity through the resonator's `velocity` port, and
`tests/engine/Tr909VelocityTest.cpp` plays each drum from velocity 0.05 to 1. `NoteGate.velocity` is a level that is 0 after the note-off,
never a gate. All other examples were searched for a velocity arc into a gate, trigger or choke port; none had one.

## val:HardClip's threshold was a gain

**What happened.** Building `circuits/distortion-bench.ttl` (3 November 2026), a hard-clipped sine had rounded, sloping edges and
peaked at about 1.2 times what the threshold said. Setting `threshold` to 0.05 made the output louder, not clipped lower. The ontology
says the element "cuts the signal flat at plus and minus `val:threshold`".

**Root cause.** `ShapedTransfer` takes its one control from `gain`, or from `threshold` if there is no `gain`, and always multiplies the
input by it before a shape that clips at 1. For `val:HardClip` that made the threshold a drive: smaller meant more gain, and the top was
always at 1. No circuit or test used the element, so nothing noticed.

**Prevention.** Fixed: a shape that declares `kControlIsThreshold` divides the input by the control and multiplies the output by it.
`tests/dsp/HardClipTest.cpp` checks the flat top and the unit level below it at four thresholds. An element no circuit uses has never
been heard, which is why the element catalogue lists them and `TODO.md` asks for a circuit for each; measure the first circuit's
waveform, not only that it makes sound.

## "Sits at its limiter" written from a peak value

**What happened.** The All Fall Down README and TODO.md said the lead stem "sits at its limiter on the loudest notes", because its peak
was -1.0 dBFS, the limiter's ceiling. On 2 November 2026 the stem was counted: 3 samples in 10 million are above 0.85, and 0.5% of the
sounding samples are above 0.5. The lead is not leaning on the limiter; the ceiling is only where the loudest transient lands.

**Root cause.** A peak equal to a ceiling was read as a limiter at work, without looking at how many samples were near it.

**Prevention.** Say a stem is limited only after counting the samples above, say, 0.85 of full scale. Both notes were corrected.

## A pattern declared longer than its notes

**What happened.** The lead of "Big Pickle" (4 November 2026) was written as an eight-bar hook, but the notes only filled four bars.
The pattern declared `val:bars 8`, so it restarted every eight bars and the last four bars of every phrase were rests: the lead played
a four-bar tune, stopped for four bars, and played it again a fifth higher each time the progression turned. It was not silent, so
nothing in the render failed.

**Root cause.** The melodic table was written for a four-bar phrase and the pattern was still declared as eight. `valis-song stems`
printed the per-bar rms, which showed bars 53 to 56 silent while the pattern was playing, and the MIDI ticks confirmed the gaps.

**Prevention.** A pattern's `val:bars` must be the length of the material it holds, not the length of the section phrase it is played
in; a long phrase is made by a longer table, or by `val:every` with a shorter pattern. After scoring, read the per-bar rms of every
part across one cycle of its longest pattern: a bar with notes and no sound is a fault (`stems` reports it, and prints the bars).
