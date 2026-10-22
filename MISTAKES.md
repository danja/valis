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
