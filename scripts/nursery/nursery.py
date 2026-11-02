# scripts/nursery/nursery.py
#
# Eight instruments for a 140 bpm dance track whose tunes come from nursery rhymes. Each takes the
# job a dance track gives to an instrument (kick, bass, lead, arp, pad, snare, hat, riser) and builds
# the sound from a toy, a game or a nursery object. Run it to write examples/nursery-*.ttl.
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build

EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples")
OUT = lambda n: os.path.join(EX, n + ".ttl")

def tail(src, trim=-3.0, ceiling=-1.0, hp=24.0):
    """DC block, trim and limiter, the same finish for every circuit."""
    return ([("dc", "OnePole", {"mode": 1, "cutoff": hp}), ("trim", "Gain", {"gain": trim}),
             ("limit", "Limiter", {"ceiling": ceiling}), ("out", "Output", {})],
            [(src[0], src[1], "dc", "in"), ("dc", "out", "trim", "in"), ("trim", "out", "limit", "in"),
             ("limit", "out", "out", "in")])

# --------------------------------------------------------------------------------------------
# 1. Humpty Kick: the fall from the wall. A sine falls in pitch, and beside it a drumhead
#    resonator (six inharmonic modes) falls with it, so the thump carries a tuned wobble that
#    sounds like a toy drum dropped off a shelf. Any note plays it.
N = [("fall", "Envelope", {"attack": 0.2, "decay": 95.0, "sustain": 0.0, "release": 40.0}),
     ("sinePitch", "Scale", {"min": 46.0, "max": 250.0}),
     ("headPitch", "Scale", {"min": 92.0, "max": 520.0}),
     ("body", "Oscillator", {"shape": 0}),
     ("bodyEnv", "Envelope", {"attack": 0.3, "decay": 260.0, "sustain": 0.0, "release": 60.0}),
     ("bodyVca", "VCA", {}),
     ("hit", "Noise", {"colour": 0.3}),
     ("hitEnv", "Envelope", {"attack": 0.1, "decay": 7.0, "sustain": 0.0, "release": 5.0}),
     ("hitVca", "VCA", {}),
     ("head", "ModalBank", {"mode": 1, "decay": 0.28, "brightness": 0.55}),
     ("headLvl", "Gain", {"gain": -5.0}),
     ("mix", "Mixer", {}),
     ("drive", "Tanh", {"gain": 2.2})]
A = [("fall", "out", "sinePitch", "in"), ("fall", "out", "headPitch", "in"),
     ("sinePitch", "out", "body", "frequency"), ("body", "out", "bodyVca", "in"), ("bodyEnv", "out", "bodyVca", "cv"),
     ("hit", "out", "hitVca", "in"), ("hitEnv", "out", "hitVca", "cv"), ("hitVca", "out", "head", "in"),
     ("headPitch", "out", "head", "frequency"), ("head", "out", "headLvl", "in"),
     ("bodyVca", "out", "mix", "in"), ("headLvl", "out", "mix", "in"), ("mix", "out", "drive", "in")]
T, TA = tail(("drive", "out"), trim=-4.0, hp=22.0)
P = [(0, "sinePitch", "min", "Low pitch", "low_pitch", 30, 90, "Fall", "hertz"),
     (1, "sinePitch", "max", "Start pitch", "start_pitch", 90, 500, "Fall", "hertz"),
     (2, "fall", "decay", "Fall time", "fall_time", 20, 400, "Fall", "ms"),
     (3, "bodyEnv", "decay", "Body decay", "body_decay", 60, 900, "Body", "ms"),
     (4, "head", "decay", "Head ring", "head_ring", 0.05, 1.5, "Head", "s"),
     (5, "headLvl", "gain", "Head level", "head_level", -40, 6, "Head", "db"),
     (6, "drive", "gain", "Drive", "drive", 1, 8, "Body", None)]
build("nursery-humpty-kick", "Humpty Kick",
"""A kick drum named for Humpty Dumpty's fall. Two things fall together from a high pitch to a low one: a sine
(the thump) and a six-mode drumhead resonator (val:ModalBank, circular membrane) struck by a 7 ms noise burst.
The membrane's modes sit at 1, 1.59, 2.14, 2.30, 2.65 and 2.92 times its pitch, so as they fall they give the
low thump a tuned, wooden wobble, like a toy drum knocked off a shelf. A tanh stage joins them.

Any note plays it. It is not pitched, so use one note (the Fall controls set the pitch it lands on).
For a 140 bpm track the thump is about a sixteenth long (107 ms); lengthen Body decay for a rounder four on the floor.""",
["Kick", "Drum", "Dance"], "AudioInstrument", "Midi", N + T, A + TA, P, OUT("nursery-humpty-kick"))

