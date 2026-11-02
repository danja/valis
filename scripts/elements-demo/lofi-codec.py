# scripts/elements-demo/lofi-codec.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "circuits")

N = [("pitch", "MidiPitch", {}),
     ("detune", "ControlMultiply", {"b": 1.006}),
     ("oscA", "Oscillator", {"shape": 1}),
     ("oscB", "Oscillator", {"shape": 1}),
     ("oscSum", "Mixer", {}),
     ("oscLvl", "Gain", {"gain": -9.0}),
     ("hiss", "Noise", {"colour": 0.0}),
     ("hissLvl", "Gain", {"gain": -30.0}),
     ("body", "Mixer", {}),
     ("ampEnv", "Envelope", {"attack": 120.0, "decay": 400.0, "sustain": 0.8, "release": 500.0}),
     ("vca", "VCA", {}),
     # first take the hiss out from under the notes, then degrade what is left
     ("gate", "SpectralGate", {"threshold": -120.0}),
     ("codec", "Codec", {"bandwidth": 9000.0, "step": 4.0, "floor": 24.0, "mix": 1.0}),
     ("level", "Gain", {"gain": 0.0}),
     ("limit", "Limiter", {"ceiling": -1.0}),
     ("spectrum", "FreqAnalyzer", {}),
     ("scope", "Oscilloscope", {}),
     ("out", "Output", {})]

A = [("pitch", "out", "oscA", "frequency"), ("pitch", "out", "detune", "a"), ("detune", "out", "oscB", "frequency"),
     ("oscA", "out", "oscSum", "in"), ("oscB", "out", "oscSum", "in"), ("oscSum", "out", "oscLvl", "in"),
     ("oscLvl", "out", "body", "in"), ("hiss", "out", "hissLvl", "in"), ("hissLvl", "out", "body", "in"),
     ("body", "out", "vca", "in"), ("ampEnv", "out", "vca", "cv"), ("vca", "out", "gate", "in"),
     ("gate", "out", "codec", "in"), ("codec", "out", "level", "in"), ("level", "out", "limit", "in"),
     ("limit", "out", "spectrum", "in"), ("spectrum", "out", "scope", "in"), ("scope", "out", "out", "in")]

P = [(0, "gate", "threshold", "Gate threshold", "gate_threshold", -120, 0, "Spectral gate", "db"),
     (1, "hissLvl", "gain", "Hiss", "hiss", -60, -6, "Source", "db"),
     (2, "detune", "b", "Detune", "detune", 0.98, 1.02, "Source", None),
     (3, "codec", "bandwidth", "Bandwidth", "bandwidth", 500, 24000, "Codec", "hertz"),
     (4, "codec", "floor", "Floor", "floor", 0, 90, "Codec", "db"),
     (5, "codec", "step", "Step", "step", 0, 24, "Codec", "db"),
     (6, "codec", "mix", "Codec mix", "codec_mix", 0, 1, "Codec", None),
     (7, "ampEnv", "attack", "Attack", "attack", 1, 2000, "Envelope", "ms"),
     (8, "ampEnv", "release", "Release", "release", 10, 3000, "Envelope", "ms"),
     (9, "level", "gain", "Level", "level", -24, 12, "Output", "db")]

HEADER = """A pad degraded the way a low-bitrate audio codec does it, with the missing frequencies visible on the panel.
Two slightly detuned saws and a little hiss go through a spectral gate (val:SpectralGate) and then a codec (val:Codec).

The gate comes first and removes every frequency bin below its threshold, so it lifts the hiss out from underneath the
notes, which a filter cannot do. Raise Hiss to put it back, then raise the Gate threshold until it is gone and the
notes are left alone.

The codec then works on what is left, in three ways that you can hear and see on the spectrum:
  Bandwidth  removes everything above it, the lowpass of a coarse encoder.
  Floor      drops every bin more than that many decibels below the loudest, so the quiet partials between the notes'
             harmonics go and what remains warbles as the bins that pass change from frame to frame.
  Step       rounds the bins that survive to steps of that many decibels, the grain of coarse quantisation.
With Floor and Step at 0 and Bandwidth at its top the codec passes the sound unchanged (delayed). Codec mix blends
the original back.
Play it from C2 to C5, a note at a time: it is monophonic, and the pitch follows the last note played."""

build("lofi-codec", "Lo-fi codec", HEADER, ["Sound design", "Lo-fi", "Demonstration"],
      "AudioInstrument", "Midi", N, A, P, os.path.join(EX, "lofi-codec.ttl"))
