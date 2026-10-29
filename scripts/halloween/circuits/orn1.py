from gen import build
N=[("pitch","MidiPitch",{}),("ghostRatio","ControlMultiply",{"b":1.0075}),("bellRatio","ControlMultiply",{"b":2.0}),
 # ghost choir: two vowel wavetables a few cents apart, the vowel drifting
 ("vowelDrift","Drift",{"centre":0.45,"amount":0.18,"speed":0.25,"seed":5}),
 ("choirA","Wavetable",{"set":2}),("choirB","Wavetable",{"set":2}),("choirMix","Mixer",{}),
 ("choirLvl","Gain",{"gain":-8.0}),
 ("swell","Envelope",{"attack":1400.0,"decay":600.0,"sustain":0.9,"release":2600.0}),("choirVca","VCA",{}),
 ("shimmer","FreqShifter",{"shift":2.7,"feedback":0.42,"mix":0.3}),
 # glass bell: a noise tick into a modal resonator tuned an octave above the note
 ("tick","Noise",{"colour":0.0}),("tickEnv","Envelope",{"attack":0.1,"decay":4.0,"sustain":0.0,"release":2.0}),("tickVca","VCA",{}),
 ("bell","ModalBank",{"decay":7.0,"brightness":0.75,"mode":1}),("bellLvl","Gain",{"gain":-4.0}),
 ("sum","Mixer",{}),("chorus","Chorus",{"rate":0.31,"depth":3.2,"mix":0.5}),
 ("spec","SpectralDelay",{"time":420.0,"spread":0.6,"feedback":0.5,"mix":0.35}),
 ("hall","Reverb",{"size":2.0,"decay":14.0,"damping":0.45,"mix":0.5}),
 ("trim","Gain",{"gain":10.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("pitch","out","ghostRatio","a"),("pitch","out","bellRatio","a"),
 ("pitch","out","choirA","frequency"),("ghostRatio","out","choirB","frequency"),
 ("vowelDrift","out","choirA","position"),("vowelDrift","out","choirB","position"),
 ("choirA","out","choirMix","in"),("choirB","out","choirMix","in"),("choirMix","out","choirLvl","in"),
 ("choirLvl","out","choirVca","in"),("swell","out","choirVca","cv"),("choirVca","out","shimmer","in"),
 ("tickEnv","out","tickVca","cv"),("tick","out","tickVca","in"),("tickVca","out","bell","in"),("bellRatio","out","bell","frequency"),
 ("bell","out","bellLvl","in"),
 ("shimmer","out","sum","in"),("bellLvl","out","sum","in"),
 ("sum","out","chorus","in"),("chorus","out","spec","in"),("spec","out","hall","in"),("hall","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"vowelDrift","amount","Vowel wander","vowel_wander",0,0.5,"Choir",None),
 (1,"vowelDrift","centre","Vowel","vowel",0,1,"Choir",None),
 (2,"swell","attack","Swell","swell",50,4000,"Choir","ms"),
 (3,"swell","release","Fade","fade",100,8000,"Choir","ms"),
 (4,"choirLvl","gain","Choir level","choir_level",-40,0,"Mix","db"),
 (5,"bellLvl","gain","Bell level","bell_level",-40,6,"Mix","db"),
 (6,"bell","decay","Bell decay","bell_decay",0.5,10,"Bell","s"),
 (7,"shimmer","shift","Shimmer shift","shimmer_shift",-20,20,"Shimmer","hertz"),
 (8,"spec","time","Spectral delay","spec_time",50,900,"Space","ms"),
 (9,"hall","mix","Hall mix","hall_mix",0,1,"Space",None)]
build("halloween-ornaments1","Halloween: ghost choir and glass",
"""An eerie ornament voice: two vowel-wavetable choirs a few cents apart, with the vowel
wandering at random, swell in over more than a second and fade over several, run through a
slowly rising frequency shifter feedback loop; and over it a glass bell, a noise tick into
a modal resonator an octave above the note. A chorus, a spectral delay and a long hall
finish it.

Best played as a few held notes or a slow, wide chord.""",
["Ambient","Horror","Choir"],"AudioInstrument","Midi",N,A,P,"/chalet/github/valis/examples/halloween-ornaments1.ttl")