# --------------------------------------------------------------------------------------------
# 2. Black Sheep Bass: "baa". The wavetable's vowel set is swept from a closed 'o' to an open 'a' on
#    every note, so the bass says "waaa". An octave divider adds a square sub an octave below.
N = [("pitch", "MidiPitch", {}), ("glide", "Slew", {"rise": 35.0, "fall": 35.0}),
     ("vowelEnv", "Envelope", {"attack": 1.0, "decay": 190.0, "sustain": 0.0, "release": 120.0}),
     ("vowelPos", "Scale", {"min": 0.04, "max": 0.78}),
     ("voice", "Wavetable", {"set": 2}),
     ("sub", "OctaveDivider", {"division": 0.0, "dry": 0.0, "wet": 1.0}),
     ("subLvl", "Gain", {"gain": -9.0}),
     ("fund", "Oscillator", {"shape": 0}), ("fundLvl", "Gain", {"gain": -3.0}),
     ("mix", "Mixer", {}),
     ("wool", "Tanh", {"gain": 3.0}),
     ("vcf", "Ladder", {"cutoff": 1400.0, "resonance": 0.18, "drive": 1.4}),
     ("ampEnv", "Envelope", {"attack": 2.0, "decay": 300.0, "sustain": 0.85, "release": 90.0}),
     ("vca", "VCA", {})]
A = [("pitch", "out", "glide", "in"), ("glide", "out", "voice", "frequency"),
     ("vowelEnv", "out", "vowelPos", "in"), ("vowelPos", "out", "voice", "position"),
     ("voice", "out", "sub", "in"), ("sub", "out", "subLvl", "in"),
     ("voice", "out", "mix", "in"), ("subLvl", "out", "mix", "in"),
     ("glide", "out", "fund", "frequency"), ("fund", "out", "fundLvl", "in"), ("fundLvl", "out", "mix", "in"),
     ("mix", "out", "wool", "in"), ("wool", "out", "vcf", "in"),
     ("vcf", "out", "vca", "in"), ("ampEnv", "out", "vca", "cv")]
T, TA = tail(("vca", "out"), trim=-3.0, hp=26.0)
P = [(0, "vowelPos", "min", "Resting vowel", "rest_vowel", 0, 1, "Voice", None),
     (1, "vowelPos", "max", "Start vowel", "start_vowel", 0, 1, "Voice", None),
     (2, "vowelEnv", "decay", "Baa length", "baa_length", 30, 800, "Voice", "ms"),
     (3, "glide", "rise", "Glide up", "glide_up", 0, 300, "Voice", "ms"),
     (4, "subLvl", "gain", "Sub level", "sub_level", -40, 6, "Mix", "db"),
     (8, "fundLvl", "gain", "Fundamental", "fundamental", -40, 6, "Mix", "db"),
     (5, "wool", "gain", "Wool", "wool", 0.5, 12, "Mix", None),
     (6, "vcf", "cutoff", "Cutoff", "cutoff", 120, 6000, "Filter", "hertz"),
     (7, "ampEnv", "decay", "Amp decay", "amp_decay", 40, 1500, "Amplifier", "ms")]
build("nursery-black-sheep-bass", "Black Sheep Bass",
"""A monophonic bass that says "baa". The oscillator is val:Wavetable on its Vowels set, whose position runs a, e, i,
o, u. Each note starts near 'o' and an envelope sweeps the position to 'a' over about 190 ms, the vowel
change of a sheep's "waaa". A sine at the played pitch keeps the fundamental the vowel formants would bury, an octave divider
(val:OctaveDivider) makes a square an octave below, and a
tanh stage adds wool (harmonics) before a ladder filter. A 35 ms glide slides between notes.

Play it from E1 to E3 (the vowel peaks are fixed harmonic numbers, so they move with the note).
Monophonic.""",
["Bass", "Vowel", "Dance"], "AudioInstrument", "Midi", N + T, A + TA, P, OUT("nursery-black-sheep-bass"))

