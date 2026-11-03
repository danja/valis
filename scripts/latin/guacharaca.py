# scripts/latin/guacharaca.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "circuits")

def stroke(p, note, count, decay, tooth, lvl):
    return [(p + "Gate", "NoteGate", {"note": note}),
            (p + "Shk", "Shaker", {"particles": count, "shake": decay, "sound": 1.2, "frequency": 4300.0,
                                   "resonance": 5.0, "regularity": 1.0, "scrape": tooth}),
            (p + "Lvl", "Gain", {"gain": lvl})]

N = (stroke("dn", 73, 14.0, 300.0, 40.0, 24.0) + stroke("up", 74, 10.0, 180.0, 48.0, 23.0) +
     [("sum", "Mixer", {}),
      ("wood", "Biquad", {"mode": 5, "frequency": 9000.0, "q": 0.7}),
      ("body", "Biquad", {"mode": 1, "frequency": 900.0, "gain": 0.0, "q": 0.9}),
      ("level", "Gain", {"gain": 0.0}),
      ("limit", "Limiter", {"ceiling": -1.0}),
      ("out", "Output", {})])

A = []
for p in ("dn", "up"):
    A += [(p + "Gate", "gate", p + "Shk", "trigger"), (p + "Gate", "velocity", p + "Shk", "velocity"),
          (p + "Shk", "out", p + "Lvl", "in"), (p + "Lvl", "out", "sum", "in")]
A += [("sum", "out", "wood", "in"), ("wood", "out", "body", "in"), ("body", "out", "level", "in"),
      ("level", "out", "limit", "in"), ("limit", "out", "out", "in")]

BOTH = "dnShk , :upShk"
P = [(0, "dnShk", "scrape", "Down teeth", "down_teeth", 8, 90, "Down stroke (note 73)", "hz"),
     (1, "dnShk", "shake", "Down length", "down_length", 20, 800, "Down stroke (note 73)", "ms"),
     (2, "dnLvl", "gain", "Down level", "down_level", -12, 40, "Down stroke (note 73)", "db"),
     (3, "upShk", "scrape", "Up teeth", "up_teeth", 8, 90, "Up stroke (note 74)", "hz"),
     (4, "upShk", "shake", "Up length", "up_length", 20, 800, "Up stroke (note 74)", "ms"),
     (5, "upLvl", "gain", "Up level", "up_level", -12, 40, "Up stroke (note 74)", "db"),
     (6, BOTH, "regularity", "Regularity", "regularity", 0, 1, "Rasp", None),
     (7, BOTH, "sound", "Tick length", "tick_length", 0.2, 12, "Rasp", "ms"),
     (8, BOTH, "frequency", "Rasp pitch", "rasp_pitch", 1500, 9000, "Rasp", "hz"),
     (9, BOTH, "resonance", "Ring", "ring", 0.5, 20, "Rasp", None),
     (10, "wood", "frequency", "Brightness", "brightness", 3000, 16000, "Tone", "hz"),
     (11, "body", "gain", "Tube body", "tube_body", -6, 12, "Tone", "db"),
     (12, "level", "gain", "Level", "level", -24, 18, "Output", "db")]

HEADER = """A guacharaca, the scraped wooden tube of Colombian cumbia and vallenato. A metal fork is drawn along a notched
tube: down on the beat, up between. Play note 73 for the down stroke and note 74 for the up stroke; velocity is the force.
Polyphonic in effect, since each stroke has its own shaker, but a repeat of the same note adds energy to the stroke
still sounding. https://en.wikipedia.org/wiki/Guacharaca

Each stroke is a val:Shaker with regularity 1, so the teeth of the notches go past as evenly spaced ticks. The down
stroke is longer and has more energy than the up stroke. Tick length is how long one tooth rings (short is dry and
wooden), Rasp pitch and Ring tune the tube, and Regularity below 1 loosens the ticks towards a shaker."""

build("guacharaca", "Guacharaca", HEADER, ["Percussion", "Acoustic", "Latin"], "AudioInstrument", "Midi", N, A, P,
      os.path.join(EX, "guacharaca.ttl"))
