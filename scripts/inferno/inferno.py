# scripts/inferno/inferno.py
#
# circuits/inferno-choir.ttl: a synthesised human scream and wail, "the sound of the inferno".
# Run it to write the circuit. See scripts/inferno/README.md.
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build

EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "circuits")

# ---- the player: notes and four controllers -----------------------------------------------------
N = [
 # MIDI in. Every controller rests at 0 until it is moved, so the knobs alone play the instrument.
 ("cc1", "MidiCC", {"controller": 1, "initial": 0.0}),
 ("cc2", "MidiCC", {"controller": 2, "initial": 0.0}),
 ("cc3", "MidiCC", {"controller": 3, "initial": 0.0}),
 ("cc4", "MidiCC", {"controller": 4, "initial": 0.0}),
 ("vel", "MidiVelocity", {}),
 ("pitch", "MidiPitch", {}),
 ("glide", "Slew", {"rise": 40.0, "fall": 40.0}),

 # Four macros. Each is the knob (the resting value in `a`) plus its controller scaled by an amount.
 ("cc1Amt", "Scale", {"min": 0.0, "max": 1.0}),
 ("wail", "ControlAdd", {"a": 0.25}),
 ("cc2Amt", "Scale", {"min": 0.0, "max": 1.0}),
 ("velAmt", "Scale", {"min": 0.0, "max": 0.35}),
 ("scream", "ControlAdd", {"a": 0.2}),
 ("cc3Amt", "Scale", {"min": 0.0, "max": 1.0}),
 ("vowel", "ControlAdd", {"a": 0.45}),
 ("mouth", "Drift", {"speed": 1.3, "amount": 0.12, "centre": 0.0, "seed": 9.0}),
 ("vowelNow", "ControlAdd", {}),
 ("cc4Amt", "Scale", {"min": 0.0, "max": 1.0}),
 ("inferno", "ControlAdd", {"a": 0.3}),

 # Pitch: glide, onset swoop, release dive, scream lift, then vibrato and waver on top.
 ("swoopEnv", "Envelope", {"attack": 1.0, "decay": 240.0, "sustain": 0.0, "release": 10.0}),
 ("swoop", "Scale", {"min": 1.0, "max": 1.25}),
 ("diveEnv", "Envelope", {"attack": 1.0, "decay": 1.0, "sustain": 1.0, "release": 900.0}),
 ("dive", "Scale", {"min": 0.7, "max": 1.0}),
 ("lift", "Scale", {"min": 1.0, "max": 1.18}),
 ("vibRate", "Scale", {"min": 4.2, "max": 7.4}),
 ("vibLfo", "LFO", {"shape": 0}),
 ("vibDepth", "Scale", {"min": 0.0, "max": 0.045}),
 ("vibAmt", "ControlMultiply", {}),
 ("waver", "Drift", {"speed": 1.4, "amount": 0.012, "centre": 0.0, "seed": 7.0}),
 ("cry", "Drift", {"speed": 0.55, "amount": 0.05, "centre": 0.0, "seed": 5.0}),
 ("wander", "ControlAdd", {}),
 ("vibFactor", "ControlAdd", {"a": 1.0}),
 ("m1", "ControlMultiply", {}), ("m2", "ControlMultiply", {}), ("m3", "ControlMultiply", {}),
 ("hz", "ControlMultiply", {}),
 ("hzB", "ControlMultiply", {"b": 1.007}),
 ("hzLow", "ControlMultiply", {"b": 0.4985}),
 ("hzRing", "ControlMultiply", {"b": 0.5}),

 # Voice: three saw oscillators (the upper pair detuned, one an octave below) through a ring modulator at half the
 # pitch, which adds the half-integer partials of a screamed subharmonic.
 ("oscA", "Glottal", {"shape": 0}),
 ("oscB", "Glottal", {"shape": 0}),
 ("oscC", "Glottal", {"shape": 0}),
 ("openQ", "Scale", {"min": 0.7, "max": 0.35}),
 ("rdQ", "Scale", {"min": 1.8, "max": 0.45}),
 ("lvA", "Gain", {"gain": -4.0}), ("lvB", "Gain", {"gain": -5.0}), ("lvC", "Gain", {"gain": -12.0}),
 ("oscMix", "Mixer", {}),
 ("rough", "RingModulator", {}),
 ("roughAmt", "Scale", {"min": 0.0, "max": 0.55}),
 ("tilt", "OnePole", {"mode": 0}),
 ("tiltHz", "Scale", {"min": 1600.0, "max": 9000.0}),

 # Breath: white noise, louder as the scream grows, shaped by the same throat as the voice.
 ("hiss", "Noise", {"colour": 0.0}),
 ("breathAmt", "Scale", {"min": 0.06, "max": 0.6}),
 ("breathVca", "VCA", {}),
 ("excite", "Mixer", {}),

 # Throat: four formants morphing from a closed "ooh" to an open "aah", all raised as the scream strains.
 ("f1v", "Scale", {"min": 320.0, "max": 900.0}),
 ("f2v", "Scale", {"min": 850.0, "max": 1450.0}),
 ("f3v", "Scale", {"min": 2250.0, "max": 2800.0}),
 ("f4v", "Scale", {"min": 3300.0, "max": 3700.0}),
 ("strain", "Scale", {"min": 0.0, "max": 0.24}),
 ("shift", "ControlAdd", {"a": 1.0}),
 ("f1", "ControlMultiply", {}), ("f2", "ControlMultiply", {}), ("f3", "ControlMultiply", {}), ("f4", "ControlMultiply", {}),
 ("t1", "StateVariable", {"resonance": 11.0}), ("t2", "StateVariable", {"resonance": 12.0}),
 ("t3", "StateVariable", {"resonance": 13.0}), ("t4", "StateVariable", {"resonance": 14.0}),
 ("g1", "Gain", {"gain": 9.0}), ("g2", "Gain", {"gain": 6.0}), ("g3", "Gain", {"gain": 1.0}), ("g4", "Gain", {"gain": -4.0}),
 ("body", "Gain", {"gain": -5.0}),
 ("throat", "Mixer", {}),

 # Fire: drive and fold grow with the inferno macro; a furnace roar and random crackles join the voice.
 ("driveG", "Scale", {"min": 1.6, "max": 14.0}),
 ("burn", "Tanh", {}),
 ("foldG", "Scale", {"min": 1.0, "max": 3.2}),
 ("scorch", "Fold", {}),
 ("smoke", "OnePole", {"mode": 0, "cutoff": 7500.0}),
 ("makeup", "Gain", {}),
 ("makeupDb", "Scale", {"min": 0.0, "max": 9.0}),
 ("roarNoise", "Noise", {"colour": 1.0}),
 ("roarLp", "OnePole", {"mode": 0, "cutoff": 260.0}),
 ("roarWander", "Drift", {"speed": 0.7, "amount": 0.3, "centre": 0.6, "seed": 3.0}),
 ("roarVca", "VCA", {}),
 ("roarAmt", "Scale", {"min": 0.25, "max": 1.0}),
 ("roarVca2", "VCA", {}),
 ("roarLvl", "Gain", {"gain": -22.0}),
 ("crackRate", "Scale", {"min": 20.0, "max": 85.0}),
 ("crackClock", "LFO", {"shape": 3}),
 ("crackPick", "Vary", {"min": 0.0, "max": 1.0, "probability": 1.0, "seed": 11.0}),
 ("crackEnv", "Envelope", {"attack": 0.1, "decay": 7.0, "sustain": 0.0, "release": 4.0}),
 ("crackNoise", "Noise", {"colour": 0.0}),
 ("crackVca", "VCA", {}),
 ("crackHp", "OnePole", {"mode": 1, "cutoff": 1800.0}),
 ("crackAmt", "Scale", {"min": 0.0, "max": 1.0}),
 ("crackVca2", "VCA", {}),
 ("crackLvl", "Gain", {"gain": -20.0}),

 # Output: amplitude envelope (velocity scales it), choir width, the hall, and the usual finish.
 ("voiceLvl", "Gain", {"gain": -2.0}),
 ("sum", "Mixer", {}),
 ("choir", "Chorus", {"rate": 0.33, "depth": 2.4, "mix": 0.35}),
 ("ampEnv", "Envelope", {"attack": 70.0, "decay": 300.0, "sustain": 0.9, "release": 1100.0}),
 ("velLvl", "Scale", {"min": 0.55, "max": 1.0}),
 ("ampCv", "ControlMultiply", {}),
 ("vca", "VCA", {}),
 ("hall", "Reverb", {"size": 1.7, "decay": 5.5, "damping": 0.45, "mix": 0.38}),
 ("dc", "OnePole", {"mode": 1, "cutoff": 28.0}),
 ("trim", "Gain", {"gain": -2.0}),
 ("limit", "Limiter", {"ceiling": -1.0}),
 ("out", "Output", {}),
]