# --------------------------------------------------------------------------------------------
# 3. Twinkle Lead: stars. A saw-and-triangle wavetable pair goes through a frequency shifter with
#    feedback (each repeat is moved up a few hertz, so the tone climbs and shimmers) and every note
#    throws a glint two octaves up from a marimba-bar resonator.
N = [("pitch", "MidiPitch", {}),
     ("tone", "Envelope", {"attack": 4.0, "decay": 420.0, "sustain": 0.35, "release": 300.0}),
     ("toneScale", "Scale", {"min": 0.2, "max": 0.62}),
     ("a", "Wavetable", {"set": 0}), ("b", "Wavetable", {"set": 0, "position": 0.1}),
     ("bRatio", "ControlMultiply", {"b": 1.0036}),
     ("aLvl", "Gain", {"gain": -3.0}), ("bLvl", "Gain", {"gain": -6.0}), ("mix", "Mixer", {}),
     ("shimmer", "FreqShifter", {"shift": 4.0, "feedback": 0.6, "mix": 0.45}),
     ("glintPitch", "ControlMultiply", {"b": 4.0}),
     ("glintNoise", "Noise", {"colour": 0.7}),
     ("glintEnv", "Envelope", {"attack": 0.1, "decay": 4.0, "sustain": 0.0, "release": 3.0}),
     ("glintVca", "VCA", {}),
     ("glint", "ModalBank", {"mode": 0, "decay": 0.7, "brightness": 1.0}),
     ("glintLvl", "Gain", {"gain": -8.0}),
     ("sum", "Mixer", {}),
     ("chorus", "Chorus", {"rate": 0.7, "depth": 1.6, "mix": 0.4}),
     ("ampEnv", "Envelope", {"attack": 3.0, "decay": 260.0, "sustain": 0.7, "release": 280.0}),
     ("vca", "VCA", {}),
     ("room", "Reverb", {"size": 0.9, "decay": 2.4, "damping": 0.5, "mix": 0.25})]
A = [("pitch", "out", "a", "frequency"), ("pitch", "out", "bRatio", "a"), ("bRatio", "out", "b", "frequency"),
     ("tone", "out", "toneScale", "in"), ("toneScale", "out", "a", "position"),
     ("a", "out", "aLvl", "in"), ("b", "out", "bLvl", "in"), ("aLvl", "out", "mix", "in"), ("bLvl", "out", "mix", "in"),
     ("mix", "out", "shimmer", "in"),
     ("pitch", "out", "glintPitch", "a"), ("glintPitch", "out", "glint", "frequency"),
     ("glintNoise", "out", "glintVca", "in"), ("glintEnv", "out", "glintVca", "cv"),
     ("glintVca", "out", "glint", "in"), ("glint", "out", "glintLvl", "in"),
     ("shimmer", "out", "sum", "in"), ("glintLvl", "out", "sum", "in"),
     ("sum", "out", "chorus", "in"), ("chorus", "out", "vca", "in"), ("ampEnv", "out", "vca", "cv"),
     ("vca", "out", "room", "in")]
T, TA = tail(("room", "out"), trim=-3.0, hp=60.0)
P = [(0, "toneScale", "min", "Brightness", "brightness", 0, 1, "Voice", None),
     (1, "toneScale", "max", "Attack brightness", "attack_brightness", 0, 1, "Voice", None),
     (2, "shimmer", "shift", "Shimmer shift", "shimmer_shift", -40, 80, "Shimmer", "hertz"),
     (3, "shimmer", "feedback", "Shimmer feedback", "shimmer_feedback", 0, 0.9, "Shimmer", None),
     (4, "shimmer", "mix", "Shimmer mix", "shimmer_mix", 0, 1, "Shimmer", None),
     (5, "glintLvl", "gain", "Glint level", "glint_level", -40, 6, "Glint", "db"),
     (6, "ampEnv", "release", "Release", "release", 20, 3000, "Amplifier", "ms"),
     (7, "room", "mix", "Room", "room", 0, 1, "Room", None)]
