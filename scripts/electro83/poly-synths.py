# scripts/electro83/poly-synths.py: the Prophet-5 line, the Emulator choir and the Quadra strings.
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
import gen
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples")
def out(n): return os.path.join(EX, n)

# ---- Prophet-5: two oscillators, a 24 dB filter with an envelope and keyboard tracking
VN=[("pitch","MidiPitch",{}),("detune","ControlMultiply",{"b":1.0045}),
 ("oscA","Oscillator",{"shape":1}),("oscB","Oscillator",{"shape":2}),
 ("oscALvl","Gain",{"gain":-4.0}),("oscBLvl","Gain",{"gain":-7.0}),("mix","Mixer",{}),
 ("filtEnv","Envelope",{"attack":3.0,"decay":380.0,"sustain":0.35,"release":260.0}),
 ("filtScale","Scale",{"min":700.0,"max":5200.0}),
 ("track","ControlMultiply",{"b":1.0}),("cutoff","ControlAdd",{}),
 ("vcf","Ladder",{"resonance":0.22,"drive":1.6}),
 ("ampEnv","Envelope",{"attack":3.0,"decay":300.0,"sustain":0.75,"release":320.0}),("vca","VCA",{})]
VA=[("pitch","out","oscA","frequency"),("pitch","out","detune","a"),("detune","out","oscB","frequency"),
 ("oscA","out","oscALvl","in"),("oscB","out","oscBLvl","in"),("oscALvl","out","mix","in"),("oscBLvl","out","mix","in"),
 ("filtEnv","out","filtScale","in"),("pitch","out","track","a"),("filtScale","out","cutoff","a"),("track","out","cutoff","b"),
 ("cutoff","out","vcf","cutoff"),("mix","out","vcf","in"),("vcf","out","vca","in"),("ampEnv","out","vca","cv")]
VP=[("cutoff","Cutoff",700.0,100,4000,"filtScale","min","hertz",True),
 ("envamt","Envelope amount",5200.0,300,9000,"filtScale","max","hertz",True),
 ("resonance","Resonance",0.22,0,0.9,"vcf","resonance",None,False),
 ("attack","Attack",3.0,0.5,2000,"ampEnv","attack","ms",True),
 ("release","Release",320.0,10,3000,"ampEnv","release","ms",True),
 ("detune","Oscillator B detune",1.0045,0.98,1.02,"detune","b",None,False)]
