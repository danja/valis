from gen import build
N=[("pitch","MidiPitch",{}),
 # a falling-saucer dive: pitch starts at four times the note and slides down to it
 ("dive","Envelope",{"attack":0.5,"decay":900.0,"sustain":0.0,"release":300.0}),("diveScale","Scale",{"min":1.0,"max":4.0}),
 ("diveMul","ControlMultiply",{}),
 ("wob","LFO",{"shape":0,"rate":7.5}),("wobScale","Scale",{"min":1.0,"max":1.04}),("wobMul","ControlMultiply",{}),
 ("osc","Oscillator",{"shape":1}),("sub","Oscillator",{"shape":2}),("subRatio","ControlMultiply",{"b":0.5}),
 ("mix","Mixer",{}),("fold","Fold",{"gain":4.5}),("crush","Redux",{"bits":6.0,"downsample":5.0,"jitter":0.35}),
 ("shift","FreqShifter",{"shift":-61.0,"feedback":0.72,"mix":0.6}),
 ("amp","Envelope",{"attack":3.0,"decay":1800.0,"sustain":0.0,"release":900.0}),("vca","VCA",{}),
 ("echo","Delay",{"time":233.0,"feedback":0.68}),("wet","Mixer",{}),("tone","OnePole",{"mode":0,"cutoff":6500.0}),("dc","OnePole",{"mode":1,"cutoff":30.0}),
 ("hall","Reverb",{"size":1.6,"decay":6.0,"damping":0.25,"mix":0.4}),
 ("trim","Gain",{"gain":-8.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("dive","out","diveScale","in"),("pitch","out","diveMul","a"),("diveScale","out","diveMul","b"),
 ("wob","out","wobScale","in"),("diveMul","out","wobMul","a"),("wobScale","out","wobMul","b"),
 ("wobMul","out","osc","frequency"),("wobMul","out","subRatio","a"),("subRatio","out","sub","frequency"),
 ("osc","out","mix","in"),("sub","out","mix","in"),("mix","out","fold","in"),("fold","out","crush","in"),
 ("crush","out","shift","in"),("shift","out","vca","in"),("amp","out","vca","cv"),
 ("vca","out","echo","in"),("vca","out","wet","in"),("echo","out","wet","in"),("wet","out","tone","in"),
 ("tone","out","dc","in"),("dc","out","hall","in"),("hall","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"diveScale","max","Dive","dive",1.0,8.0,"Saucer",None),
 (1,"dive","decay","Dive time","dive_time",50,3000,"Saucer","ms"),
 (2,"wob","rate","Warble rate","warble_rate",0.5,20,"Saucer","hertz"),
 (3,"wobScale","max","Warble depth","warble_depth",1.0,1.2,"Saucer",None),
 (4,"fold","gain","Fold","fold",0.5,12,"Damage",None),
 (5,"crush","bits","Bits","bits",2,16,"Damage",None),
 (6,"crush","downsample","Downsample","downsample",1,32,"Damage",None),
 (7,"shift","shift","Shift","shift",-300,300,"Damage","hertz"),
 (8,"shift","feedback","Shift feedback","shift_fb",0,0.9,"Damage",None),
 (9,"echo","time","Echo time","echo_time",20,900,"Space","ms"),
 (10,"echo","feedback","Echo feedback","echo_fb",0,0.95,"Space",None),
 (11,"hall","mix","Hall mix","hall_mix",0,1,"Space",None)]
build("halloween-ornaments2","Halloween: falling saucer",
"""A disturbing ornament for the track's breakdowns: each note dives from four times its pitch
down to the note, with a fast warble, then is wavefolded, bit-crushed with jitter and run
through a frequency shifter with high feedback, so the harmonics smear into something that
is not quite a pitch. A feedback echo set to a plain 233 ms gives it a stuttering tail.

The dive and the warble are ratios applied to the keyboard pitch by ControlMultiply stages.
Keep the notes short and let the echo carry them.""",
["Horror","Sci-fi","Effects"],"AudioInstrument","Midi",N,A,P,"/chalet/github/valis/examples/halloween-ornaments2.ttl")