build("nursery-twinkle-lead", "Twinkle Lead",
"""A monophonic lead for "Twinkle, Twinkle, Little Star". Two slightly detuned wavetable voices (saw and triangle
region) pass through a frequency shifter with feedback (val:FreqShifter): it adds a few hertz to every partial and
returns the result to its own input, so each repeat sits a little higher and the tone climbs and shimmers, as the
barberpole effect does. Every note also strikes a marimba-bar resonator (val:ModalBank, mode 0) two octaves above, a
short glint like a star. Chorus and a small room finish it.

Play it from C4 to C6. Monophonic.""",
["Lead", "Shimmer", "Dance"], "AudioInstrument", "Midi", N + T, A + TA, P, OUT("nursery-twinkle-lead"))

# --------------------------------------------------------------------------------------------
# 4. Incy Wincy Arp: the spider climbing the spout. A plucked, dispersed string, a sympathetic
#    string a fifth above (the web), and a dotted-eighth echo at 140 bpm (321 ms) whose repeats are
#    shifted up an octave, so each echo climbs the spout.
N = [("pitch", "MidiPitch", {}),
     ("pluck", "Pluck", {"position": 0.18, "hardness": 0.65}),
     ("string", "StiffString", {"feedback": 0.988, "damping": 0.38, "dispersion": 0.2}),
     ("fifth", "ControlMultiply", {"b": 1.5}),
     ("web", "CombFilter", {"feedback": 0.82, "damping": 0.3}),
     ("webLvl", "Gain", {"gain": -9.0}),
     ("ampEnv", "Envelope", {"attack": 0.5, "decay": 900.0, "sustain": 0.0, "release": 450.0}),
     ("vca", "VCA", {}),
     ("dry", "Mixer", {}),
     ("echo", "Delay", {"time": 321.4, "feedback": 0.42}),
     ("climb", "PitchShift", {"pitch": 12.0, "mix": 1.0}),
     ("climbLvl", "Gain", {"gain": -7.0}),
     ("sum", "Mixer", {}),
     ("room", "Reverb", {"size": 0.7, "decay": 1.4, "damping": 0.6, "mix": 0.16})]
A = [("pitch", "out", "pluck", "frequency"), ("pitch", "out", "string", "frequency"), ("pluck", "out", "string", "in"),
     ("pitch", "out", "fifth", "a"), ("fifth", "out", "web", "frequency"),
     ("string", "out", "web", "in"), ("web", "out", "webLvl", "in"),
     ("string", "out", "dry", "in"), ("webLvl", "out", "dry", "in"),
     ("dry", "out", "vca", "in"), ("ampEnv", "out", "vca", "cv"),
     ("vca", "out", "echo", "in"), ("echo", "out", "climb", "in"), ("climb", "out", "climbLvl", "in"),
     ("vca", "out", "sum", "in"), ("climbLvl", "out", "sum", "in"), ("sum", "out", "room", "in")]
T, TA = tail(("room", "out"), trim=-3.0, hp=80.0)
P = [(0, "pluck", "hardness", "Hardness", "hardness", 0, 1, "Pluck", None),
     (1, "pluck", "position", "Pluck position", "position", 0.04, 0.5, "Pluck", None),
     (2, "string", "dispersion", "Dispersion", "dispersion", 0, 1, "String", None),
     (3, "string", "damping", "Damping", "damping", 0, 1, "String", None),
     (4, "webLvl", "gain", "Web level", "web_level", -40, 6, "Web", "db"),
     (5, "echo", "time", "Echo time", "echo_time", 20, 1000, "Echo", "ms"),
     (6, "echo", "feedback", "Echo feedback", "echo_feedback", 0, 0.9, "Echo", None),
     (7, "climbLvl", "gain", "Climb level", "climb_level", -40, 6, "Echo", "db")]
build("nursery-incy-wincy-arp", "Incy Wincy Arp",
"""An arpeggio voice for "Incy Wincy Spider". A plucked string (val:Pluck into val:StiffString with dispersion, so the
upper partials are stretched like a toy piano's) rings beside a sympathetic comb tuned a fifth above, the web. The sum
goes through an echo of 321 ms, a dotted eighth at 140 bpm, and each echo is shifted up an octave (val:PitchShift,
+12), so the echoes climb the spout while the note itself stays put.

Play it from C3 to C6, as short sixteenths. Monophonic: a string and a comb tuned to the last note played.""",
["Arp", "Pluck", "Dance"], "AudioInstrument", "Midi", N + T, A + TA, P, OUT("nursery-incy-wincy-arp"))

