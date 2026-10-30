# scripts/electro83/speaker-room.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples")

N=[("in","Input",{}),
 ("lowCut","Biquad",{"mode":4,"frequency":38.0,"q":0.9}),
 ("cone","Biquad",{"mode":1,"frequency":62.0,"gain":7.0,"q":1.4}),
 ("push","Tanh",{"gain":1.7}),
 ("highCut","Biquad",{"mode":5,"frequency":9000.0,"q":0.7}),
 ("room","Reverb",{"size":0.22,"decay":0.35,"damping":0.65,"mix":0.22}),
 ("trim","Gain",{"gain":-1.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("in","out","lowCut","in"),("lowCut","out","cone","in"),("cone","out","push","in"),("push","out","highCut","in"),
 ("highCut","out","room","in"),("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"lowCut","frequency","Low cut","low_cut",20,120,"Speaker","hertz"),
 (1,"cone","frequency","Cone resonance","cone_freq",40,120,"Speaker","hertz"),
 (2,"cone","gain","Cone gain","cone_gain",0,15,"Speaker","db"),
 (3,"push","gain","Drive","drive",1,6,"Speaker",None),
 (4,"highCut","frequency","High cut","high_cut",2000,16000,"Speaker","hertz"),
 (5,"room","mix","Room","room_mix",0,1,"Room",None),
 (6,"room","decay","Room decay","room_decay",0.1,3,"Room","s")]
build("speaker-room","Speaker and room",
"""A sound played through a loudspeaker and picked up again by a microphone in the room,
which is how the kick on a 1983 electronic record was often made fuller: the drum machine
went to a studio monitor and the monitor's sound, with the room in it, was recorded.

The model is a low cut, a bell for the cone's resonance, a soft clip where the cone runs out
of travel, a high cut, and a short small room. Feed it audio with valis-render -i, or
insert it after a drum or a voice. It has no input of its own when nothing is connected.""",
["Effect","Speaker","Room"],"AudioEffect","Audio",N,A,P,os.path.join(EX,"speaker-room.ttl"))