A = [
 # macros
 ("cc1", "out", "cc1Amt", "in"), ("cc1Amt", "out", "wail", "b"),
 ("cc2", "out", "cc2Amt", "in"), ("cc2Amt", "out", "scream", "b"),
 ("vel", "out", "velAmt", "in"), ("velAmt", "out", "scream", "c"),
 ("cc3", "out", "cc3Amt", "in"), ("cc3Amt", "out", "vowel", "b"),
 ("cc4", "out", "cc4Amt", "in"), ("cc4Amt", "out", "inferno", "b"),

 # pitch
 ("pitch", "out", "glide", "in"),
 ("swoopEnv", "out", "swoop", "in"), ("diveEnv", "out", "dive", "in"), ("scream", "out", "lift", "in"),
 ("glide", "out", "m1", "a"), ("swoop", "out", "m1", "b"),
 ("m1", "out", "m2", "a"), ("dive", "out", "m2", "b"),
 ("m2", "out", "m3", "a"), ("lift", "out", "m3", "b"),
 ("wail", "out", "vibRate", "in"), ("vibRate", "out", "vibLfo", "rate"),
 ("wail", "out", "vibDepth", "in"),
 ("vibLfo", "out", "vibAmt", "a"), ("vibDepth", "out", "vibAmt", "b"),
 ("vibAmt", "out", "vibFactor", "b"),
 ("waver", "out", "wander", "a"), ("cry", "out", "wander", "c"), ("wander", "out", "vibFactor", "c"),
 ("m3", "out", "hz", "a"), ("vibFactor", "out", "hz", "b"),
 ("hz", "out", "oscA", "frequency"),
 ("hz", "out", "hzB", "a"), ("hzB", "out", "oscB", "frequency"),
 ("hz", "out", "hzLow", "a"), ("hzLow", "out", "oscC", "frequency"),
 ("hz", "out", "hzRing", "a"), ("hzRing", "out", "rough", "frequency"),

 # voice: the glottal pulse tightens (a lower open quotient and Rd) as the scream grows
 ("scream", "out", "openQ", "in"), ("scream", "out", "rdQ", "in"),
 ("openQ", "out", "oscA", "open"), ("openQ", "out", "oscB", "open"), ("openQ", "out", "oscC", "open"),
 ("rdQ", "out", "oscA", "rd"), ("rdQ", "out", "oscB", "rd"), ("rdQ", "out", "oscC", "rd"),
 ("oscA", "out", "lvA", "in"), ("oscB", "out", "lvB", "in"), ("oscC", "out", "lvC", "in"),
 ("lvA", "out", "oscMix", "in"), ("lvB", "out", "oscMix", "in"), ("lvC", "out", "oscMix", "in"),
 ("oscMix", "out", "rough", "in"), ("scream", "out", "roughAmt", "in"), ("roughAmt", "out", "rough", "mix"),
 ("rough", "out", "tilt", "in"), ("scream", "out", "tiltHz", "in"), ("tiltHz", "out", "tilt", "cutoff"),
 ("hiss", "out", "breathVca", "in"), ("scream", "out", "breathAmt", "in"), ("breathAmt", "out", "breathVca", "cv"),
 ("tilt", "out", "excite", "in"), ("breathVca", "out", "excite", "in"),

 # throat
 ("vowel", "out", "vowelNow", "a"), ("mouth", "out", "vowelNow", "b"),
 ("vowelNow", "out", "f1v", "in"), ("vowelNow", "out", "f2v", "in"), ("vowelNow", "out", "f3v", "in"), ("vowelNow", "out", "f4v", "in"),
 ("scream", "out", "strain", "in"), ("strain", "out", "shift", "b"),
 ("f1v", "out", "f1", "a"), ("f2v", "out", "f2", "a"), ("f3v", "out", "f3", "a"), ("f4v", "out", "f4", "a"),
 ("shift", "out", "f1", "b"), ("shift", "out", "f2", "b"), ("shift", "out", "f3", "b"), ("shift", "out", "f4", "b"),
 ("f1", "out", "t1", "cutoff"), ("f2", "out", "t2", "cutoff"), ("f3", "out", "t3", "cutoff"), ("f4", "out", "t4", "cutoff"),
 ("excite", "out", "t1", "in"), ("excite", "out", "t2", "in"), ("excite", "out", "t3", "in"), ("excite", "out", "t4", "in"),
 ("t1", "bp", "g1", "in"), ("t2", "bp", "g2", "in"), ("t3", "bp", "g3", "in"), ("t4", "bp", "g4", "in"),
 ("g1", "out", "throat", "in"), ("g2", "out", "throat", "in"), ("g3", "out", "throat", "in"), ("g4", "out", "throat", "in"),
 ("excite", "out", "body", "in"), ("body", "out", "throat", "in"),

 # fire
 ("inferno", "out", "driveG", "in"), ("driveG", "out", "burn", "gain"),
 ("inferno", "out", "foldG", "in"), ("foldG", "out", "scorch", "gain"),
 ("throat", "out", "burn", "in"), ("burn", "out", "scorch", "in"), ("scorch", "out", "smoke", "in"),
 ("roarNoise", "out", "roarLp", "in"), ("roarLp", "out", "roarVca", "in"), ("roarWander", "out", "roarVca", "cv"),
 ("roarVca", "out", "roarVca2", "in"), ("inferno", "out", "roarAmt", "in"), ("roarAmt", "out", "roarVca2", "cv"),
 ("roarVca2", "out", "roarLvl", "in"),
 ("inferno", "out", "crackRate", "in"), ("crackRate", "out", "crackClock", "rate"),
 ("crackClock", "out", "crackPick", "trigger"), ("crackPick", "out", "crackEnv", "gate"),
 ("crackNoise", "out", "crackVca", "in"), ("crackEnv", "out", "crackVca", "cv"), ("crackVca", "out", "crackHp", "in"),
 ("crackHp", "out", "crackVca2", "in"), ("inferno", "out", "crackAmt", "in"), ("crackAmt", "out", "crackVca2", "cv"),
 ("crackVca2", "out", "crackLvl", "in"),

 # output
 ("smoke", "out", "makeup", "in"), ("inferno", "out", "makeupDb", "in"), ("makeupDb", "out", "makeup", "gain"),
 ("makeup", "out", "voiceLvl", "in"),
 ("voiceLvl", "out", "sum", "in"), ("roarLvl", "out", "sum", "in"), ("crackLvl", "out", "sum", "in"),
 ("sum", "out", "choir", "in"),
 ("vel", "out", "velLvl", "in"), ("ampEnv", "out", "ampCv", "a"), ("velLvl", "out", "ampCv", "b"),
 ("choir", "out", "vca", "in"), ("ampCv", "out", "vca", "cv"),
 ("vca", "out", "hall", "in"), ("hall", "out", "dc", "in"), ("dc", "out", "trim", "in"),
 ("trim", "out", "limit", "in"), ("limit", "out", "out", "in"),
]