# --------------------------------------------------------------------------------------------
# 5. Rock-a-bye Pad: the cradle. A root, a fifth and an octave (always sounding together) of slightly
#    detuned saw and triangle waves, a filter that opens and closes with a cradle's rock (a swing every
#    two beats at 140 bpm), a phaser, an octave-up shimmer, and a long reverb.
N = [("pitch", "MidiPitch", {}),
     ("fifthR", "ControlMultiply", {"b": 1.4983}), ("octR", "ControlMultiply", {"b": 2.0012}),
     ("detR", "ControlMultiply", {"b": 1.0058}),
     ("o1", "Oscillator", {"shape": 1}), ("o2", "Oscillator", {"shape": 1}),
     ("o3", "Oscillator", {"shape": 3}), ("o4", "Oscillator", {"shape": 3}),
     ("g1", "Gain", {"gain": -8.0}), ("g2", "Gain", {"gain": -8.0}), ("g3", "Gain", {"gain": -7.0}), ("g4", "Gain", {"gain": -9.0}),
     ("mix", "Mixer", {}),
     ("rock", "LFO", {"rate": 1.1667}),
     ("rockDepth", "ControlMultiply", {"b": 650.0}),
     ("cutoff", "ControlAdd", {"b": 1250.0}),
     ("vcf", "Ladder", {"resonance": 0.12, "drive": 1.1}),
     ("ampEnv", "Envelope", {"attack": 450.0, "decay": 700.0, "sustain": 0.85, "release": 1500.0}),
     ("vca", "VCA", {}),
     ("swirl", "Phaser", {"rate": 0.18, "depth": 0.8, "feedback": 0.35, "mix": 0.4}),
     ("up", "PitchShift", {"pitch": 12.0, "mix": 0.28}),
     ("room", "Reverb", {"size": 1.4, "decay": 6.0, "damping": 0.45, "mix": 0.42})]
A = [("pitch", "out", "o1", "frequency"), ("pitch", "out", "detR", "a"), ("detR", "out", "o2", "frequency"),
     ("pitch", "out", "fifthR", "a"), ("fifthR", "out", "o3", "frequency"),
     ("pitch", "out", "octR", "a"), ("octR", "out", "o4", "frequency"),
     ("o1", "out", "g1", "in"), ("o2", "out", "g2", "in"), ("o3", "out", "g3", "in"), ("o4", "out", "g4", "in"),
     ("g1", "out", "mix", "in"), ("g2", "out", "mix", "in"), ("g3", "out", "mix", "in"), ("g4", "out", "mix", "in"),
     ("rock", "out", "rockDepth", "a"), ("rockDepth", "out", "cutoff", "a"), ("cutoff", "out", "vcf", "cutoff"),
     ("mix", "out", "vcf", "in"), ("vcf", "out", "vca", "in"), ("ampEnv", "out", "vca", "cv"),
     ("vca", "out", "swirl", "in"), ("swirl", "out", "up", "in"), ("up", "out", "room", "in")]
T, TA = tail(("room", "out"), trim=3.0, hp=70.0)
P = [(0, "cutoff", "b", "Cutoff", "cutoff", 200, 5000, "Filter", "hertz"),
     (1, "rockDepth", "b", "Rock depth", "rock_depth", 0, 2500, "Filter", "hertz"),
     (2, "rock", "rate", "Rock rate", "rock_rate", 0.05, 6, "Filter", "hertz"),
     (3, "vcf", "resonance", "Emphasis", "emphasis", 0, 0.8, "Filter", None),
     (4, "up", "mix", "Shimmer", "shimmer", 0, 1, "Space", None),
     (5, "room", "mix", "Room", "room", 0, 1, "Space", None),
     (6, "ampEnv", "attack", "Attack", "attack", 5, 3000, "Amplifier", "ms"),
     (7, "ampEnv", "release", "Release", "release", 50, 6000, "Amplifier", "ms")]
build("nursery-rock-a-bye-pad", "Rock-a-bye Pad",
"""A pad for "Rock-a-bye Baby". Each key sounds a root, a fifth and an octave together (two saws and two triangles, one
saw 10 cents sharp). The filter is opened and closed by a slow low-frequency oscillator at 1.1667 Hz, one swing
every two beats at 140 bpm, like a cradle rocking in time with the track. A phaser, a shimmer an octave up, and a
six-second reverb follow.

Hold one key per chord (the fifth and octave are built in). Play it from C2 to C4. Monophonic.""",
["Pad", "Lullaby", "Dance"], "AudioInstrument", "Midi", N + T, A + TA, P, OUT("nursery-rock-a-bye-pad"))

