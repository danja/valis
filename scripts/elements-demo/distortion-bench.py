# scripts/elements-demo/distortion-bench.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "circuits")

# The curve knob feeds the first three-way switch directly (a value above 2 is held at 2) and, less 2, the second.
# Levels: each curve has a trim so a change of curve changes the sound and not the loudness.
TRIM = {"hard": 1.7, "soft": -0.4, "arc": -2.8, "diode": -7.6, "tri": 5.5}

N = [("pitch", "MidiPitch", {}),
     ("osc", "Oscillator", {"shape": 1}),
     ("ampEnv", "Envelope", {"attack": 4.0, "decay": 300.0, "sustain": 0.9, "release": 160.0}),
     ("vca", "VCA", {}),
     ("drive", "Gain", {"gain": 12.0}),
     # the five curves, side by side on the same driven signal
     ("hard", "HardClip", {"threshold": 0.5}),
     ("soft", "SoftSine", {"gain": 1.0}),
     ("arc", "SinArcTan", {"gain": 1.0}),
     ("diode", "Diode", {}),
     ("tri", "Triode", {"mu": 100.0, "bias": -1.5})]
for k in TRIM:
    N.append((k + "Trim", "Gain", {"gain": TRIM[k]}))
N += [("curve", "ControlAdd", {"a": 0.0}),
      ("curveHigh", "ControlAdd", {"b": -2.0}),
      ("pick1", "AudioSelect", {}),
      ("pick2", "AudioSelect", {}),
      ("dc", "OnePole", {"mode": 1, "cutoff": 30.0}),
      ("mix", "DryWet", {"mix": 1.0}),
      ("level", "Gain", {"gain": -6.0}),
      ("limit", "Limiter", {"ceiling": -1.0}),
      ("scope", "Oscilloscope", {}),
      ("spectrum", "FreqAnalyzer", {}),
      ("out", "Output", {})]

A = [("pitch", "out", "osc", "frequency"), ("osc", "out", "vca", "in"), ("ampEnv", "out", "vca", "cv"),
     ("vca", "out", "drive", "in")]
for k in TRIM:
    A += [("drive", "out", k, "in"), (k, "out", k + "Trim", "in")]
A += [("hardTrim", "out", "pick1", "a"), ("softTrim", "out", "pick1", "b"), ("arcTrim", "out", "pick1", "c"),
      ("pick1", "out", "pick2", "a"), ("diodeTrim", "out", "pick2", "b"), ("triTrim", "out", "pick2", "c"),
      ("curve", "out", "pick1", "select"), ("curve", "out", "curveHigh", "a"), ("curveHigh", "out", "pick2", "select"),
      ("pick2", "out", "dc", "in"), ("vca", "out", "mix", "dry"), ("dc", "out", "mix", "wet"),
      ("mix", "out", "level", "in"), ("level", "out", "limit", "in"), ("limit", "out", "scope", "in"),
      ("scope", "out", "spectrum", "in"), ("spectrum", "out", "out", "in")]

P = [(0, "curve", "a", "Curve", "curve", 0, 4, "Curve", None),
     (1, "osc", "shape", "Source wave", "source_wave", 0, 3, "Source", None),
     (2, "drive", "gain", "Drive", "drive", 0, 36, "Curve", "db"),
     (3, "hard", "threshold", "Hard clip threshold", "threshold", 0.05, 1.0, "Hard clip", None),
     (4, "soft", "gain", "Soft curves gain", "soft_gain", 0.1, 20, "Soft sine and arc tan", None),
     (5, "diode", "emissionCoefficient", "Diode ideality", "diode_n", 1.0, 2.0, "Diode", None),
     (6, "tri", "bias", "Triode bias", "triode_bias", -10, 0, "Triode", "volt"),
     (7, "tri", "mu", "Triode mu", "triode_mu", 1, 250, "Triode", None),
     (8, "mix", "mix", "Dry / wet", "dry_wet", 0, 1, "Output", None),
     (9, "level", "gain", "Level", "level", -24, 6, "Output", "db")]

HEADER = """A bench for the shaping curves, one at a time, on a note you play. A saw (or a sine, square or triangle) goes
through a drive stage and then through all five curves at once; the Curve selector picks which one you hear. The
spectrum and the waveform are on the panel, so a change of curve can be seen as well as heard.

  Hard clip (val:HardClip)    Flat tops at plus and minus the threshold: strong odd harmonics, the harshest of the five.
  Soft sine (val:SoftSine)    x / (|x| + 1): the gentlest knee, few high harmonics until it is driven hard.
  Sin arc tan (val:SinArcTan) x / sqrt(x squared + 1): between soft sine and tanh.
  Diode (val:Diode)           A 1N4148 junction after a 1 kilohm resistor: lopsided, so even harmonics as well as odd.
  Triode (val:Triode)         A valve stage: negative bias and mu give even and odd harmonics, a richer sound than a symmetric clipper.

Drive sets how hard every curve is hit. The other controls belong to one curve each and do nothing to the others.
Each curve has a level trim so that changing the curve changes the tone, not the loudness; the dry / wet knob blends
the undistorted note back in. A high-pass at 30 Hz removes the offset the lopsided curves add.
Play it from C2 to C5, a note at a time."""

build("distortion-bench", "Distortion bench", HEADER, ["Sound design", "Distortion", "Demonstration"],
      "AudioInstrument", "Midi", N, A, P, os.path.join(EX, "distortion-bench.ttl"))

# Post-process what gen.py cannot say: the curve and the source wave are enumerations, and the soft-curve gain
# drives two elements at once.
path = os.path.join(EX, "distortion-bench.ttl")
t = open(path).read()
def enum(slot, points):
    global t
    key = ":p%d a val:Param" % slot
    start = t.index(key)
    end = t.index(" .\n", start)
    sp = " ;\n    lv2:portProperty lv2:integer , lv2:enumeration" + "".join(
        " ;\n    lv2:scalePoint [ rdfs:label \"%s\" ; rdf:value %d ]" % (l, i) for i, l in enumerate(points))
    t = t[:end] + sp + t[end:]
enum(0, ["Hard clip", "Soft sine", "Sin arc tan", "Diode", "Triode"])
enum(1, ["Sine", "Saw", "Square", "Triangle"])
t = t.replace("val:target :soft ; val:property val:gain ;\n    lv2:name \"Soft curves gain\"",
              "val:target :soft , :arc ; val:property val:gain ;\n    lv2:name \"Soft curves gain\"")
t = t.replace("@prefix rdfs:", "@prefix rdf:   <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n@prefix rdfs:", 1)
open(path, "w").write(t)
