# scripts/elements-demo/arp-quantiser.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "circuits")

# All twelve pitch classes, scrambled over sixteen steps, so every scale has something to correct.
PATTERN = "C3 C#3 F#3 G3 D3 A3 Eb3 B3 F3 G#3 E3 Bb3 C4 D4 F#4 A4"

N = [("seqRef", "StepSequencer", {"pattern": PATTERN, "gatelength": 0.6, "division": 0.25}),
     ("quant", "Quantise", {"root": 0.0, "scale": 4.0}),
     ("neg", "ControlMultiply", {"b": -1.0}),
     ("delta", "ControlAdd", {}),
     # the same pattern again, moved by what the quantiser changed
     ("seqPlay", "StepSequencer", {"pattern": PATTERN, "gatelength": 0.6, "division": 0.25}),
     ("osc", "Oscillator", {"shape": 1}),
     ("filtEnv", "Envelope", {"attack": 1.0, "decay": 180.0, "sustain": 0.0, "release": 60.0}),
     ("filtScale", "Scale", {"min": 500.0, "max": 3500.0}),
     ("vcf", "Ladder", {"resonance": 0.3, "drive": 1.5}),
     ("ampEnv", "Envelope", {"attack": 1.0, "decay": 250.0, "sustain": 0.5, "release": 90.0}),
     ("vca", "VCA", {}),
     ("dc", "OnePole", {"mode": 1, "cutoff": 30.0}),
     ("level", "Gain", {"gain": -6.0}),
     ("limit", "Limiter", {"ceiling": -1.0}),
     ("out", "Output", {}),
     # the same notes, out of the plugin as MIDI
     ("midi", "NoteOut", {"velocity": 0.8, "channel": 1.0})]

A = [("seqRef", "note", "quant", "in"), ("seqRef", "note", "neg", "a"),
     ("quant", "out", "delta", "a"), ("neg", "out", "delta", "b"), ("delta", "out", "seqPlay", "transpose"),
     ("seqPlay", "pitch", "osc", "frequency"), ("seqPlay", "gate", "filtEnv", "gate"), ("seqPlay", "gate", "ampEnv", "gate"),
     ("filtEnv", "out", "filtScale", "in"), ("filtScale", "out", "vcf", "cutoff"),
     ("osc", "out", "vcf", "in"), ("vcf", "out", "vca", "in"), ("ampEnv", "out", "vca", "cv"),
     ("vca", "out", "dc", "in"), ("dc", "out", "level", "in"), ("level", "out", "limit", "in"), ("limit", "out", "out", "in"),
     ("seqPlay", "gate", "midi", "gate"), ("seqPlay", "pitch", "midi", "pitch")]

P = [(0, "quant", "root", "Root", "root", 0, 11, "Scale", None),
     (1, "quant", "scale", "Scale", "scale", 0, 6, "Scale", None),
     (2, "seqRef", "division", "Step length", "step_length", 0.0625, 1.0, "Sequence", None),
     (3, "seqRef", "gatelength", "Gate length", "gate_length", 0.05, 1.0, "Sequence", None),
     (4, "delta", "c", "Transpose", "transpose", -24, 24, "Sequence", "semitone12TET"),
     (5, "osc", "shape", "Wave", "wave", 0, 3, "Voice", None),
     (6, "filtScale", "min", "Cutoff", "cutoff", 100, 4000, "Voice", "hertz"),
     (7, "filtScale", "max", "Envelope amount", "env_amount", 200, 9000, "Voice", "hertz"),
     (8, "vcf", "resonance", "Resonance", "resonance", 0, 0.95, "Voice", None),
     (9, "ampEnv", "decay", "Decay", "decay", 20, 1500, "Voice", "ms"),
     (10, "midi", "velocity", "MIDI velocity", "midi_velocity", 0, 1, "MIDI out", None),
     (11, "midi", "channel", "MIDI channel", "midi_channel", 1, 16, "MIDI out", None),
     (12, "level", "gain", "Level", "level", -30, 6, "Output", "db")]

HEADER = """A sequence of notes from outside any key, forced into one, and sent out of the plugin as MIDI as well as played.
A step sequencer (val:StepSequencer) plays sixteen notes that use all twelve pitch classes. A copy of it is read by a
quantiser (val:Quantise), which snaps each note to the nearest note of the chosen scale and root; the difference moves
a second copy of the sequencer, so what sounds is the sequence in the key. The same notes leave the plugin as MIDI
(val:NoteOut), so a second instrument on another track can play along. The Root selector and the Scale selector (Free passes
the notes as written, then Semitones, Octaves, Fifths, Major, Minor, Pentatonic) change the key while it plays.

Step length, Gate length and Transpose belong to the sequence; Wave, Cutoff, Envelope amount, Resonance and Decay to the voice.
MIDI velocity and MIDI channel set what is sent out. The sequencer follows the host's musical position, and runs on its own
at the last tempo when the host is stopped, so this circuit plays whenever the plugin is running and does not need a key held.
In a host, send the plugin's MIDI output to another track; offline, valis-render --midi-out writes it to a file."""

build("arp-quantiser", "Arp quantiser", HEADER, ["Sequencer", "Generative", "Demonstration"],
      "AudioInstrument", "Midi", N, A, P, os.path.join(EX, "arp-quantiser.ttl"))

path = os.path.join(EX, "arp-quantiser.ttl")
t = open(path).read()
def enum(slot, points):
    global t
    start = t.index(":p%d a val:Param" % slot)
    end = t.index(" .\n", start)
    sp = " ;\n    lv2:portProperty lv2:integer , lv2:enumeration" + "".join(
        " ;\n    lv2:scalePoint [ rdfs:label \"%s\" ; rdf:value %d ]" % (l, i) for i, l in enumerate(points))
    t = t[:end] + sp + t[end:]
enum(0, ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"])
enum(1, ["Free", "Semitones", "Octaves", "Fifths", "Major", "Minor", "Pentatonic"])
enum(5, ["Sine", "Saw", "Square", "Triangle"])
for prop in ("division", "gatelength"):
    a = "val:target :seqRef ; val:property val:%s ;" % prop
    assert a in t
    t = t.replace(a, "val:target :seqRef , :seqPlay ; val:property val:%s ;" % prop)
t = t.replace("@prefix rdfs:", "@prefix rdf:   <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n@prefix rdfs:", 1)
open(path, "w").write(t)
