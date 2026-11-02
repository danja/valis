# Inferno Choir

`inferno.py` writes `circuits/inferno-choir.ttl`, a synthesised human scream and wail. Run it from anywhere:

    python3 scripts/inferno/inferno.py

Edit the script and run it again, not the Turtle file. The test is `tests/engine/InfernoChoirTest.cpp`.

## Playing it

It is monophonic and answers MIDI notes and four continuous controllers. A controller rests at 0 until it is moved, and each is added to
the knob of the same name, so the knobs alone play the instrument.

| Controller | Macro | What it moves |
|---|---|---|
| CC1 (mod wheel) | Wail | vibrato rate (4.2 to 7.4 Hz) and depth |
| CC2 (breath) | Scream | pitch lift, formants up, brighter tone, subharmonic roughness, breath noise |
| CC3 | Vowel | the throat, from a closed "ooh" to an open "aah" |
| CC4 | Inferno | drive, wavefolder, furnace roar, crackle rate and level |

Note velocity also adds to Scream (Velocity to scream). The Controllers section sets how far the full travel of each controller reaches
(CC3 may be negative, to close the vowel as the wheel rises).

## How it is built

Controllers reach the circuit through `val:MidiCC` (new; `controller` picks the number, `initial` is the value before the host has sent it).
A control arc replaces the value of the port it reaches, so each macro is a `val:ControlAdd` whose `a` is the knob and whose `b` is the
controller scaled by a `val:Scale`; the Params bind `a` and the Scale `max`.

Pitch is the note, glided, multiplied by an onset swoop (an envelope that falls from 1.25 times), a release dive, a lift that grows with
Scream, and a vibrato and drift factor (slow Cry, mid Waver, fast Jitter). The voice is three `val:Glottal` oscillators (the upper pair detuned, one an octave
below; Rosenberg flow by default, selectable) into a ring modulator at half the pitch, a low-pass whose cutoff rises with Scream, and breath noise. Four formants share the
excitation; their frequencies are four `val:Scale` blocks driven by the vowel (plus a slow Mouth drift) and a throat-size factor that grows
with Scream. The fire is a drive and fold chain after the throat, a brown-noise roar and random crackles (`val:LFO` square, `val:Vary` and an
`val:Envelope` pulling a noise burst).

## What is known

Measured, not heard. Silent until played; in tune within 0.1 per cent from C3 to A5 when the expression is taken out; the peak reaches the
limiter's -1 dBFS ceiling when Scream and Inferno are both high. The `valis-shot is` check reads the wail as "a human voice" (share 0.60,
a yes) with the Rosenberg pulse, against 0.28 with saws, 0.31 with the Liljencrants-Fant pulse, 0.27 with Klatt, 0.40 with Radiated and 0.34 with
Cosine. It reads a calm steady note as an electric guitar and the scream, with Inferno high, as "an electric guitar" or "a synthesizer
keyboard preset": the distortion stage dominates it. It needs a listener.