# (slot, node, property, name, symbol, min, max, section, unit)
P = [
 # Macros: each is the resting position of an expression the controllers move.
 (0, "wail", "a", "Wail", "wail", 0, 1, "Voice", None),
 (1, "scream", "a", "Scream", "scream", 0, 1, "Voice", None),
 (2, "vowel", "a", "Vowel", "vowel", 0, 1, "Voice", None),
 (3, "inferno", "a", "Inferno", "inferno", 0, 1, "Voice", None),
 # Controllers: how far the full travel of each moves its macro.
 (4, "cc1Amt", "max", "CC1 wail", "cc1_wail", 0, 1, "Controllers", None),
 (5, "cc2Amt", "max", "CC2 scream", "cc2_scream", 0, 1, "Controllers", None),
 (6, "cc3Amt", "max", "CC3 vowel", "cc3_vowel", -1, 1, "Controllers", None),
 (7, "cc4Amt", "max", "CC4 inferno", "cc4_inferno", 0, 1, "Controllers", None),
 (8, "velAmt", "max", "Velocity to scream", "vel_scream", 0, 1, "Controllers", None),
 # Pitch
 (9, "glide", "rise", "Glide", "glide", 0, 1500, "Pitch", "ms"),
 (10, "swoop", "max", "Onset swoop", "onset_swoop", 1.0, 2.0, "Pitch", None),
 (11, "dive", "min", "Release dive", "release_dive", 0.25, 1.0, "Pitch", None),
 (12, "diveEnv", "release", "Dive time", "dive_time", 50, 5000, "Pitch", "ms"),
 (13, "lift", "max", "Scream lift", "scream_lift", 1.0, 1.6, "Pitch", None),
 (14, "vibDepth", "max", "Vibrato depth", "vibrato_depth", 0, 0.15, "Pitch", None),
 (15, "waver", "amount", "Waver", "waver", 0, 0.06, "Pitch", None),
 (36, "cry", "amount", "Cry", "cry", 0, 0.15, "Pitch", None),
 (37, "oscA , :oscB , :oscC", "jitter", "Jitter", "jitter", 0, 0.08, "Voice source", None),
 (40, "oscA , :oscB , :oscC", "shimmer", "Shimmer", "shimmer", 0, 1, "Voice source", None),
 (41, "oscA , :oscB , :oscC", "shape", "Pulse shape", "pulse_shape", 0, 6, "Voice source", None),
 (42, "oscA , :oscB , :oscC", "skew", "Speed quotient", "speed_quotient", 0.5, 6, "Voice source", None),
 (43, "openQ", "min", "Calm open quotient", "calm_open", 0.1, 1, "Voice source", None),
 (44, "openQ", "max", "Scream open quotient", "scream_open", 0.1, 1, "Voice source", None),
 (45, "rdQ", "min", "Calm Rd", "calm_rd", 0.3, 2.7, "Voice source", None),
 (46, "rdQ", "max", "Scream Rd", "scream_rd", 0.3, 2.7, "Voice source", None),
 (38, "mouth", "amount", "Mouth", "mouth", 0, 0.3, "Throat", None),
 (39, "makeupDb", "max", "Fire makeup", "fire_makeup", 0, 18, "Fire", "db"),
 (16, "hzB", "b", "Choir detune", "choir_detune", 1.0, 1.04, "Pitch", None),
 (17, "lvC", "gain", "Low voice", "low_voice", -40, 0, "Pitch", "db"),
 # Throat
 (18, "f1v", "max", "Jaw", "jaw", 500, 1100, "Throat", "hertz"),
 (19, "f2v", "max", "Tongue", "tongue", 900, 2200, "Throat", "hertz"),
 (20, "shift", "a", "Throat size", "throat_size", 0.7, 1.5, "Throat", None),
 (21, "t1", "resonance", "Sharpness", "sharpness", 1, 18, "Throat", None),
 (22, "body", "gain", "Body", "body", -40, 0, "Throat", "db"),
 (23, "breathAmt", "max", "Scream breath", "scream_breath", 0, 1, "Throat", None),
 # Fire
 (24, "roughAmt", "max", "Roughness", "roughness", 0, 1, "Fire", None),
 (25, "driveG", "max", "Burn", "burn", 2, 30, "Fire", None),
 (26, "foldG", "max", "Scorch", "scorch", 1, 6, "Fire", None),
 (27, "roarLvl", "gain", "Roar", "roar", -60, 0, "Fire", "db"),
 (28, "crackLvl", "gain", "Crackle", "crackle", -60, 0, "Fire", "db"),
 (29, "tiltHz", "max", "Scream brightness", "scream_brightness", 2000, 14000, "Fire", "hertz"),
 # Space and level
 (30, "ampEnv", "attack", "Swell", "swell", 2, 4000, "Output", "ms"),
 (31, "ampEnv", "release", "Release", "release", 30, 8000, "Output", "ms"),
 (32, "choir", "mix", "Choir width", "choir_width", 0, 1, "Output", None),
 (33, "hall", "mix", "Hall", "hall", 0, 1, "Output", None),
 (34, "hall", "decay", "Hall decay", "hall_decay", 0.5, 15, "Output", "s"),
 (35, "voiceLvl", "gain", "Level", "level", -30, 6, "Output", "db"),
]
# One knob that drives two elements: sharpness sets all four formant resonances.
P = [(s, ("t1 , :t2 , :t3 , :t4" if sym == "sharpness" else t), pr, nm, sym, lo, hi, sec, u)
     for (s, t, pr, nm, sym, lo, hi, sec, u) in P]

