# scripts/elements-demo/vocoder-speak.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "circuits")

N = [("in", "Input", {}),
     # speech has little above 2.5 kHz that a vocoder can use, so lift it before it is analysed
     ("lift", "Biquad", {"mode": 2, "frequency": 2500.0, "gain": 6.0, "q": 0.7}),
     ("pitch", "MidiPitch", {}),
     ("detune", "ControlMultiply", {"b": 1.004}),
     ("oscA", "Oscillator", {"shape": 1}),
     ("oscB", "Oscillator", {"shape": 1}),
     ("saws", "Mixer", {}),
     ("hiss", "Noise", {"colour": 0.0}),
     ("hissHp", "Biquad", {"mode": 1, "frequency": 4000.0, "q": 0.7}),
     ("hissLvl", "Gain", {"gain": -14.0}),
     ("carrier", "Mixer", {}),
     ("ampEnv", "Envelope", {"attack": 8.0, "decay": 100.0, "sustain": 1.0, "release": 120.0}),
     ("gate", "VCA", {}),
     ("lpc", "LPC", {"order": 16.0, "mix": 1.0}),
     ("voc", "Vocoder", {"bands": 20.0, "low": 100.0, "high": 8000.0, "attack": 4.0, "release": 25.0, "mix": 1.0}),
     # a channel vocoder is quieter than the model, so it is made up here and a change of method does not change the loudness
     ("vocLvl", "Gain", {"gain": 17.0}),
     ("method", "AudioSelect", {"select": 0.0}),
     ("level", "Gain", {"gain": 0.0}),
     ("limit", "Limiter", {"ceiling": -1.0}),
     ("spectrum", "FreqAnalyzer", {}),
     ("out", "Output", {})]

A = [("in", "out", "lift", "in"), ("lift", "out", "lpc", "in"), ("lift", "out", "voc", "in"),
     ("pitch", "out", "oscA", "frequency"), ("pitch", "out", "detune", "a"), ("detune", "out", "oscB", "frequency"),
     ("oscA", "out", "saws", "in"), ("oscB", "out", "saws", "in"), ("saws", "out", "carrier", "in"),
     ("hiss", "out", "hissHp", "in"), ("hissHp", "out", "hissLvl", "in"), ("hissLvl", "out", "carrier", "in"),
     ("carrier", "out", "gate", "in"), ("ampEnv", "out", "gate", "cv"),
     ("gate", "out", "lpc", "carrier"), ("gate", "out", "voc", "carrier"),
     ("lpc", "out", "method", "a"), ("voc", "out", "vocLvl", "in"), ("vocLvl", "out", "method", "b"), ("gate", "out", "method", "c"),
     ("method", "out", "level", "in"), ("level", "out", "limit", "in"), ("limit", "out", "spectrum", "in"),
     ("spectrum", "out", "out", "in")]

P = [(0, "method", "select", "Method", "method", 0, 2, "Method", None),
     (1, "lpc", "order", "LPC order", "lpc_order", 2, 32, "LPC", None),
     (2, "lpc", "mix", "LPC mix", "lpc_mix", 0, 1, "LPC", None),
     (3, "voc", "bands", "Vocoder bands", "vocoder_bands", 2, 32, "Channel vocoder", None),
     (4, "voc", "release", "Vocoder release", "vocoder_release", 1, 500, "Channel vocoder", "ms"),
     (5, "voc", "mix", "Vocoder mix", "vocoder_mix", 0, 1, "Channel vocoder", None),
     (6, "oscA", "shape", "Carrier wave", "carrier_wave", 0, 3, "Carrier", None),
     (7, "detune", "b", "Detune", "detune", 0.98, 1.02, "Carrier", None),
     (8, "hissLvl", "gain", "Hiss", "hiss", -60, 0, "Carrier", "db"),
     (9, "lift", "gain", "Presence", "presence", 0, 18, "Voice", "db"),
     (10, "level", "gain", "Level", "level", -24, 18, "Output", "db")]

HEADER = """A talk box: play a note, speak into the input, and the note speaks. The voice on the plugin's audio input
shapes a sawtooth carrier that is played from MIDI. It is the same idea done two ways, and the Method selector lets you hear
one after the other on the same voice and the same note:

  LPC              val:LPC fits one smooth curve (an all-pole model, see https://en.wikipedia.org/wiki/Linear_predictive_coding)
                   to the voice's spectrum every 128 samples and puts that curve on the carrier. Its order sets how much
                   of the voice survives: 8 gives vowels, 16 about the clarity of speech, 32 follows every ripple.
  Channel vocoder  val:Vocoder splits both signals into bands and sets each carrier band to the voice's level in
                   that band (https://en.wikipedia.org/wiki/Vocoder). More bands, clearer words; fewer is the robot.
  Plain carrier    The note with no voice on it, for comparison.

Hiss adds high-passed noise to the carrier so that s, t and k have something to shape. Presence lifts the voice above
2.5 kHz before it is analysed, which helps consonants. Carrier wave and Detune set the buzz being shaped.
Silent unless a note is held and a voice is on the input. Play it from C2 to C5, a note at a time. Feed it a voice
recorded dry, with no reverb: the model follows the room as well as the speaker."""

build("vocoder-speak", "Vocoder speak", HEADER, ["Voice", "Vocoder", "Demonstration"],
      "AudioInstrument", "Midi", N, A, P, os.path.join(EX, "vocoder-speak.ttl"))

path = os.path.join(EX, "vocoder-speak.ttl")
t = open(path).read()
def enum(slot, points):
    global t
    start = t.index(":p%d a val:Param" % slot)
    end = t.index(" .\n", start)
    sp = " ;\n    lv2:portProperty lv2:integer , lv2:enumeration" + "".join(
        " ;\n    lv2:scalePoint [ rdfs:label \"%s\" ; rdf:value %d ]" % (l, i) for i, l in enumerate(points))
    t = t[:end] + sp + t[end:]
enum(0, ["LPC", "Channel vocoder", "Plain carrier"])
enum(6, ["Sine", "Saw", "Square", "Triangle"])
t = t.replace("@prefix rdfs:", "@prefix rdf:   <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n@prefix rdfs:", 1)
open(path, "w").write(t)
