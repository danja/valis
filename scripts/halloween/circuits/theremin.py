from gen import build
N=[("pitch","MidiPitch",{}),
 # a legato glide between notes, as the hand travels from one pitch to the next
 ("glide","Slew",{"rise":140.0,"fall":140.0}),
 # each note scoops up from below its pitch, as a hand approaching the antenna does
 ("scoopEnv","Envelope",{"attack":170.0,"decay":10.0,"sustain":1.0,"release":50.0}),
 ("scoop","Scale",{"min":0.935,"max":1.0}),("scoopMul","ControlMultiply",{}),
 ("vib","LFO",{"shape":0,"rate":5.6}),("vibScale","Scale",{"min":1.0,"max":1.011}),("vibMul","ControlMultiply",{}),
 ("wander","Drift",{"centre":1.0,"amount":0.0025,"speed":0.7,"seed":13}),("wanderMul","ControlMultiply",{}),
 ("osc","Oscillator",{"shape":0}),("edge","Tanh",{"gain":1.8}),("tone","OnePole",{"mode":0,"cutoff":2600.0}),
 ("ampEnv","Envelope",{"attack":140.0,"decay":100.0,"sustain":1.0,"release":520.0}),("vca","VCA",{}),
 ("phase","Phaser",{"rate":0.12,"depth":0.8,"feedback":0.55,"mix":0.4}),
 ("echo","BucketBrigade",{"time":560.0,"feedback":0.58,"bandwidth":0.3}),("wet","Mixer",{}),
 ("hall","Reverb",{"size":1.9,"decay":9.0,"damping":0.5,"mix":0.45}),
 ("trim","Gain",{"gain":-11.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("scoopEnv","out","scoop","in"),("pitch","out","glide","in"),("glide","out","scoopMul","a"),("scoop","out","scoopMul","b"),
 ("vib","out","vibScale","in"),("scoopMul","out","vibMul","a"),("vibScale","out","vibMul","b"),
 ("vibMul","out","wanderMul","a"),("wander","out","wanderMul","b"),("wanderMul","out","osc","frequency"),
 ("osc","out","edge","in"),("edge","out","tone","in"),("tone","out","vca","in"),("ampEnv","out","vca","cv"),
 ("vca","out","phase","in"),("phase","out","echo","in"),("phase","out","wet","in"),("echo","out","wet","in"),
 ("wet","out","hall","in"),("hall","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(11,"glide","rise","Glide up","glide_up",0,1000,"Hand","ms"),
 (12,"glide","fall","Glide down","glide_down",0,1000,"Hand","ms"),
 (0,"scoop","min","Scoop depth","scoop_depth",0.8,1.0,"Hand",None),
 (1,"scoopEnv","attack","Scoop time","scoop_time",10,600,"Hand","ms"),
 (2,"vib","rate","Vibrato rate","vib_rate",2,9,"Hand","hertz"),
 (3,"vibScale","max","Vibrato depth","vib_depth",1.0,1.05,"Hand",None),
 (4,"wander","amount","Wander","wander",0,0.02,"Hand",None),
 (5,"edge","gain","Edge","edge",1,10,"Tone",None),
 (6,"tone","cutoff","Tone","tone",500,8000,"Tone","hertz"),
 (7,"phase","depth","Phaser depth","phase_depth",0,1,"Space",None),
 (8,"echo","feedback","Echo feedback","echo_fb",0,1.0,"Space",None),
 (9,"hall","mix","Hall mix","hall_mix",0,1,"Space",None),
 (10,"hall","decay","Hall decay","hall_decay",1,20,"Space","s")]
build("halloween-theremin","Halloween: theremin",
"""A theremin for the song's unearthly lead. The pitch travels between notes through a
val:Slew, and every note also scoops up from about a semitone below. A wide vibrato rides on
the pitch, and a slow random drift stops it sounding like a sequencer. The pitch then passes
through three ControlMultiply stages (scoop, vibrato, drift), each a ratio close to 1.

The tone is a sine pushed slightly into a soft clipper and low-passed, then a slow phaser, a
long echo and a cavernous hall. Play long notes with legato touch for the best effect.""",
["Theremin","Lead","Gothic"],"AudioInstrument","Midi",N,A,P,"/chalet/github/valis/examples/halloween-theremin.ttl")
