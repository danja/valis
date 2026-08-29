# Case study: Mutable Instruments Rings (Karplus-Strong)

Mutable Instruments Rings is a physical modelling resonator. Its most
recognisable mode is **Karplus-Strong string synthesis**: seed a delay line with
a brief noise burst, then feed the output back through a low-pass filter. The
delay length determines pitch; the low-pass filter determines how quickly high
harmonics decay. The full Turtle is at `examples/rings.ttl`.

This case study also documents the creation of the `val:CombFilter` element that
made it possible.

---

## Creating the CombFilter element

Rings needs a feedback comb filter not available in the existing element set.
Using the `/new-element` skill:

**1. Ontology** (`vocabs/valis.ttl`) — add a class near the other `val:Filter`
subclasses:

```turtle
val:CombFilter a rdfs:Class ; rdfs:subClassOf val:Filter ;
    rdfs:label "CombFilter" ;
    rdfs:comment "Feedback comb filter with one-pole lowpass in the
feedback path — the Karplus-Strong plucked-string algorithm." ;
    val:implementation "CombFilter" ;
    val:linear false ;
    lv2:port
      [ a lv2:InputPort,  lv2:AudioPort   ; lv2:symbol "in"        ] ,
      [ a lv2:OutputPort, lv2:AudioPort   ; lv2:symbol "out"       ] ,
      [ a lv2:InputPort,  lv2:ControlPort ; lv2:symbol "frequency" ;
        lv2:default 220.0 ; lv2:minimum 10.0 ; lv2:maximum 20000.0 ;
        units:unit units:hz ] ,
      [ a lv2:InputPort,  lv2:ControlPort ; lv2:symbol "feedback"  ;
        lv2:default 0.95  ; lv2:minimum 0.0   ; lv2:maximum 0.99  ] ,
      [ a lv2:InputPort,  lv2:ControlPort ; lv2:symbol "damping"   ;
        lv2:default 0.1   ; lv2:minimum 0.0   ; lv2:maximum 1.0   ] .
```

**2. C++ class** (`src/dsp/elements/Filters.cpp`) — inside
`namespace valis::elements`:

```cpp
class CombFilter final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        freqIndex     = controlIndex(type, "frequency");
        feedbackIndex = controlIndex(type, "feedback");
        dampingIndex  = controlIndex(type, "damping");
        buffer.assign(static_cast<std::size_t>(rate / 10.0) + 2, 0.0f);
        writePos = 0; filterState = 0.0f;
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0; filterState = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        const float freq     = std::clamp(controlAt(args, freqIndex,     220.0f), 10.0f, ...);
        const float feedback = std::clamp(controlAt(args, feedbackIndex,  0.95f), 0.0f, 0.99f);
        const float damping  = std::clamp(controlAt(args, dampingIndex,   0.1f),  0.0f, 1.0f);

        const int delayN = std::max(1, static_cast<int>(sampleRate / freq + 0.5));
        const float a = damping * 0.95f;   // LP coefficient

        for (int i = 0; i < args.numSamples; ++i)
        {
            // Read from delayN samples ago
            int readPos = writePos - delayN;
            if (readPos < 0) readPos += bufSize;
            const float delayed = buffer[readPos];

            // One-pole lowpass in the feedback path
            filterState = a * filterState + (1.0f - a) * delayed;

            buffer[writePos] = in[i] + filterState * feedback;
            out[i] = delayed;

            if (++writePos >= bufSize) writePos = 0;
        }
    }
    // ...
};
```

The key design decisions:

- **Buffer preallocated in `prepare()`** for a minimum pitch of 10 Hz (`sampleRate / 10` samples). No allocation ever happens in `process()`.
- **One-pole lowpass in feedback**: `a = damping * 0.95`. When `a=0` (damping=0) every frequency is sustained equally; when `a→0.95` (damping=1) high harmonics decay quickly, leaving a warm, muted tone.
- **Delay length changes at control rate** (every 32 samples). Notes retune immediately with no glide artefact at typical musical speeds.
- **Read-before-write**: the output is the delayed sample *before* the new input is written, which matches the standard comb filter convention.

**3. Registry** — add one line to `registerFilters()`:

```cpp
registry.add("CombFilter", &make<elements::CombFilter>);
```

**4. Test count** — the `ops_OpDispatcherTest` hardcodes the number of element
types; increment it by one each time a new element is added. The
`dsp_ElementRegistryTest` asserts ontology ↔ registry symmetry automatically.

---

## The Karplus-Strong circuit

```
MidiPitch ──(control)──► CombFilter.frequency
Noise ──► VCA ──► CombFilter ──► Gain ──► Output
Envelope ──(control)──► VCA.cv
```

### Excitation: timed noise burst

A `val:Noise` source feeds a `val:VCA`. A `val:Envelope` with zero sustain opens
the VCA for a few milliseconds at each note-on:

```turtle
:env a val:Envelope ;
    val:attack  0.1 ;   # ms — near-instantaneous
    val:decay   5.0 ;   # ms — the pluck width
    val:sustain 0.0 ;   # closes completely after decay
    val:release 1.0 .
```

The shorter the decay, the sharper and more percussive the pluck. Longer decays
produce a bowed-string character (the delay line fills through multiple periods).

### Resonator: the comb filter

```turtle
:comb a val:CombFilter ;
    val:frequency 220.0 ;
    val:feedback  0.98 ;
    val:damping   0.15 .
```

`feedback=0.98` means each cycle retains 98% of its amplitude — roughly 3.5
seconds of decay time at 220 Hz. `damping=0.15` gives a gentle treble roll-off,
characteristic of a real guitar string.

### Pitch tracking

```turtle
:aPitchComb a val:ControlArc ;
    val:from [ val:node :pitch ; val:port "out"       ] ;
    val:to   [ val:node :comb  ; val:port "frequency" ] .
```

`val:MidiPitch` outputs the current MIDI note as Hz. Connecting it to
`CombFilter.frequency` sets the delay length to exactly one period at that pitch.

---

## Tuning the sound

| Parameter | Low | High |
|---|---|---|
| `feedback` | short sustain, percussive | long decay, singing |
| `damping` | bright, metallic | dark, muted, wood-like |
| Envelope `decay` | sharp attack, pluck | slow fill, bowed |
| Noise `colour` | 0 = white (brighter, more initial fizz) | 1 = pink (warmer) |

---

## Extending toward the full Rings

Rings also includes a **modal bank** (parallel resonators at harmonic ratios).
That mode can be approximated in Valis without any new elements by running
several `val:StateVariable` filters in parallel, each tuned to a harmonic, and
summing them through a `val:Mixer`:

```
MidiPitch → Scale(min, max) → SVF1.cutoff (fundamental)
                             → SVF2.cutoff (2nd harmonic)  ...
SVF1.bp + SVF2.bp + ... → Mixer → Output
```

The `val:Scale` element (min=1×, max=2×) doubles the frequency for the second
harmonic, `max=3×` for the third, and so on. A full six-resonator bank is 30
lines of Turtle and four element types — no new C++ required.

## Loading the circuit

Paste `examples/rings.ttl` into the Turtle editor or use **File → Load
Circuit…**. Play a MIDI note; each note-on fires the pluck burst and the
resonator rings at the correct pitch. Five parameters are exposed: Sustain,
Damping, Pluck Decay, Noise Colour, and Volume.