# --------------------------------------------------------------------------------------------
# 6. Rattle Snare: a baby's rattle on a drum. A noise-struck membrane gives the crack and a bank of
#    beads (val:Shaker) gives the rattle. Any note plays it.
N = [("crack", "Noise", {"colour": 0.1}),
     ("crackEnv", "Envelope", {"attack": 0.1, "decay": 5.0, "sustain": 0.0, "release": 4.0}),
     ("crackVca", "VCA", {}),
     ("skin", "ModalBank", {"mode": 2, "frequency": 196.0, "decay": 0.17, "brightness": 0.75}),
     ("skinLvl", "Gain", {"gain": -2.0}),
     ("snap", "Noise", {"colour": 0.55}),
     ("snapEnv", "Envelope", {"attack": 0.1, "decay": 115.0, "sustain": 0.0, "release": 60.0}),
     ("snapVca", "VCA", {}),
     ("snapHp", "Biquad", {"mode": 4, "frequency": 1700.0, "q": 0.8}),
     ("snapLvl", "Gain", {"gain": -7.0}),
     ("beads", "Shaker", {"particles": 140.0, "shake": 150.0, "sound": 2.5, "frequency": 5200.0, "resonance": 2.2}),
     ("beadsLvl", "Gain", {"gain": -5.0}),
     ("mix", "Mixer", {}),
     ("toy", "Redux", {"bits": 11.0, "downsample": 2.0}),
     ("drive", "Tanh", {"gain": 1.8})]
A = [("crack", "out", "crackVca", "in"), ("crackEnv", "out", "crackVca", "cv"), ("crackVca", "out", "skin", "in"),
     ("skin", "out", "skinLvl", "in"),
     ("snap", "out", "snapVca", "in"), ("snapEnv", "out", "snapVca", "cv"), ("snapVca", "out", "snapHp", "in"),
     ("snapHp", "out", "snapLvl", "in"), ("beads", "out", "beadsLvl", "in"),
     ("skinLvl", "out", "mix", "in"), ("snapLvl", "out", "mix", "in"), ("beadsLvl", "out", "mix", "in"),
     ("mix", "out", "toy", "in"), ("toy", "out", "drive", "in")]
T, TA = tail(("drive", "out"), trim=-4.0, hp=100.0)
P = [(0, "skin", "frequency", "Tune", "tune", 120, 400, "Skin", "hertz"),
     (1, "skin", "decay", "Skin ring", "skin_ring", 0.03, 0.8, "Skin", "s"),
     (2, "snapEnv", "decay", "Snap length", "snap_length", 20, 400, "Snap", "ms"),
     (3, "beads", "particles", "Beads", "beads", 5, 400, "Rattle", None),
     (4, "beads", "shake", "Rattle length", "rattle_length", 20, 800, "Rattle", "ms"),
     (5, "beadsLvl", "gain", "Rattle level", "rattle_level", -40, 6, "Rattle", "db"),
     (6, "toy", "bits", "Bits", "bits", 4, 16, "Toy", None),
     (7, "drive", "gain", "Drive", "drive", 1, 8, "Toy", None)]
build("nursery-rattle-snare", "Rattle Snare",
"""A snare for a baby-rattle backbeat. Three layers: a short noise burst strikes a rectangular-membrane resonator
(val:ModalBank, mode 2, tuned near 196 Hz) for the crack; a filtered noise tail gives the snap; and a bank of 140 beads
(val:Shaker, which models colliding particles and not filtered noise) gives a rattle that thins to single grains as it
dies. The sum goes through 11-bit, half-rate bit reduction for the cheap-toy grit.

Any note plays it. A harder note shakes the beads harder.""",
["Snare", "Drum", "Dance"], "AudioInstrument", "Midi", N + T, A + TA, P, OUT("nursery-rattle-snare"))

