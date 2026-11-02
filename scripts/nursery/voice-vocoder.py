# scripts/nursery/voice-vocoder.py
#
# examples/nursery-voice-vocoder.ttl: an effect that makes a speech synthesiser sing the track's chord. The
# voice is the modulator of a channel vocoder; the carrier is made inside the circuit, so no MIDI is needed.
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build

EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples")

N = [("in", "Input", {}),
     # consonants carry little energy; a lift above 2.5 kHz keeps the bands there awake
     ("lift", "Biquad", {"mode": 2, "frequency": 2500.0, "gain": 7.0, "q": 0.7}),
     # the carrier: an open fifth of saws (A3, E4, A4) with two slightly sharp, and a veil of high noise for s and t
     ("saw1", "Oscillator", {"shape": 1, "frequency": 220.0}),
     ("saw2", "Oscillator", {"shape": 1, "frequency": 220.9}),
     ("saw3", "Oscillator", {"shape": 1, "frequency": 329.63}),
     ("saw4", "Oscillator", {"shape": 1, "frequency": 440.0}),
     ("saw5", "Oscillator", {"shape": 1, "frequency": 441.4}),
     ("sawMix", "Mixer", {}),
     ("sawLvl", "Gain", {"gain": -12.0}),
     ("hiss", "Noise", {"colour": 0.0}),
     ("hissHp", "Biquad", {"mode": 4, "frequency": 4500.0, "q": 0.7}),
     ("hissLvl", "Gain", {"gain": -18.0}),
     ("carrier", "Mixer", {}),
     ("voc", "Vocoder", {"bands": 24.0, "low": 110.0, "high": 9000.0, "attack": 2.0, "release": 35.0, "mix": 1.0}),
     ("choir", "Chorus", {"rate": 0.4, "depth": 2.2, "mix": 0.35}),
     ("room", "Reverb", {"size": 1.1, "decay": 3.0, "damping": 0.5, "mix": 0.3}),
     ("lvl", "Gain", {"gain": 14.0}),
     ("limit", "Limiter", {"ceiling": -1.0}),
     ("out", "Output", {})]
A = [("in", "out", "lift", "in"), ("lift", "out", "voc", "in"),
     ("saw1", "out", "sawMix", "in"), ("saw2", "out", "sawMix", "in"), ("saw3", "out", "sawMix", "in"),
     ("saw4", "out", "sawMix", "in"), ("saw5", "out", "sawMix", "in"), ("sawMix", "out", "sawLvl", "in"), ("sawLvl", "out", "carrier", "in"),
     ("hiss", "out", "hissHp", "in"), ("hissHp", "out", "hissLvl", "in"), ("hissLvl", "out", "carrier", "in"),
     ("carrier", "out", "voc", "carrier"), ("voc", "out", "choir", "in"), ("choir", "out", "room", "in"),
     ("room", "out", "lvl", "in"), ("lvl", "out", "limit", "in"), ("limit", "out", "out", "in")]
P = [(0, "voc", "bands", "Bands", "bands", 8, 32, "Vocoder", None),
     (1, "voc", "release", "Release", "release", 5, 200, "Vocoder", "ms"),
     (2, "voc", "mix", "Vocoder mix", "vocoder_mix", 0, 1, "Vocoder", None),
     (3, "hissLvl", "gain", "Consonant hiss", "hiss", -60, 0, "Carrier", "db"),
     (4, "sawLvl", "gain", "Chord level", "chord_level", -40, 0, "Carrier", "db"),
     (5, "choir", "mix", "Chorus", "chorus", 0, 1, "Space", None),
     (6, "room", "mix", "Room", "room", 0, 1, "Space", None),
     (7, "lvl", "gain", "Output", "output", -24, 24, "Space", "db")]
build("nursery-voice-vocoder", "Voice vocoder",
"""An effect that makes a speech synthesiser sing the track's chord. Put it after the voice on the same track. The voice is the
modulator of a 24-band channel vocoder (val:Vocoder, https://en.wikipedia.org/wiki/Vocoder); the carrier is inside the circuit and
needs no MIDI: five sawtooth oscillators on A3, E4 and A4 (an open fifth, two of them a few cents sharp so they beat) with a
veil of high-passed noise for the consonants. The open fifth sits on every chord of A minor (Am, F, Dm, E, G) without
clashing. A shelf lifts the voice above 2.5 kHz first so that s, t and k still open their bands. A chorus and a room follow.

Silent when the voice is silent. Play nothing: the input is the voice.""",
["Effect", "Vocoder", "Voice"], "AudioEffect", "Audio", N, A, P, os.path.join(EX, "nursery-voice-vocoder.ttl"))
