# scripts/electro83/reverb-return.py
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
from gen import build
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples")

N=[("in","Input",{}),
 ("lowCut","Biquad",{"mode":4,"frequency":450.0,"q":0.7}),
 ("hall","Reverb",{"size":0.9,"decay":1.7,"damping":0.45,"mix":1.0}),
 ("highCut","Biquad",{"mode":5,"frequency":9500.0,"q":0.7}),
 ("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("in","out","lowCut","in"),("lowCut","out","hall","in"),("hall","out","highCut","in"),
 ("highCut","out","limit","in"),("limit","out","out","in")]
P=[(0,"lowCut","frequency","Low cut","low_cut",100,2000,"Return","hertz"),
 (1,"hall","size","Size","size",0.2,2,"Room",None),
 (2,"hall","decay","Decay","decay",0.2,6,"Room","s"),
 (3,"hall","damping","Damping","damping",0,1,"Room",None),
 (4,"highCut","frequency","High cut","high_cut",2000,16000,"Return","hertz")]
build("reverb-return","Reverb return",
"""The wet side of a reverb, for a reverb return: the input is sent through a low cut, a room and a
high cut, and only the room comes out. Mix it under the dry sound. The low cut keeps the room off the
low end, which is why it suits high percussion such as a wood block, a tambourine or a shaker.""",
["Effect","Reverb","Return"],"AudioEffect","Audio",N,A,P,os.path.join(EX,"reverb-return.ttl"))