N=[("grit","Tanh",{"gain":2.4}),("gritTone","OnePole",{"mode":0,"cutoff":7000.0}),
 ("chorus","Chorus",{"rate":0.45,"depth":1.8,"mix":0.3}),("room","Reverb",{"size":0.8,"decay":1.8,"damping":0.5,"mix":0.16}),
 ("trim","Gain",{"gain":-9.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","grit","in"),("grit","out","gritTone","in"),("gritTone","out","chorus","in"),("chorus","out","room","in"),("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"cutoff","Cutoff","cutoff","Filter","hertz"),(1,"envamt","Envelope amount","env_amount","Filter","hertz"),
 (2,"resonance","Resonance","resonance","Filter",None),(3,"attack","Attack","attack","Envelope","ms"),
 (4,"release","Release","release","Envelope","ms"),(5,"detune","Oscillator B detune","detune","Oscillators",None)]
gen.build_poly("prophet-5","Prophet-5 style poly synth",
"""An eight-voice analogue polyphonic synthesiser in the manner of the Sequential Circuits Prophet-5
(https://en.wikipedia.org/wiki/Prophet-5), the instrument behind the synthesiser line on New Order's
1983 single "Blue Monday". Each voice has a saw and a slightly sharp square oscillator, a 24 dB
per octave low-pass filter with its own envelope and keyboard tracking, and an amplifier envelope.
After the voices come a soft-clip distortion (a little, for edge), a light chorus and a small room.""",
["Synth","Poly","Electro"],VN,VA,VP,("vca","out"),8,N,A,P,out("prophet-5.ttl"))

# ---- Emulator choir: a vowel, bit-reduced the way an 8-bit sampler holds it
VN=[("pitch","MidiPitch",{}),("detune","ControlMultiply",{"b":1.007}),
 ("saw1","Oscillator",{"shape":1}),("saw2","Oscillator",{"shape":1}),("breath","Noise",{"colour":0.1}),
 ("breathLvl","Gain",{"gain":-26.0}),("src","Mixer",{}),
 ("f1","StateVariable",{"cutoff":700.0,"resonance":3.5}),("f2","StateVariable",{"cutoff":1220.0,"resonance":3.5}),
 ("f3","StateVariable",{"cutoff":2600.0,"resonance":3.0}),
 ("f1Lvl","Gain",{"gain":0.0}),("f2Lvl","Gain",{"gain":-5.0}),("f3Lvl","Gain",{"gain":-12.0}),("vowel","Mixer",{}),
 ("bits","Redux",{"bits":8.0,"downsample":2.0,"jitter":0.0}),
 ("env","Envelope",{"attack":220.0,"decay":400.0,"sustain":0.85,"release":520.0}),("vca","VCA",{})]
VA=[("pitch","out","saw1","frequency"),("pitch","out","detune","a"),("detune","out","saw2","frequency"),
 ("saw1","out","src","in"),("saw2","out","src","in"),("breath","out","breathLvl","in"),("breathLvl","out","src","in"),
 ("src","out","f1","in"),("src","out","f2","in"),("src","out","f3","in"),
 ("f1","bp","f1Lvl","in"),("f2","bp","f2Lvl","in"),("f3","bp","f3Lvl","in"),
 ("f1Lvl","out","vowel","in"),("f2Lvl","out","vowel","in"),("f3Lvl","out","vowel","in"),
 ("vowel","out","bits","in"),("bits","out","vca","in"),("env","out","vca","cv")]
VP=[("attack","Attack",220.0,5,3000,"env","attack","ms",True),("release","Release",520.0,20,4000,"env","release","ms",True),
 ("bits","Bits",8.0,4,16,"bits","bits",None,False),("holdrate","Sample-rate divide",2.0,1,8,"bits","downsample",None,False),
 ("f1","Formant 1",700.0,300,1000,"f1","cutoff","hertz",True),("f2","Formant 2",1220.0,700,2500,"f2","cutoff","hertz",True)]
N=[("tape","Tape",{"drive":2.0,"bump":0.0,"wow":0.45,"flutter":0.35,"hf":7000.0,"hiss":-70.0}),
 ("chorus","Chorus",{"rate":0.35,"depth":2.2,"mix":0.4}),("hall","Reverb",{"size":1.4,"decay":3.2,"damping":0.55,"mix":0.28}),
 ("trim","Gain",{"gain":-12.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","tape","in"),("tape","out","chorus","in"),("chorus","out","hall","in"),("hall","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"attack","Attack","attack","Envelope","ms"),(1,"release","Release","release","Envelope","ms"),
 (2,"bits","Bits","bits","Sampler",None),(3,"holdrate","Sample-rate divide","rate_divide","Sampler",None),
 (4,"f1","Formant 1","formant_1","Vowel","hertz"),(5,"f2","Formant 2","formant_2","Vowel","hertz")]
gen.build_poly("emulator-choir","Emulator choir",
"""A choir pad in the manner of a voice sampled into an early sampler, the E-mu Emulator
(https://en.wikipedia.org/wiki/E-mu_Emulator), which held a couple of seconds of 8-bit sound and
looped it. The band said their choir sounded like a Mellotron, so it is a vowel and a little breath,
held by a bit-reducer, then a slow tape wobble and a chorus.

The voice is two detuned saws through three formant filters at the frequencies of an "ah". The
formants stay put as the note changes, as they do in a sample played up and down a keyboard. Four voices.
No recording is used.""",
["Choir","Pad","Sampler"],VN,VA,VP,("vca","out"),4,N,A,P,out("emulator-choir.ttl"))

# ---- Quadra: a string machine, three saws and an ensemble
VN=[("pitch","MidiPitch",{}),("up","ControlMultiply",{"b":1.0065}),("down","ControlMultiply",{"b":0.9935}),
 ("sawA","Oscillator",{"shape":1}),("sawB","Oscillator",{"shape":1}),("sawC","Oscillator",{"shape":1}),("sum","Mixer",{}),
 ("tone","OnePole",{"mode":0,"cutoff":4200.0}),("thin","OnePole",{"mode":1,"cutoff":180.0}),
 ("env","Envelope",{"attack":260.0,"decay":300.0,"sustain":1.0,"release":520.0}),("vca","VCA",{})]
VA=[("pitch","out","sawA","frequency"),("pitch","out","up","a"),("up","out","sawB","frequency"),("pitch","out","down","a"),("down","out","sawC","frequency"),
 ("sawA","out","sum","in"),("sawB","out","sum","in"),("sawC","out","sum","in"),
 ("sum","out","tone","in"),("tone","out","thin","in"),("thin","out","vca","in"),("env","out","vca","cv")]
VP=[("attack","Attack",260.0,5,3000,"env","attack","ms",True),("release","Release",520.0,20,4000,"env","release","ms",True),
 ("brightness","Brightness",4200.0,600,12000,"tone","cutoff","hertz",True)]
N=[("ens1","Chorus",{"rate":0.55,"depth":3.0,"mix":0.5}),("ens2","Chorus",{"rate":0.83,"depth":2.2,"mix":0.5}),
 ("swirl","Phaser",{"rate":0.18,"depth":0.7,"feedback":0.25,"mix":0.3}),
 ("room","Reverb",{"size":1.1,"decay":2.2,"damping":0.5,"mix":0.2}),
 ("trim","Gain",{"gain":-10.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","ens1","in"),("ens1","out","ens2","in"),("ens2","out","swirl","in"),("swirl","out","room","in"),("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"attack","Attack","attack","Envelope","ms"),(1,"release","Release","release","Envelope","ms"),
 (2,"brightness","Brightness","brightness","Tone","hertz"),
]
gen.build_poly("quadra-strings","Quadra strings",
"""A string-machine pad in the manner of the ARP Quadra
(https://en.wikipedia.org/wiki/ARP_Quadra), whose string section the engineer remembers on
New Order's 1983 single "Blue Monday". Each voice is three saws a little apart, softened by a
low-pass, with a slow attack. Two choruses at different rates and a slow phaser make the
ensemble that string machines are known for. Eight voices.""",
["Strings","Pad","Ensemble"],VN,VA,VP,("vca","out"),8,N,A,P,out("quadra-strings.ttl"))
