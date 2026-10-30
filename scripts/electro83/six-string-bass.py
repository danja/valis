# scripts/electro83/six-string-bass.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples")

N=[("pitch","MidiPitch",{}),("dc","OnePole",{"mode":1,"cutoff":30.0}),
 ("pluckEnv","Envelope",{"attack":0.1,"decay":7.0,"sustain":0.0,"release":2.0}),
 ("noise","Noise",{"colour":0.2}),("pluckVca","VCA",{}),("pick","StateVariable",{"cutoff":3000.0,"resonance":0.7}),
 ("string","StiffString",{"feedback":0.993,"damping":0.12,"dispersion":0.01}),
 ("ampEnv","Envelope",{"attack":0.5,"decay":3000.0,"sustain":0.25,"release":160.0}),("ampVca","VCA",{}),
 ("pickup","StateVariable",{"cutoff":1900.0,"resonance":0.9}),("body","Mixer",{}),
 ("bodyLp","OnePole",{"mode":0,"cutoff":5200.0}),
 ("amp","Tanh",{"gain":1.7}),
 ("chorus","Chorus",{"rate":0.7,"depth":2.8,"mix":0.5}),
 ("trim","Gain",{"gain":5.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("pitch","out","string","frequency"),("pluckEnv","out","pluckVca","cv"),("noise","out","pluckVca","in"),
 ("pluckVca","out","pick","in"),("pick","lp","string","in"),
 ("string","out","ampVca","in"),("ampEnv","out","ampVca","cv"),
 ("ampVca","out","dc","in"),("dc","out","pickup","in"),("pickup","bp","body","in"),("dc","out","body","in"),
 ("body","out","bodyLp","in"),("bodyLp","out","amp","in"),("amp","out","chorus","in"),
 ("chorus","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"string","feedback","Sustain","sustain",0.9,0.998,"String",None),
 (1,"string","damping","Damping","damping",0,1,"String",None),
 (2,"pick","cutoff","Pick brightness","pick_bright",800,9000,"String","hertz"),
 (3,"ampEnv","decay","Ring time","ring_time",200,5000,"String","ms"),
 (4,"pickup","cutoff","Pickup centre","pickup_centre",500,5000,"Pickup","hertz"),
 (5,"amp","gain","Amp drive","amp_drive",1,10,"Amp",None),
 (6,"chorus","rate","Chorus rate","chorus_rate",0.1,6,"Chorus","hertz"),
 (7,"chorus","depth","Chorus depth","chorus_depth",0,6,"Chorus",None),
 (8,"chorus","mix","Chorus mix","chorus_mix",0,1,"Chorus",None)]
build("six-string-bass","Six-string bass guitar",
"""A six-string bass guitar of the kind Peter Hook played in Joy Division and New Order: strung
and tuned like a guitar but an octave lower (E A D G B E), and played high on the neck, so a
melody on it was often taken for a guitar. A noise burst through a low-pass excites a stiff
string (val:StiffString, tuned across its range), a band-pass stands in for the pickup, a soft
clip for the amplifier, and a chorus widens it.

Play it from E3 (52) to E5 (76), where the lead lines of the band's bass lived. It plays one
note at a time, as a melodic line does.""",
["Bass","Guitar","Post-punk"],"AudioInstrument","Midi",N,A,P,os.path.join(EX,"six-string-bass.ttl"))