# --------------------------------------------------------------------------------------------
# 7. Ding Dong Hat: a bell, struck and shifted. Two plates of the plate-mode resonator, a
#    frequency shifter to make them metallic, and a sizzle of fine grains. The note's velocity chooses
#    closed (soft) or open (hard) because it sets how long everything rings.
N = [("vel", "MidiVelocity", {}),
     ("ring", "Scale", {"min": 0.03, "max": 0.5}),
     ("tail", "Scale", {"min": 22.0, "max": 380.0}),
     ("tick", "Noise", {"colour": 0.9}),
     ("tickEnv", "Envelope", {"attack": 0.1, "decay": 1.5, "sustain": 0.0, "release": 1.0}),
     ("tickVca", "VCA", {}),
     ("plateA", "ModalBank", {"mode": 3, "frequency": 3900.0, "brightness": 1.0}),
     ("plateB", "ModalBank", {"mode": 3, "frequency": 6300.0, "brightness": 1.0}),
     ("bells", "Mixer", {}),
     ("metal", "FreqShifter", {"shift": 230.0, "feedback": 0.0, "mix": 0.6}),
     ("sizzle", "Shaker", {"particles": 320.0, "shake": 40.0, "sound": 0.6, "frequency": 9500.0, "resonance": 1.0}),
     ("sizzleLvl", "Gain", {"gain": -9.0}),
     ("mix", "Mixer", {}),
     ("hp", "Biquad", {"mode": 4, "frequency": 4800.0, "q": 0.9}),
     ("ampEnv", "Envelope", {"attack": 0.2, "sustain": 0.0, "release": 20.0}),
     ("vca", "VCA", {})]
A = [("vel", "out", "ring", "in"), ("vel", "out", "tail", "in"),
     ("tick", "out", "tickVca", "in"), ("tickEnv", "out", "tickVca", "cv"),
     ("tickVca", "out", "plateA", "in"), ("tickVca", "out", "plateB", "in"),
     ("ring", "out", "plateA", "decay"), ("ring", "out", "plateB", "decay"),
     ("plateA", "out", "bells", "in"), ("plateB", "out", "bells", "in"), ("bells", "out", "metal", "in"),
     ("metal", "out", "mix", "in"), ("sizzle", "out", "sizzleLvl", "in"), ("sizzleLvl", "out", "mix", "in"),
     ("tail", "out", "ampEnv", "decay"), ("mix", "out", "hp", "in"), ("hp", "out", "vca", "in"), ("ampEnv", "out", "vca", "cv")]
T, TA = tail(("vca", "out"), trim=-2.0, hp=300.0)
P = [(0, "plateA", "frequency", "Bell A", "bell_a", 800, 9000, "Bells", "hertz"),
     (1, "plateB", "frequency", "Bell B", "bell_b", 800, 12000, "Bells", "hertz"),
     (2, "metal", "shift", "Metal", "metal", -800, 800, "Bells", "hertz"),
     (3, "ring", "min", "Closed ring", "closed_ring", 0.01, 0.3, "Length", "s"),
     (4, "ring", "max", "Open ring", "open_ring", 0.05, 1.5, "Length", "s"),
     (5, "tail", "max", "Open length", "open_length", 60, 1000, "Length", "ms"),
     (6, "sizzleLvl", "gain", "Sizzle", "sizzle", -40, 6, "Sizzle", "db"),
     (7, "hp", "frequency", "Low cut", "low_cut", 1000, 10000, "Tone", "hertz")]
build("nursery-ding-dong-hat", "Ding Dong Hat",
"""A hi-hat built from a bell. A 1.5 ms noise tick strikes two plate-mode resonators (val:ModalBank, mode 3, at 3.9 kHz
and 6.3 kHz), a frequency shifter (val:FreqShifter, +230 Hz) moves their partials apart so they are metallic and
no longer in tune with each other, and a bank of 320 fine grains (val:Shaker) adds sizzle. The note's velocity sets
how long it rings, through val:MidiVelocity: a soft note is a closed hat (30 ms), a hard one is open (up to 380 ms).

Any note plays it; play it with a range of velocities.""",
["Hat", "Drum", "Dance"], "AudioInstrument", "Midi", N + T, A + TA, P, OUT("nursery-ding-dong-hat"))

