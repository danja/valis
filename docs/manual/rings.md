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

## Tuning the sound

| Parameter | Low | High |
|---|---|---|
| `feedback` | short sustain, percussive | long decay, singing |
| `damping` | bright, metallic | dark, muted, wood-like |
| Envelope `decay` | sharp attack, pluck | slow fill, bowed |
| Noise `colour` | 0 = white (more initial fizz) | 1 = pink (warmer) |

---

## Part II — Modal synthesis (`examples/rings-modal.ttl`)

`val:ModalBank` runs six parallel 2-pole resonators tuned to the natural
frequency ratios of a physical object. Unlike a comb filter (which has
harmonically spaced modes), a real bar, drumhead or plate has *inharmonic*
modes — and that inharmonicity is what makes each material sound like itself.

### Mode presets

| Mode | Object | Ratio pattern |
|------|--------|---------------|
| 0 | Marimba / wooden bar | 1 : 2.756 : 5.404 : 8.933 : 13.344 : 18.648 |
| 1 | Drumhead (circular membrane) | 1 : 1.593 : 2.136 : 2.296 : 2.653 : 2.917 |
| 2 | Rectangular membrane | 1 : 1.414 : 1.581 : 2.000 : 2.236 : 2.550 |
| 3 | Plate (clamped) | 1 : 1.414 : 2.000 : 2.236 : 2.449 : 2.828 |

The ratios come from the analytic solutions to the wave equation for each
geometry (Bessel function zeros for the drumhead, Euler-Bernoulli beam theory
for the bar, rectangular membrane modes from sum-of-squares).

### Mallet exciter

A mallet is modelled as a band-limited noise burst: white noise passed through a
lowpass filter to control hardness, then gated by a short ADSR:

```turtle
:noise a val:Noise ; val:colour 0.5 .
:tone  a val:OnePole ; val:cutoff 3000.0 ; val:mode 0.0 .   # LP, mallet hardness
:env   a val:Envelope ; val:attack 0.1 ; val:decay 8.0 ; val:sustain 0.0 ; val:release 1.0 .
:vca   a val:VCA .

Noise → OnePole(LP) → VCA → ModalBank
```

`tone.cutoff` is the "Hardness" parameter: 200 Hz = soft padded mallet; 20 kHz
= hard metal beater. The ADSR `decay` controls how long the mallet is in contact.

### Decay and brightness

`decay` is the T60 time (to −60 dB) of the fundamental. Higher modes decay
faster, proportional to their frequency ratio — a marimba's 18th harmonic dies
roughly 18× sooner than the fundamental, matching physical reality.

`brightness` controls the spectral tilt of the excitation coupling: at 0 only
the fundamental rings; at 1 all six modes are equally loud at the moment of
impact.

---

## Part III — Reed exciter (`examples/rings-reed.ttl`)

`val:Reed` implements a digital waveguide single-reed instrument. A cylindrical
bore with negative reflection at the open end produces an odd-harmonic spectrum
(1st, 3rd, 5th harmonic only) — the acoustic signature of a clarinet or oboe.
The instrument is self-oscillating: no external excitation is needed once mouth
pressure exceeds the reed's closure threshold.

### The reed model

```
p_reflected = −bore_output (negative reflection at open end)
delta_p     = pressure − p_reflected
reed_open   = sqrt(max(0, delta_p)) × stiffness_factor
p_new       = p_reflected + reed_open    (clipped to ±1)
```

When `pressure` is low the reed stays closed and the output is silence. Above
roughly 0.35 the reed opens enough to sustain oscillation. The pitch is set by
the bore delay length: `N = sampleRate / (2 × frequency)` samples, giving a
round trip of `1/frequency` seconds.

### Playing guide

| Pressure | Behaviour |
|----------|-----------|
| 0.00–0.35 | Silent (reed closed) |
| 0.35–0.60 | Stable fundamental tone |
| 0.60–0.80 | Brighter, slightly overblown |
| > 0.85 | Overblowing — register change |

`stiffness` controls the reed responsiveness. A soft reed (0) is oboe-like and
responsive to small pressure changes. A stiff reed (1) requires more pressure
but is more stable in the upper register.

`damping` controls bore losses. At 0 the bore is lossless and bright (metallic).
At 1 the bore is heavily damped, producing a stopped mouthpiece sound.

### Signal chain

```turtle
MidiPitch ──(control)──► Reed.frequency
Reed ──► Ladder(tone, resonance) ──► Gain ──► Output
```

The `val:Ladder` filter after the reed acts as the player's embouchure —
rolling off high harmonics smooths the square-wave-like reed output into a more
vocal clarinet tone.

---

## Loading the circuits

Three circuit files cover the Rings range:

| File | Sound | Exciter | Resonator |
|------|-------|---------|-----------|
| `examples/rings.ttl` | Plucked string | noise burst | CombFilter (KS) |
| `examples/rings-modal.ttl` | Bar / drum / plate | mallet noise burst | ModalBank |
| `examples/rings-reed.ttl` | Clarinet / oboe | self-oscillating | Reed waveguide |

Load any of them via the Turtle editor. For `rings-modal.ttl`, set the **Mode**
parameter (0–3) to switch between marimba, drumhead, membrane, and plate sounds
— each note-on fires the mallet burst at the new mode's frequency ratios.
