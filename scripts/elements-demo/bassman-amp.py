# scripts/elements-demo/bassman-amp.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "circuits")

N = [("pitch", "MidiPitch", {}),
     ("pick", "Pluck", {"position": 0.18, "hardness": 0.6}),
     ("string", "StiffString", {"feedback": 0.994, "damping": 0.22, "dispersion": 0.004}),
     ("ampEnv", "Envelope", {"attack": 0.5, "decay": 2500.0, "sustain": 0.0, "release": 400.0}),
     ("ampVca", "VCA", {}),
     ("dc", "OnePole", {"mode": 1, "cutoff": 40.0}),
     ("input", "Gain", {"gain": 6.0}),
     # the passive network, then a stage to make up what it loses, then the valve
     ("stack", "ToneStack", {"treble": 0.5, "middle": 0.5, "bass": 0.5}),
     ("makeup", "Gain", {"gain": 12.0}),
     ("preamp", "Triode", {"mu": 100.0, "bias": -1.5}),
     ("valveDc", "OnePole", {"mode": 1, "cutoff": 30.0}),
     ("preTrim", "Gain", {"gain": -6.0}),
     ("power", "SoftSine", {"gain": 1.5}),
     ("cab", "OnePole", {"mode": 0, "cutoff": 5000.0}),
     # open while a note is held and for as long as the string rings; shut otherwise, so the valve stage is silent when idle
     ("hold", "Envelope", {"attack": 0.5, "decay": 10.0, "sustain": 1.0, "release": 5000.0}),
     ("gate", "VCA", {}),
     ("level", "Gain", {"gain": 6.0}),
     ("limit", "Limiter", {"ceiling": -1.0}),
     ("scope", "Oscilloscope", {}),
     ("spectrum", "FreqAnalyzer", {}),
     ("out", "Output", {})]

A = [("pitch", "out", "pick", "frequency"), ("pitch", "out", "string", "frequency"), ("pick", "out", "string", "in"),
     ("string", "out", "ampVca", "in"), ("ampEnv", "out", "ampVca", "cv"), ("ampVca", "out", "dc", "in"),
     ("dc", "out", "input", "in"), ("input", "out", "stack", "in"), ("stack", "out", "makeup", "in"),
     ("makeup", "out", "preamp", "in"), ("preamp", "out", "valveDc", "in"), ("valveDc", "out", "preTrim", "in"), ("preTrim", "out", "power", "in"),
     ("power", "out", "cab", "in"), ("cab", "out", "gate", "in"), ("hold", "out", "gate", "cv"), ("gate", "out", "level", "in"), ("level", "out", "limit", "in"),
     ("limit", "out", "scope", "in"), ("scope", "out", "spectrum", "in"), ("spectrum", "out", "out", "in")]

P = [(0, "stack", "treble", "Treble", "treble", 0, 1, "Tone stack", None),
     (1, "stack", "middle", "Middle", "middle", 0, 1, "Tone stack", None),
     (2, "stack", "bass", "Bass", "bass", 0, 1, "Tone stack", None),
     (3, "input", "gain", "Input", "input", -12, 24, "Preamp", "db"),
     (4, "preamp", "bias", "Valve bias", "bias", -10, 0, "Preamp", "volt"),
     (5, "preamp", "mu", "Valve mu", "mu", 1, 250, "Preamp", None),
     (6, "power", "gain", "Power stage drive", "power_drive", 0.1, 10, "Power", None),
     (7, "cab", "cutoff", "Speaker rolloff", "speaker", 1500, 12000, "Power", "hertz"),
     (8, "pick", "hardness", "Pick hardness", "pick_hardness", 0, 1, "String", None),
     (9, "pick", "position", "Pick position", "pick_position", 0.05, 0.5, "String", None),
     (10, "ampEnv", "decay", "Ring time", "ring_time", 300, 6000, "String", "ms"),
     (11, "level", "gain", "Level", "level", -30, 6, "Output", "db")]

HEADER = """A guitar through the amplifier that set the pattern for them all, the Fender '59 Bassman
(https://en.wikipedia.org/wiki/Fender_Bassman). A plucked string (val:Pluck into val:StiffString) feeds the amplifier's
passive treble, middle and bass network (val:ToneStack), a stage to make up the level the network loses, a valve preamp
(val:Triode, followed by a high-pass that removes the offset its bias adds), and a soft power stage (val:SoftSine) into a speaker rolloff.

The three tone controls are not independent bands: the middle control also changes the treble, and the network always
costs level, about 12 dB at the middle notch with everything at half. Turn Middle down for the scooped sound the
amplifier is known for, and push Input to drive the valve; the lopsided valve curve adds even harmonics as it
clips, and the spectrum on the panel shows it. The stack comes before the valve, as it does in the amplifier, so the
tone controls change what the valve distorts as well as what is heard.
Play it from E2 to E5, a note at a time."""

build("bassman-amp", "Bassman amp", HEADER, ["Guitar", "Amplifier", "Demonstration"],
      "AudioInstrument", "Midi", N, A, P, os.path.join(EX, "bassman-amp.ttl"))
