# Mistakes log

Short entries: what happened, why, and what prevents it. Rules that came out of
an entry live in `CLAUDE.md`, which is where they get read.

## Measuring with a stale binary

**What happened:** a rewritten `val:Reed` measured perfectly in its unit test and
wrongly through `valis-render`. Later a model-layer feature passed its tests and
`valis-render` played silence. Both times rounds went into comparing the two paths
before anyone suspected the binaries.

**Root cause:** `cmake --build build --target <test>` builds that test and the
library, not `valis_render`, so the tool was linked against the previous
`libvalis_core.a`. The giveaway the first time was a peak that never moved
whatever the inputs (`0.2500`, the old element's output scale).

**Prevention:** build everything (`cmake --build build`, no target) before
measuring through any tool. A number that does not move when the inputs move is
evidence about which code is running. When a test and a tool disagree about a
circuit whose compiled form prints identically in both, suspect the binaries
first. In `CLAUDE.md` under the change workflow.

## Events located by block, not by stream position

Happened twice, from opposite directions. Both are the same bug.

- **`val:SignalGenerator` impulse:** it fired only when a period wrapped on
  sample 0 of a block (`i == 0 && phase < inc`), so most periods produced
  nothing and the output changed with the host's block size. The phase
  accumulator already tracked wraps exactly; gating on the block index threw
  that away.
- **`valis-render` notes:** it compared the note time to the start of each block
  for equality, so `--gate-on 0.02` (sample 960, not a multiple of 512) never
  sent a note-on and the example rendered silent. Only `--gate-on 0.0` worked,
  which is why it survived.

**Prevention:** a per-sample condition depends on the stream position (phase,
counters held in members), never on the index within the block. A stream-position
event fires in the block that contains it, `at >= blockStart && at < blockStart + n`,
never by equality with a boundary. Tests render the impulse train and assert its
exact count. Rule in `CLAUDE.md` under real-time rules.

## A flute that was structurally a stopped pipe

**What happened:** the first `val:Flute` was in tune across three octaves and
stable, and sounded nothing like a flute. Its spectrum had strong odd harmonics
and almost no even ones, which is a stopped pipe or a clarinet. Many parameters
were tried; none moved the balance.

**Root cause:** the excitation, not the resonator. The jet was a cubic with a
constant bias driven hard enough to clip, and a symmetrically clipped signal is a
square wave with no even harmonics. The jet delay was also half the air column's,
so its phase reinforced odd harmonics and cancelled even ones.

**Prevention:** model the excitation, not a curve that resembles it. The flow is
`Uj * tanh((eta - y0) / b)`; `y0`, the jet's offset, breaks the symmetry and is now
`val:offset`. A lowpass on the jet's response, tracking pitch, stops it
reinforcing the third and fifth. `tests/dsp/FluteTest.cpp` asserts the harmonic
ordering. A model can be stable and in tune and still be the wrong instrument, and
only a spectrum measurement says so. In `CLAUDE.md` under physical models.

## Granulator Freeze: two positions that could not differ

**What happened:** the Freeze control "didn't appear to make any difference", and
the Mix knob below the fold of the Controls panel could not be reached.

**Root cause:** `freeze` was a three-way choice (Auto, Record, Freeze), and Auto
meant "keep material loaded by `val:file`". The example loads material through a
`val:SampleLoad`, so Auto and Record were the same thing by construction. Separately,
`juce::Slider` consumes the scroll wheel, so scrolling the panel nudged the dial
under the pointer instead.

**Prevention:** every position of a control must be able to differ in the circuit
that exposes it. `freeze` is now a plain `lv2:toggled` switch, and an unconnected
input is detected from `ProcessArgs::silence`. `PanelSlider` scrolls by default and
adjusts only with Ctrl or Command held. Both rules in `CLAUDE.md` under the
Controls view.

## Oxide survey elements: four slips caught by the first test run

All four were found by the new tests, or by the compiler, before anything else used the elements.

- **Redux hold length:** the counter was decremented before it was tested, so a
  hold of 4 lasted 3 samples. Test the counter first, then count down, and assert the
  hold length against a strictly rising input (`tests/dsp/ReduxTest.cpp`).
- **Reverb decay 3 to 4 dB short:** line lengths were fractional at size 1, and linear
  interpolation of a fractional delay is a lowpass, so high-frequency modes decayed
  faster than the time asked for. Base lengths are whole samples now. A delay that is
  read between samples in a feedback loop is also a filter; measure the decay with the
  damping off before blaming anything else.
- **Wavetable loudness:** each frame was scaled once, to peak at 1 at full bandwidth.
  A pulse-like frame then peaked at 0.12 at an ordinary pitch, where most harmonics are
  dropped. Every table is scaled on its own now. When a normalisation depends on a
  property of the data that the playback path changes, normalise after the change.
- **A summed input needs a `val:Mixer`:** the split-reverb example had the same arc
  written twice and the multiband example fed two arcs into one compressor. The
  compiler caught both with located errors; nothing to prevent beyond rendering each
  new example with `valis-render` before writing a test around it.

## Running only the nearest tests after changing a shipped example

**What happened:** adding a choke to each DMX voice card changed the number of
elements the circuit compiles to. The new tests and the DMX engine test passed;
`CircuitCompilerTest`, which counts the DMX nodes (`8 * 9 + 4`), was not run until
the full suite at the end, where it failed.

**Root cause:** the tests chosen were the ones for the code being written, and
a shipped example is read by tests that have nothing to do with the element that
changed. A test that pins a count of a shipped file's contents is exactly the kind
that no targeted run will include.

**Prevention:** after editing anything under `examples/`, run the whole suite, or
at least `grep -rl <example name> tests/` and run those. A count asserted on an
example is a reason to update the count when the example grows, not a sign the
example is wrong.

## Adding a mode port to an element whose test says it has none

**What happened:** to give the Grist feedback filter a lowpass, bandpass and highpass
choice, `val:StateVariable` got a `mode` port and a selected output. `OntologyTest`
asserts the element has no `mode` port: the Scream circuit taps `lp` and `hp` at the
same time, so the design is named outputs, not a switch. The full suite caught it.

**Root cause:** the change was made to the nearest element that had the three signals,
without reading the test that pins how that element is meant to be used.

**Prevention:** before adding a port to an existing element, grep `tests/` for the
element's name and read any assertion about its ports. A test that says why a port is
absent is a design decision, so put the feature in a new element
(`val:AudioSelect`) rather than reversing it.

## Modulation effects that moved once per block

**What happened:** `val:Chorus`, `val:Flanger` and `val:Phaser` read their LFO once per block and held
the delay, or the allpass pole, for the whole block. The delay stepped at every block boundary, which
a fast rate or a long block made audible, and the sound depended on how the host cut the stream. None
of the three put its LFO back to its starting phase on `reset()`, and `val:Noise` did not restart its
sequence. A conformance suite written over every element found all four in one run.

**Root cause:** the elements were written when the engine's block was the unit of time, and nothing
checked that a different cut of the same stream gave the same sound. It is the "events located by
block" mistake above in a different place: a continuous quantity sampled at the block rather than at the
sample.

**Prevention:** `tests/dsp/ElementConformanceTest.cpp` now holds every element to determinism, reset,
block-size independence, boundedness under random controls, no allocation in `process()` and decay.
A new element is checked without a line of test code, and an exception needs a reason written next to
it. A modulation source that runs inside an element advances per sample (or per fixed count of
samples carried across blocks), never per block.

## A context built twice, and only one of them complete

**What happened:** the `render` tool refused to run inside the plugin ("no ontology or registry") from the
day it was written. `ValisProcessor::ops()` built the context the ops run in and never set `ctx.registry`.
Every test passed, because every test builds its own context by hand and sets everything.

**Root cause:** the context is assembled in two places that share no code, and the tests' copy was the
complete one. A field an op needs but the plugin forgot is invisible to any test that does not use the
plugin's copy, and the plugin cannot be constructed headless.

**Prevention:** `tests/ops/ContextWiringTest.cpp` reads `OpContext` from `Ops.h` and the body of
`ValisProcessor::ops()` as text and fails if any field is not assigned. Adding a field to the context now
fails the build until the plugin sets it. The general lesson: where a thing that tests cannot construct is
assembled by hand in one place, test the assembly against the declaration of what it must contain.

## A rule applied in the views and not in the ops

**What happened:** a `val:Param` can give its own `lv2:minimum` and `lv2:maximum`, narrower than the port's.
The Controls view and the host parameter both used it. `list_params` and `set_param` did not: they reported
the port's range, so a caller was told a knob went from -60 to 24 dB when it went from 20 to 24, and
`set_param` clamped to the wrong range. Found when the stability check drew values from `list_params`'
ranges and they were outside the knob's.

**Root cause:** the same fact ("what range does this knob have") was worked out separately in the UI, in the
processor and in the ops, and the ops copy was written first and never updated when the override was added.
The ops were meant to be the single implementation the views sit on, but the views had grown their own.

**Prevention:** `testABindingsOwnRangeIsTheRangeTheOpsReport` holds the ops to the binding's range. Where a
view and an op both need a value, the view should ask the op; the next step is to make the Controls view and
the processor read their ranges from `listParams`.

## `valis-render --midi` tripped a JUCE assertion on a relative path

**What happened.** The first build of the `--midi` option constructed `juce::File` from the path
as given. A relative path such as `chord.mid` fires `JUCE Assertion failure in juce_File.cpp:219`
and the file is looked up against nothing in particular. The render still worked, which hid it.

**Root cause.** `-o` already resolves against the working directory, but the new input option was
written without looking at how the neighbouring one handled its path.

**Prevention.** Any user-supplied path in a tool goes through
`juce::File::getCurrentWorkingDirectory().getChildFile(...)`. Run the tool once with a relative
path and watch stderr for JUCE assertions, not only for the exit code.

## Two MCP servers could bind the same port, and a failed bind ruined the server

**What happened.** A second Valis instance asked for the same MCP port as the first and was told it
had bound it. Requests then went to either server, so a client talked to the wrong circuit. The
fix, trying the next port, then failed on the first attempt: after `bind_to_port` fails, httplib
marks that `Server` object decommissioned and every later bind on it fails too. A third problem
showed up in the test: `stop()` straight after `start()` hangs, because stopping a server that has
not begun to listen does nothing and its thread then listens for ever.

**Root cause.** httplib's default socket options include `SO_REUSEPORT`, which the plug-in never
looked at. The other two come from httplib's lifetime rules, which are not stated where the API is
used.

**Prevention.** The server sets its own socket options (`SO_REUSEADDR`, or `SO_EXCLUSIVEADDRUSE` on
Windows), makes a new `httplib::Server` for each port it tries, and `start()` returns only once the
server is listening. `tests/mcp/McpServerPortTest.cpp` covers all three. Any code that wraps a library
with its own lifecycle rules gets a start, stop and restart test as well as a first-use test.

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

## Tape hiss set in the wrong unit

**What happened.** The choir circuit set `val:hiss` on `val:Tape` to 0, meaning "none". The element reads the
value in decibels, so 0 is full-scale noise, and the choir was not silent before a note.

**Root cause.** The unit was not checked against the port declaration in `vocabs/valis.ttl`; the property name
suggested an amount, not a level.

**Prevention.** Check a port's unit and range in the ontology before setting a value in a generated circuit, and
render with no note played: an instrument must be silent. The engine test asserts it (`generate-circuit` skill).

## A MIDI writer that looped forever on a negative time

**What happened.** The score for "Lights On, Nobody Home" gave the guitar a small random timing offset. On bar 0 it
made one note start at a negative time. `scripts/halloween/midiw.py` turned the negative delta into a variable-length
quantity, and its `while n:` loop never ends for a negative number, because shifting a negative integer right never
reaches 0. The process grew until the system killed it (exit 137), twice, and took the session down with it.

**Root cause.** The writer trusted its input, and the score added a jitter to a note at time 0 without clamping it.

**Prevention.** `midiw.write` now raises `ValueError` for a note that starts before 0 or has no length. A score
that adds timing jitter clamps the start at 0. Run a new score under `ulimit -v` and `timeout` the first time.

## An element added to a voice and never connected

**What happened.** The new violin voice declared a `val:MidiVelocity` that nothing read. The full test run failed
`ops_LintTest` ("has no path to the output"), although the circuit rendered and measured fine.

**Root cause.** The circuit was checked by rendering and by pitch alone. The lint test, which runs over every file in
`examples/`, was run before the circuit was rewritten and not after.

**Prevention.** After regenerating any circuit in `examples/`, run `ops_LintTest` as well as the render. It takes under
a second. Velocity should drive something (here, bow speed), not be declared and dropped.

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

## A test that measured the wrong thing, twice

**What happened.** `PluckTest` failed on the pick-position comb that was in fact correct.

**Root cause.** The test never released the gate between plucks, so only the first pluck fired, and
`ElementTestFixture::computeSpectrum` applies a window that nearly removes a burst at the start of the block.

**Prevention.** When a test of a one-shot fails, check that every trigger in the test is a fresh gate edge and that
the measurement window contains the event. Use a plain DFT for a burst at the start of a block.

## Control and audio ports joined by an arc

**What happened.** Vibrato was added to the trumpets and violins with a `val:VCA` between an LFO and a `val:Scale`.
`ops_LintTest` passed. The renderer printed "arc joins a control output to a audio input" as warnings and the
vibrato did nothing; the warnings were buried in a long run.

**Root cause.** The lint did not check that an arc joins ports of the same kind.

**Prevention.** Use `val:ControlMultiply` for control-rate products and `val:VCA` only on audio. After regenerating a
circuit, render it once and read the renderer's output for "arc joins". `lint` now reports it as `mixed-rate-arc`
(`tests/ops/LintTest.cpp`).

## A background wait that matched itself

**What happened.** Four background commands began with `until ! pgrep -f "cmake --build" ...; do sleep 10; done` to
wait for a build. None ever ran its payload. A test that had aborted during that period (`ops_LintTest`) went unexamined
for a long time, and passed when run on its own.

**Root cause.** `pgrep -f` matches full command lines, including the shell running the wait, whose own text contains
the pattern. The condition never became false.

**Prevention.** Do not wait by pattern-matching process names. Run the build in the foreground (or as the background
command itself) and chain the next step after it. A test that aborts during a concurrent build should be re-run alone
before it is called a failure or ignored.

## Reaching for Python to check a C++ front end

**What happened.** To check the CLAP and AST feature extractors, a Python reference was started with `transformers`.
Versions 5.x and 4.46.3 both demanded `torch` for the feature extractors, a multi-gigabyte install, and the user
had said to avoid Python in this path. The script, environment and data were deleted.

**Root cause.** The goal (no Python dependency) was not applied to the verification step as well as the product.

**Prevention.** Check a front end against known answers instead of a reference: `shot_NeuralEarsTest` requires AST to
name a 440 Hz sine `Sine wave` (it does, at 0.84) and hiss `White noise`, and CLAP to put an 80 Hz tone nearer "a low,
deep rumble" and a 5 kHz tone nearer "a high pitched whistle". A wrong mel scale or normalisation fails these. They do
not prove bit-exact features, so small shifts in a score are not ruled out.

## A report that was valid JSON only for tidy file names

**What happened.** `valis-shot` built its JSON with `printf` and put the file name inside quotes unescaped. A name with a
quote, or a sentence given to `listen` with one, produced output no parser accepts. Moving the code behind MCP, where the
names come from a caller, made that a real fault. Using `juce::JSON` instead printed `25.600000000000001`.

**Root cause.** The format was written for one reader (a person at a terminal) and reused for another (a program).

**Prevention.** One small builder with `quote()` and a fixed-places `num()` (`src/shot/ShotReport.cpp`), and a test that
measures a file called `a"b.wav` (`tests/shot/ShotReportTest.cpp`).

## A hand-written MIDI file with the wrong track length

**What happened.** A test built a standard MIDI file from a byte array. The track header said 12 bytes and the events
were 13, so the file was malformed, `render` refused it and the test aborted. It looked like a fault in the new render
parameters.

**Root cause.** The length was counted in my head and the file was never opened by anything else before the test ran.

**Prevention.** Derive the track length from the array (`sizeof` of the events) or build the file with `juce::MidiFile`,
and when a test fails on a file it wrote itself, check the file first.

## A clamp copied from the wrong purpose

**What happened.** `fitParams` reused the "hold the range to plus or minus a thousand" rule from `checkStability`. For the
SH-101 filter peak (20 to 20000 Hz) that made the search range 20 to 1000, so the start value of 8000 was clamped to 1000
and the search reported no improvement while looking as if it had run.

**Root cause.** The rule exists for ranges of plus or minus a million (unbounded), but was written as a plain clamp on
every range.

**Prevention.** Cap a bound only when it is itself huge (`fitParams`), and state in the test the distance before and after,
so a search that cannot move is a failure and not a quiet no-op.

## A new example that clipped, and a fit that found tones that were not there

**What happened.** `examples/free-bell.ttl` peaked at 8.5 (+18 dBFS). The fitter run on its render reported twelve modes
for a bank of eight: the extra ones were the sum and difference tones that clipping makes (252, 793, 1842, 2364 Hz).
`valis-render` had printed "peak 8.52 exceeds full scale" and I did not read it before writing a test around the file.

**Root cause.** A noise burst sets every mode of a bank ringing at once, so the sum is far above the unit-impulse level
the gains are written for, and the output gain was set without rendering.

**Prevention.** Render a new example and read the peak before using it. `testFitModes` now asserts the peak is under 1.
A lint for gain staging estimated from the element chain is still in `TODO.md`.

## A tanh stage that turned a loud input into NaN, and a stem I reported before reading its log

**What happened.** `examples/metal-clang.ttl` rendered one click and then silence. The render log said `rms nan`, and I
started the mix without reading it. The NaN came from `val:Tanh` in `ADAA2` mode: `TanhShape::antiderivative2` computes
`exp(-2x)`, which overflows for x below about -354, and the difference of infinities that follows is NaN. A dry click
boosted by 27 dB, then a drive of 16, reached that size. The NaN then stayed in every filter after it.

**Root cause.** The shape was written for audio-sized input and nothing bounded it. No test covered `Adaa.h` directly.

**Prevention.** `antiderivative2` now reads large negative x from the positive side (the function is odd). `tests/dsp/AdaaTest.cpp`
checks finiteness out to 1e6, the join at the switch-over, and a swing between -4000 and 4000. Read the render log before
mixing: a `nan` or `exceeds full scale` there is a failed stem. Also, `track.py` picks the Release binary first, and that
binary can be older than the source, so it now takes `VALIS_RENDER` to name one.