HEADER = """A synthesised human scream and wail, the sound of the inferno. It is a source-filter voice: three val:Glottal
oscillators (a detuned pair and one an octave below) stand in for the vocal folds, with a pulse shape you choose (Rosenberg by
default) whose open quotient and Rd tighten as the scream grows, a ring modulator at half the pitch adds the
half-integer partials of a screamed subharmonic, and four parallel band-pass formants (val:StateVariable) form the
throat, morphing from a closed "ooh" to an open "aah". White noise is the breath. A tanh drive, a wavefolder and a
low-pass are the strained larynx and the smoke, a brown-noise furnace roar and random crackles are the fire, and a
chorus and a long hall are the pit.

Play it from about A2 to A5 (a wail sits low, a scream above C5). Monophonic. It answers MIDI notes (pitch, and velocity
adds scream) and four controllers, each added to its knob:
  CC1 (mod wheel)  Wail: vibrato rate and depth.
  CC2 (breath)     Scream: lifts the pitch, raises the formants, opens the brightness, roughens the tone, adds breath.
  CC3              Vowel: closed "ooh" to open "aah".
  CC4              Inferno: drive, fold, roar and crackle.
Each controller's reach is set by its CC amount knob, and a controller rests at 0 until it is moved."""

build("inferno-choir", "Inferno Choir", HEADER, ["Voice", "Scream", "Horror", "Drone"], "AudioInstrument", "Midi",
      N, A, P, os.path.join(EX, "inferno-choir.ttl"))