# --------------------------------------------------------------------------------------------
# 8. Wind-up Riser: a clockwork toy winding itself tight. A guiro (regular scrape) speeds up from 3 to
#    180 ticks a second, noise through a rising ladder filter and a rising frequency shifter climbs
#    behind it, and a saw rises in pitch. Hold a note: it builds for the time of the attack and holds at the top.
N = [("wind", "Envelope", {"attack": 5000.0, "decay": 100.0, "sustain": 1.0, "release": 150.0}),
     ("tickRate", "Scale", {"min": 3.0, "max": 180.0}),
     ("ratchet", "Shaker", {"particles": 60.0, "shake": 4000.0, "sound": 1.2, "frequency": 6500.0, "resonance": 4.0, "regularity": 1.0}),
     ("ratchetLvl", "Gain", {"gain": -4.0}),
     ("air", "Noise", {"colour": 0.2}),
     ("airCut", "Scale", {"min": 250.0, "max": 9000.0}),
     ("airFilter", "Ladder", {"resonance": 0.55, "drive": 1.3}),
     ("airShift", "Scale", {"min": 10.0, "max": 420.0}),
     ("climb", "FreqShifter", {"feedback": 0.82, "mix": 0.7}),
     ("climbLvl", "Gain", {"gain": -9.0}),
     ("pitch", "Scale", {"min": 110.0, "max": 1760.0}),
     ("tone", "Oscillator", {"shape": 1}),
     ("toneCut", "Ladder", {"cutoff": 2600.0, "resonance": 0.2}),
     ("toneLvl", "Gain", {"gain": -12.0}),
     ("mix", "Mixer", {}),
     ("grit", "Tanh", {"gain": 1.6}),
     ("swell", "Scale", {"min": 0.0, "max": 1.0}),
     ("vca", "VCA", {}),
     ("room", "Reverb", {"size": 0.9, "decay": 1.8, "damping": 0.5, "mix": 0.18})]
A = [("wind", "out", "tickRate", "in"), ("tickRate", "out", "ratchet", "scrape"), ("ratchet", "out", "ratchetLvl", "in"),
     ("wind", "out", "airCut", "in"), ("airCut", "out", "airFilter", "cutoff"), ("air", "out", "airFilter", "in"),
     ("wind", "out", "airShift", "in"), ("airShift", "out", "climb", "shift"),
     ("airFilter", "out", "climb", "in"), ("climb", "out", "climbLvl", "in"),
     ("wind", "out", "pitch", "in"), ("pitch", "out", "tone", "frequency"), ("tone", "out", "toneCut", "in"), ("toneCut", "out", "toneLvl", "in"),
     ("ratchetLvl", "out", "mix", "in"), ("climbLvl", "out", "mix", "in"), ("toneLvl", "out", "mix", "in"),
     ("mix", "out", "grit", "in"), ("wind", "out", "swell", "in"), ("grit", "out", "vca", "in"), ("swell", "out", "vca", "cv"),
     ("vca", "out", "room", "in")]
T, TA = tail(("room", "out"), trim=1.0, hp=120.0)
P = [(0, "wind", "attack", "Wind time", "wind_time", 500, 5000, "Build", "ms"),
     (1, "tickRate", "max", "Top tick rate", "top_tick_rate", 20, 400, "Ratchet", "hertz"),
     (2, "ratchetLvl", "gain", "Ratchet level", "ratchet_level", -40, 6, "Ratchet", "db"),
     (3, "airCut", "max", "Top cutoff", "top_cutoff", 1000, 16000, "Air", "hertz"),
     (4, "airShift", "max", "Top shift", "top_shift", 0, 1000, "Air", "hertz"),
     (5, "pitch", "max", "Top pitch", "top_pitch", 220, 4000, "Tone", "hertz"),
     (6, "toneLvl", "gain", "Tone level", "tone_level", -40, 6, "Tone", "db"),
     (7, "swell", "min", "Start level", "start_level", 0, 1, "Build", None)]
build("nursery-wind-up-riser", "Wind-up Riser",
"""A riser for a build, modelled on a clockwork toy being wound. One envelope, the winding, drives four things. A guiro
(val:Shaker with regularity 1, a regular scrape) speeds up from 3 to 180 ticks a second, so a slow tick becomes a
buzz, the ratchet. Noise through a ladder filter whose cutoff climbs from 250 Hz to 9 kHz goes through a frequency
shifter with feedback whose shift also rises, so the noise seems to climb forever. A saw rises from A2 to A6. The level swells with it.

Hold one note (any note) for the length of the build. The Wind time control is the length of the climb, up to 5 seconds;
at 140 bpm that is 2.9 bars, and 3.43 seconds is two bars. After that it holds at the top until the key is released.""",
["Riser", "FX", "Dance"], "AudioInstrument", "Midi", N + T, A + TA, P, OUT("nursery-wind-up-riser"))
