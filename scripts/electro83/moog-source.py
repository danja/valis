# scripts/electro83/moog-source.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples")

N=[("pitch","MidiPitch",{}),
 # two oscillators: a saw at the played pitch and a square an octave down, a little apart
 ("osc2Ratio","ControlMultiply",{"b":0.5035}),
 ("osc1","Oscillator",{"shape":1}),("osc2","Oscillator",{"shape":2}),
 ("osc1Lvl","Gain",{"gain":-3.0}),("osc2Lvl","Gain",{"gain":-4.0}),("mix","Mixer",{}),
 # the filter: an envelope sweep plus a share of the keyboard pitch
 ("filtEnv","Envelope",{"attack":1.0,"decay":170.0,"sustain":0.18,"release":110.0}),
 ("filtScale","Scale",{"min":160.0,"max":1900.0}),
 ("track","ControlMultiply",{"b":0.9}),("cutoff","ControlAdd",{}),
 ("vcf","Ladder",{"resonance":0.32,"drive":2.2}),
 ("ampEnv","Envelope",{"attack":1.5,"decay":260.0,"sustain":0.8,"release":70.0}),("vca","VCA",{}),
 ("dc","OnePole",{"mode":1,"cutoff":24.0}),("trim","Gain",{"gain":-3.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("pitch","out","osc1","frequency"),("pitch","out","osc2Ratio","a"),("osc2Ratio","out","osc2","frequency"),
 ("osc1","out","osc1Lvl","in"),("osc2","out","osc2Lvl","in"),("osc1Lvl","out","mix","in"),("osc2Lvl","out","mix","in"),
 ("filtEnv","out","filtScale","in"),("pitch","out","track","a"),("filtScale","out","cutoff","a"),("track","out","cutoff","b"),
 ("cutoff","out","vcf","cutoff"),("mix","out","vcf","in"),
 ("vcf","out","vca","in"),("ampEnv","out","vca","cv"),("vca","out","dc","in"),("dc","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"filtScale","min","Cutoff","cutoff",60,1500,"Filter","hertz"),
 (1,"filtScale","max","Envelope amount","env_amount",200,8000,"Filter","hertz"),
 (2,"vcf","resonance","Emphasis","emphasis",0,0.95,"Filter",None),
 (3,"track","b","Keyboard tracking","key_track",0,2,"Filter",None),
 (4,"filtEnv","decay","Filter decay","filter_decay",20,1500,"Filter","ms"),
 (5,"osc2Lvl","gain","Oscillator 2 level","osc2_level",-40,6,"Oscillators","db"),
 (6,"osc2Ratio","b","Oscillator 2 ratio","osc2_ratio",0.25,2.05,"Oscillators",None),
 (7,"vcf","drive","Drive","drive",1,10,"Filter",None),
 (8,"ampEnv","decay","Amp decay","amp_decay",20,2000,"Amplifier","ms"),
 (9,"ampEnv","release","Amp release","amp_release",5,1000,"Amplifier","ms")]
build("moog-source","Moog Source bass",
"""A monophonic analogue synthesiser voice in the manner of the Moog Source
(https://en.wikipedia.org/wiki/Moog_Source), which played the bass line of New Order's 1983
single "Blue Monday" from a sequencer. Two oscillators (a saw at the played pitch and a square
an octave below, slightly sharp so they beat), a four-pole 24 dB per octave low-pass filter
with emphasis, and two ADSR envelopes, one for the filter and one for the amplifier.

The cutoff is the sum of the filter envelope's sweep and a share of the keyboard pitch
(val:ControlAdd), so higher notes open the filter further. The short amp decay gives the
pulsing, plucked bass of a sequenced line. Play it from E1 to E3.""",
["Bass","Synth","Electro"],"AudioInstrument","Midi",N,A,P,os.path.join(EX,"moog-source.ttl"))
