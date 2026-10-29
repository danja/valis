from gen import build
N=[]; A=[]
def voice(p, note, fmin, fmax, decay, pdec, amp_dec, sat=None):
    N.append((p+"Gate","NoteGate",{"note":note}))
    N.append((p+"PEnv","Envelope",{"attack":0.2,"decay":pdec,"sustain":0.0,"release":2.0}))
    N.append((p+"PScale","Scale",{"min":fmin,"max":fmax}))
    N.append((p+"AEnv","Envelope",{"attack":1.0,"decay":amp_dec,"sustain":0.0,"release":amp_dec*0.5}))
    N.append((p+"Osc","TwinTBridge",{"decay":decay}))
    N.append((p+"Vca","VCA",{}))
    A.extend([(p+"Gate","gate",p+"PEnv","gate"),(p+"Gate","gate",p+"AEnv","gate"),(p+"Gate","gate",p+"Osc","trigger"),
      (p+"Gate","velocity",p+"Osc","velocity"),(p+"PEnv","out",p+"PScale","in"),(p+"PScale","out",p+"Osc","frequency"),
      (p+"AEnv","out",p+"Vca","cv")])
# kick: body + sub
voice("k",36,46.0,200.0,650.0,45.0,3000.0)
N.append(("kSub","TwinTBridge",{"frequency":34.0,"decay":1400.0}))
N.append(("kSubGain","Gain",{"gain":-3.0}))
N.append(("kMix","Mixer",{}))
N.append(("kSat","Tanh",{"gain":4.0}))
A.extend([("kGate","gate","kSub","trigger"),("kGate","velocity","kSub","velocity"),("kSub","out","kSubGain","in"),
  ("kOsc","out","kMix","in"),("kSubGain","out","kMix","in"),("kMix","out","kSat","in"),("kSat","out","kVca","in")])
# two toms, deep and ominous
voice("t1",41,64.0,150.0,1100.0,120.0,4000.0)
voice("t2",45,88.0,190.0,850.0,100.0,3200.0)
N.append(("tomMix","Mixer",{}))
A.extend([("t1Osc","out","t1Vca","in"),("t2Osc","out","t2Vca","in"),("t1Vca","out","tomMix","in"),("t2Vca","out","tomMix","in")])
# dungeon slam: filtered noise burst on note 39
N.append(("sGate","NoteGate",{"note":39}))
N.append(("sNoise","Noise",{"colour":0.6}))
N.append(("sEnv","Envelope",{"attack":2.0,"decay":900.0,"sustain":0.0,"release":600.0}))
N.append(("sCut","Scale",{"min":300.0,"max":6500.0}))
N.append(("sFilter","Ladder",{"resonance":0.35,"drive":3.0}))
N.append(("sVca","VCA",{}))
N.append(("sLvl","Gain",{"gain":14.0}))
N.append(("sHp","OnePole",{"mode":1,"cutoff":45.0}))
A.extend([("sGate","gate","sEnv","gate"),("sEnv","out","sCut","in"),("sCut","out","sFilter","cutoff"),
  ("sNoise","out","sFilter","in"),("sFilter","out","sHp","in"),("sHp","out","sLvl","in"),("sLvl","out","sVca","in"),("sEnv","out","sVca","cv")])
N.append(("wetMix","Mixer",{}))
A.extend([("tomMix","out","wetMix","in"),("sVca","out","wetMix","in")])
N.append(("hall","Reverb",{"size":1.7,"decay":6.0,"damping":0.65,"mix":0.5}))
A.append(("wetMix","out","hall","in"))
N.append(("master","Mixer",{}))
N.append(("trim","Gain",{"gain":-9.0}))
N.append(("tape","Tape",{"drive":9.0,"bump":5.0,"wow":0.35,"flutter":0.25,"hf":9000.0}))
N.append(("limit","Limiter",{"ceiling":-1.0}))
N.append(("out","Output",{}))
A.extend([("kVca","out","master","in"),("hall","out","master","in"),("master","out","trim","in"),("trim","out","tape","in"),
  ("tape","out","limit","in"),("limit","out","out","in")])
P=[(0,"kPScale","min","Kick tune","kick_tune",34,90,"Kick","hertz"),
   (1,"kOsc","decay","Kick decay","kick_decay",200,1200,"Kick","ms"),
   (2,"kSubGain","gain","Sub level","sub_level",-30,6,"Kick","db"),
   (3,"kSat","gain","Kick drive","kick_drive",1,15,"Kick",None),
   (4,"t1Osc","decay","Tom decay","tom_decay",300,2500,"Toms","ms"),
   (5,"hall","mix","Hall mix","hall_mix",0,1,"Hall",None),
   (6,"hall","decay","Hall decay","hall_decay",1,12,"Hall","s"),
   (7,"sCut","max","Slam brightness","slam_bright",1000,9000,"Slam","hertz"),
   (8,"tape","drive","Tape drive","tape_drive",0,24,"Master","db")]
build("halloween-drums-low","Halloween: low drums",
"""Kick, two tom drums and a noise slam for a slow, heavy dubstep half-time groove in the
manner of Bauhaus's "Bela Lugosi's Dead": the kick is a bridged-T body with a long sub
tone under it, the toms are tuned low and sent to a large dark hall, and the slam is a
burst of noise through a closing ladder filter. Everything passes through tape
saturation and a limiter.

MIDI notes: 36 kick, 39 slam, 41 low tom, 45 mid tom.""",
["Dubstep","Gothic","Percussion"],"AudioInstrument","Midi",N,A,P,"/chalet/github/valis/examples/halloween-drums-low.ttl")
