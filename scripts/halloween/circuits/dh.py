from gen import build
N=[]; A=[]
def hit(p, note, src, att, dec, vcaIn=None):
    """src: node feeding the VCA (audio)."""
    N.append((p+"Gate","NoteGate",{"note":note}))
    N.append((p+"Env","Envelope",{"attack":att,"decay":dec,"sustain":0.0,"release":dec*0.5}))
    N.append((p+"Vca","VCA",{}))
    A.extend([(p+"Gate","gate",p+"Env","gate"),(p+"Env","out",p+"Vca","cv"),(src[0],src[1],p+"Vca","in")])
# closed hat
N.append(("chNoise","Noise",{"colour":0.0})); N.append(("chHp","OnePole",{"mode":1,"cutoff":8500.0}))
A.append(("chNoise","out","chHp","in")); hit("ch",42,("chHp","out"),0.3,45.0)
# open hat through a frequency shifter
N.append(("ohNoise","Noise",{"colour":0.0})); N.append(("ohHp","OnePole",{"mode":1,"cutoff":5200.0}))
A.append(("ohNoise","out","ohHp","in")); hit("oh",46,("ohHp","out"),0.5,480.0)
N.append(("ohShift","FreqShifter",{"shift":43.0,"feedback":0.55,"mix":0.45}))
A.append(("ohVca","out","ohShift","in"))
# shaker, swelling, bandpassed
N.append(("shNoise","Noise",{"colour":0.25})); N.append(("shBp","StateVariable",{"cutoff":6800.0,"resonance":2.2}))
A.append(("shNoise","out","shBp","in")); hit("sh",70,("shBp","bp"),30.0,110.0)
# wood block ticks: bridged-T at high pitch, amp envelope straight to the VCA
N.append(("wdGate","NoteGate",{"note":75})); N.append(("wdOsc","TwinTBridge",{"frequency":1850.0,"decay":75.0}))
N.append(("wdEnv","Envelope",{"attack":0.5,"decay":400.0,"sustain":0.0,"release":300.0})); N.append(("wdVca","VCA",{}))
A.extend([("wdGate","gate","wdOsc","trigger"),("wdGate","velocity","wdOsc","velocity"),("wdGate","gate","wdEnv","gate"),
  ("wdOsc","out","wdVca","in"),("wdEnv","out","wdVca","cv")])
# clap into a hall
N.append(("clNoise","Noise",{"colour":0.1})); N.append(("clBp","StateVariable",{"cutoff":1500.0,"resonance":1.6}))
A.append(("clNoise","out","clBp","in")); hit("cl",39,("clBp","bp"),1.0,190.0)
N.append(("clLvl","Gain",{"gain":5.0})); A.extend([("clVca","out","clLvl","in")])
# buses
N.append(("echoMix","Mixer",{})); N.append(("echo","BucketBrigade",{"time":375.0,"feedback":0.55,"bandwidth":0.3}))
A.extend([("shVca","out","echoMix","in"),("wdVca","out","echoMix","in"),("echoMix","out","echo","in")])
N.append(("hall","Reverb",{"size":1.4,"decay":3.2,"damping":0.35,"mix":1.0}))
A.append(("clLvl","out","hall","in"))
N.append(("master","Mixer",{}))
A.extend([("chVca","out","master","in"),("ohVca","out","master","in"),("ohShift","out","master","in"),
  ("echoMix","out","master","in"),("echo","out","master","in"),("hall","out","master","in")])
# remove the plain open hat path (only shifted path + a little dry): drop the direct ohVca arc to master
A.remove(("ohVca","out","master","in"))
N.append(("trim","Gain",{"gain":-4.0})); N.append(("limit","Limiter",{"ceiling":-1.0})); N.append(("out","Output",{}))
A.extend([("master","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")])
P=[(0,"chHp","cutoff","Closed hat tone","ch_tone",4000,12000,"Hats","hertz"),
   (1,"chEnv","decay","Closed hat decay","ch_decay",10,150,"Hats","ms"),
   (2,"ohEnv","decay","Open hat decay","oh_decay",100,1200,"Hats","ms"),
   (3,"ohShift","shift","Open hat shift","oh_shift",-200,200,"Hats","hertz"),
   (4,"ohShift","feedback","Open hat feedback","oh_fb",0,0.9,"Hats",None),
   (5,"shBp","cutoff","Shaker tone","sh_tone",3000,10000,"Shaker","hertz"),
   (6,"echo","time","Echo time","echo_time",80,700,"Echo","ms"),
   (7,"echo","feedback","Echo feedback","echo_fb",0,1.0,"Echo",None),
   (8,"hall","decay","Clap hall decay","hall_decay",0.5,8,"Clap","s"),
   (9,"wdOsc","frequency","Block pitch","wd_pitch",800,3500,"Block","hertz")]
build("halloween-drums-high","Halloween: high drums",
"""Hi-hats and small percussion with a different treatment on each voice, for the top of
a slow dubstep groove: a dry closed hat, an open hat run through a frequency shifter
with feedback so it rings inharmonically, a swelling bandpassed shaker and a wood block
into a darkening tape-style echo, and a clap into a hall.

MIDI notes: 39 clap, 42 closed hat, 46 open hat, 70 shaker, 75 wood block.""",
["Dubstep","Gothic","Percussion"],"AudioInstrument","Midi",N,A,P,"/chalet/github/valis/examples/halloween-drums-high.ttl")
