# scripts/latin/circuits.py: the instruments for "Lights On, Nobody Home": a nylon-string guitar, a guitarron,
# a harp, trumpets, violins and Latin percussion with a small kit. Writes into examples/.
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
import gen
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples")
def out(n): return os.path.join(EX, n)

def plucked(hardness, position, string, ring):
    """Voice nodes and arcs for a plucked string: val:Pluck, a finger or plectrum on the string, strikes a stiff string."""
    VN=[("pitch","MidiPitch",{}),
        ("pick","Pluck",{"position":position,"hardness":hardness}),
        ("string","StiffString",string),
        ("ampEnv","Envelope",{"attack":0.5,"decay":ring,"sustain":0.0,"release":ring}),("ampVca","VCA",{}),
        ("dc","OnePole",{"mode":1,"cutoff":28.0})]
    VA=[("pitch","out","string","frequency"),("pitch","out","pick","frequency"),("pick","out","string","in"),
        ("string","out","ampVca","in"),("ampEnv","out","ampVca","cv"),("ampVca","out","dc","in")]
    return VN, VA

# ---- nylon guitar: the vihuela when played high and strummed, the guitar when played low and picked
VN,VA=plucked(0.45,0.17,{"feedback":0.9965,"damping":0.5,"dispersion":0.004},1800.0)
VP=[("sustain","Sustain",0.9965,0.9,0.999,"string","feedback",None,False),
    ("damping","Damping",0.5,0,1,"string","damping",None,False),
    ("pick","Pick hardness",0.45,0.0,1.0,"pick","hardness",None,False),
    ("ring","Ring time",1800.0,200,5000,"ampEnv","decay","ms",True)]
N=[("topPlate","StateVariable",{"cutoff":210.0,"resonance":2.2}),("air","StateVariable",{"cutoff":105.0,"resonance":3.0}),
   ("wood","ModalBank",{"frequency":330.0,"decay":0.12,"brightness":0.6,"mode":3}),("woodLvl","Gain",{"gain":-28.0}),
   ("body","Mixer",{}),("room","Reverb",{"size":0.7,"decay":1.1,"damping":0.5,"mix":0.12}),
   ("trim","Gain",{"gain":-8.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","topPlate","in"),("poly","out","air","in"),("poly","out","body","in"),("topPlate","bp","body","in"),("air","bp","body","in"),
   ("poly","out","wood","in"),("wood","out","woodLvl","in"),("woodLvl","out","body","in"),
   ("body","out","room","in"),("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"sustain","Sustain","sustain","String",None),(1,"damping","Damping","damping","String",None),
   (2,"pick","Pick hardness","pick_hardness","String",None),(3,"ring","Ring time","ring_time","String","ms")]
gen.build_poly("nylon-guitar","Nylon-string guitar",
"""A six-voice nylon-string guitar. In each voice a val:Pluck, a fingertip or nail a sixth of the way
along the string, strikes a stiff string (val:StiffString, tuned by its loop delay). After the voices two resonances stand in for the body: the air in
the box at about 105 Hz and the top plate at about 210 Hz, mixed back against the strings. Played in the middle of the
keyboard it is a guitar; strummed high, with the notes of a chord a few milliseconds apart, it is a vihuela, the small
bright guitar of a mariachi band.
https://en.wikipedia.org/wiki/Vihuela""",
["Guitar","Acoustic","Latin"],VN,VA,VP,("dc","out"),6,N,A,P,out("nylon-guitar.ttl"))

# ---- harp: a long ring, bright pluck
VN,VA=plucked(0.6,0.12,{"feedback":0.9985,"damping":0.3,"dispersion":0.002},4200.0)
VP=[("ring","Ring time",4200.0,500,8000,"ampEnv","decay","ms",True),
    ("pick","Pick hardness",0.6,0.0,1.0,"pick","hardness",None,False)]
N=[("soundboard","StateVariable",{"cutoff":160.0,"resonance":2.0}),("body","Mixer",{}),
   ("room","Reverb",{"size":1.2,"decay":2.4,"damping":0.45,"mix":0.2}),
   ("trim","Gain",{"gain":-9.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","soundboard","in"),("poly","out","body","in"),("soundboard","bp","body","in"),
   ("body","out","room","in"),("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"ring","Ring time","ring_time","String","ms"),(1,"pick","Pick hardness","pick_hardness","String",None)]
gen.build_poly("harp","Harp",
"""An eight-voice harp, as played in the Veracruz folk music that mariachi grew from. A bright pluck (val:Pluck) strikes
a lightly damped stiff string that rings for several seconds, with one low soundboard resonance and a room.
https://en.wikipedia.org/wiki/Harp""",
["Harp","Acoustic","Latin"],VN,VA,VP,("dc","out"),8,N,A,P,out("harp.ttl"))

# ---- guitarron: a big, round bass guitar
N=[("pitch","MidiPitch",{}),("dc","OnePole",{"mode":1,"cutoff":25.0}),
 ("pick","Pluck",{"position":0.14,"hardness":0.45}),
 ("string","StiffString",{"feedback":0.993,"damping":0.45,"dispersion":0.01}),
 ("ampEnv","Envelope",{"attack":0.5,"decay":900.0,"sustain":0.0,"release":900.0}),("ampVca","VCA",{}),
 ("box","StateVariable",{"cutoff":95.0,"resonance":2.4}),("wood","ModalBank",{"frequency":150.0,"decay":0.18,"brightness":0.5,"mode":3}),
 ("woodLvl","Gain",{"gain":-28.0}),("body","Mixer",{}),
 ("tone","OnePole",{"mode":0,"cutoff":1800.0}),
 ("trim","Gain",{"gain":-13.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("pitch","out","string","frequency"),("pitch","out","pick","frequency"),("pick","out","string","in"),
 ("string","out","ampVca","in"),("ampEnv","out","ampVca","cv"),
 ("ampVca","out","dc","in"),("dc","out","box","in"),("dc","out","body","in"),("dc","out","wood","in"),("wood","out","woodLvl","in"),("woodLvl","out","body","in"),("box","bp","body","in"),
 ("body","out","tone","in"),("tone","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"string","damping","Damping","damping",0,1,"String",None),
 (1,"pick","hardness","Pick hardness","pick_hardness",0,1,"String",None),
 (2,"ampEnv","decay","Ring time","ring_time",100,3000,"String","ms"),
 (3,"tone","cutoff","Tone","tone",500,6000,"Tone","hertz")]
gen.build("guitarron","Guitarron",
"""The guitarron, the large six-string bass of a mariachi band, which plays the root and the fifth under the
chords. A soft thumb pluck (val:Pluck) strikes a heavily damped stiff string; a low resonance stands in for its deep, round
box and a low-pass takes the edge off. Play it from E1 (28) to A2 (45).
https://en.wikipedia.org/wiki/Guitarr%C3%B3n_mexicano""",
["Bass","Acoustic","Latin"],"AudioInstrument","Midi",N,A,P,out("guitarron.ttl"))

# ---- trumpets: a lip-reed waveguide per voice (val:Brass), blown by an envelope, with a gentle vibrato
VN=[("pitch","MidiPitch",{}),("vel","MidiVelocity",{}),("velScale","Scale",{"min":0.72,"max":1.0}),
 ("vibLfo","LFO",{"rate":5.3,"shape":0}),("vibEnv","Envelope",{"attack":380.0,"decay":10.0,"sustain":1.0,"release":40.0}),
 ("vibVca","ControlMultiply",{}),("vibScale","Scale",{"min":1.0,"max":1.0032}),("vibMul","ControlMultiply",{}),
 ("env","Envelope",{"attack":38.0,"decay":140.0,"sustain":0.78,"release":70.0}),("press","ControlMultiply",{}),
 ("horn","Brass",{"lip":0.55,"breath":0.03,"damping":0.2})]
VA=[("pitch","out","vibMul","a"),("vibLfo","out","vibVca","a"),("vibEnv","out","vibVca","b"),("vibVca","out","vibScale","in"),("vibScale","out","vibMul","b"),("vibMul","out","horn","frequency"),
 ("vel","out","velScale","in"),("velScale","out","press","b"),("env","out","press","a"),("press","out","horn","pressure")]
VP=[("lip","Lip tension",0.55,0,1,"horn","lip",None,False),
 ("damping","Bore damping",0.2,0,1,"horn","damping",None,False),
 ("attack","Attack",38.0,5,400,"env","attack","ms",True),
 ("release","Release",70.0,10,1500,"env","release","ms",True)]
N=[("bell","StateVariable",{"cutoff":1250.0,"resonance":0.9}),("body","Mixer",{}),
 ("room","Reverb",{"size":1.1,"decay":1.8,"damping":0.45,"mix":0.14}),
 ("trim","Gain",{"gain":-7.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","bell","in"),("poly","out","body","in"),("bell","bp","body","in"),("body","out","room","in"),
 ("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"lip","Lip tension","lip_tension","Lips",None),(1,"damping","Bore damping","damping","Bore",None),
 (2,"attack","Attack","attack","Envelope","ms"),(3,"release","Release","release","Envelope","ms")]
gen.build_poly("trumpets","Mariachi trumpets",
"""Four voices of trumpet, played as a pair in thirds in a mariachi band. Each voice is a lip-reed waveguide
(val:Brass): the lips, the air column and the bell are modelled, so the tone gets brighter when the player blows
harder, as a real trumpet's does, instead of a filter being opened on a saw wave. Blowing pressure comes from an
envelope scaled by the note's velocity, and a slight vibrato moves the pitch. After the voices a resonance at 1.25 kHz
lifts the region where a trumpet's bell speaks, and a small room closes the sum.
https://en.wikipedia.org/wiki/Mariachi""",
["Brass","Trumpet","Latin"],VN,VA,VP,("horn","out"),4,N,A,P,out("trumpets.ttl"))

# ---- violins: a bowed string per voice (val:Bow), a violin body after the voices
VN=[("pitch","MidiPitch",{}),("vel","MidiVelocity",{}),
 ("vibLfo","LFO",{"rate":5.6,"shape":0}),("vibEnv","Envelope",{"attack":520.0,"decay":10.0,"sustain":1.0,"release":40.0}),
 ("vibVca","ControlMultiply",{}),("vibScale","Scale",{"min":1.0,"max":1.0042}),("vibMul","ControlMultiply",{}),
 ("env","Envelope",{"attack":110.0,"decay":150.0,"sustain":1.0,"release":120.0}),
 ("bowSpeed","Scale",{"min":0.0,"max":0.5}),("velScale","Scale",{"min":0.8,"max":1.1}),("speed","ControlMultiply",{}),
 ("slope","ControlMultiply",{"b":0.033}),("lift","ControlAdd",{"b":-10.9}),
 ("string","Bow",{"pressure":0.6,"position":0.09,"damping":0.35}),
 ("comp","Gain",{}),("vca","VCA",{})]
VA=[("pitch","out","vibMul","a"),("vibLfo","out","vibVca","a"),("vibEnv","out","vibVca","b"),("vibVca","out","vibScale","in"),("vibScale","out","vibMul","b"),("vibMul","out","string","frequency"),
 ("env","out","bowSpeed","in"),("vel","out","velScale","in"),("bowSpeed","out","speed","a"),("velScale","out","speed","b"),("speed","out","string","velocity"),
 ("pitch","out","slope","a"),("slope","out","lift","a"),("lift","out","comp","gain"),
 ("string","out","comp","in"),("comp","out","vca","in"),("env","out","vca","cv")]
VP=[("pressure","Bow pressure",0.6,0.2,0.9,"string","pressure",None,False),
 ("position","Bow position",0.09,0.04,0.3,"string","position",None,False),
 ("attack","Attack",110.0,5,1500,"env","attack","ms",True),
 ("release","Release",120.0,10,2500,"env","release","ms",True)]
N=[("air","ModalBank",{"frequency":280.0,"decay":0.05,"brightness":0.6}),
 ("wood","ModalBank",{"frequency":460.0,"decay":0.04,"brightness":0.7}),
 ("hill","StateVariable",{"cutoff":2600.0,"resonance":1.6}),("hillLvl","Gain",{"gain":-6.0}),
 ("body","Mixer",{}),("bodyLvl","Gain",{"gain":-26.0}),
 ("ens","Chorus",{"rate":0.5,"depth":2.2,"mix":0.4}),
 ("room","Reverb",{"size":1.2,"decay":2.0,"damping":0.5,"mix":0.18}),
 ("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","air","in"),("poly","out","wood","in"),("poly","out","hill","in"),("hill","bp","hillLvl","in"),
 ("poly","out","body","in"),("air","out","body","in"),("wood","out","body","in"),("hillLvl","out","body","in"),
 ("body","out","bodyLvl","in"),("bodyLvl","out","ens","in"),("ens","out","room","in"),("room","out","limit","in"),("limit","out","out","in")]
P=[(0,"pressure","Bow pressure","bow_pressure","Bow",None),(1,"position","Bow position","bow_position","Bow",None),
 (2,"attack","Attack","attack","Envelope","ms"),(3,"release","Release","release","Envelope","ms")]
gen.build_poly("violins","Mariachi violins",
"""A section of violins: four voices, each a bowed string (val:Bow, tuned to within about 9 cents up to
880 Hz) with bow speed from an envelope, a vibrato, and a level that rises with pitch to make up for how quickly a bowed
string loses level as it climbs. After the voices comes a violin's body: the air resonance near 280 Hz and the main wood
resonance near 460 Hz (two val:ModalBank elements), and the bridge hill, a lift near 2.6 kHz that gives the instrument its
brilliance. A slow chorus makes a section from the voices, and a small room closes the sum.
val:Bow is not yet reliable above about 900 Hz, which is the upper end of a violin's normal range.
https://en.wikipedia.org/wiki/Violin_acoustics""",
["Strings","Violin","Latin"],VN,VA,VP,("vca","out"),4,N,A,P,out("violins.ttl"))

# ---- Latin percussion and a small kit, one circuit, played by note number. Struck things are a short burst of noise
# into a val:ModalBank tuned to the modes of the object; shaken things are val:Shaker; the cowbell and crash are plates.
N=[]; A=[]
def gate(p, note, att, dec):
    """A note gate, an envelope for the strike and the velocity folded into it."""
    N.extend([(p+"Gate","NoteGate",{"note":note}),(p+"Env","Envelope",{"attack":att,"decay":dec,"sustain":0.0,"release":dec}),
              (p+"Hit","ControlMultiply",{})])
    A.extend([(p+"Gate","gate",p+"Env","gate"),(p+"Env","out",p+"Hit","a"),(p+"Gate","velocity",p+"Hit","b")])
def struck(p, note, freq, mode, decay, bright, tone, ms, lvl, color=0.0):
    """Noise through a low-pass, for the stick or the palm, strikes a modal bank."""
    gate(p, note, 0.2, ms)
    N.extend([(p+"Noise","Noise",{"colour":color}),(p+"Vca","VCA",{}),(p+"Soft","OnePole",{"mode":0,"cutoff":tone}),
              (p+"Mod","ModalBank",{"frequency":freq,"decay":decay,"brightness":bright,"mode":mode}),(p+"Lvl","Gain",{"gain":lvl-20.0})])
    A.extend([(p+"Noise","out",p+"Vca","in"),(p+"Hit","out",p+"Vca","cv"),(p+"Vca","out",p+"Soft","in"),
              (p+"Soft","out",p+"Mod","in"),(p+"Mod","out",p+"Lvl","in")])
def wires(p, note, col, cut, q, dec, lvl, kind="bp"):
    """Snare wires or cymbal: noise through a filter on its own envelope."""
    gate(p, note, 0.5, dec)
    N.extend([(p+"Noise","Noise",{"colour":col}),(p+"Filt","StateVariable",{"cutoff":cut,"resonance":q}),(p+"Vca","VCA",{}),(p+"Lvl","Gain",{"gain":lvl})])
    A.extend([(p+"Noise","out",p+"Filt","in"),(p+"Filt",kind,p+"Vca","in"),(p+"Hit","out",p+"Vca","cv"),(p+"Vca","out",p+"Lvl","in")])
def shaken(p, note, lvl, **kw):
    N.extend([(p+"Gate","NoteGate",{"note":note}),(p+"Shk","Shaker",kw),(p+"Lvl","Gain",{"gain":lvl})])
    A.extend([(p+"Gate","gate",p+"Shk","trigger"),(p+"Gate","velocity",p+"Shk","velocity"),(p+"Shk","out",p+"Lvl","in")])
shaken("mar",70,-4.0,particles=28.0,shake=75.0,sound=3.0,frequency=5200.0,resonance=2.5)            # maracas
shaken("cab",69,-8.0,particles=90.0,shake=120.0,sound=1.5,frequency=7400.0,resonance=1.8)          # cabasa beads
shaken("gus",73,14.0,particles=40.0,shake=90.0,sound=2.0,frequency=2700.0,resonance=7.0,regularity=0.92,scrape=46.0)   # guiro
shaken("gul",74,10.0,particles=40.0,shake=380.0,sound=2.0,frequency=2500.0,resonance=7.0,regularity=0.92,scrape=34.0)
struck("cgh",63,310.0,1,0.30,0.45,2200.0,6.0,11.0)          # conga, high: palm on a skin
struck("cgl",64,225.0,1,0.38,0.40,1800.0,6.0,11.0)          # conga, low
struck("slp",62,330.0,1,0.12,0.9,6500.0,2.0,-1.0)           # conga slap: sharp, bright, short
struck("bgh",60,430.0,1,0.16,0.55,3500.0,3.0,4.0)          # bongo, high
struck("bgl",61,330.0,1,0.20,0.5,3000.0,3.0,4.0)           # bongo, low
struck("clv",75,2300.0,0,0.10,0.2,9000.0,0.8,19.0)          # claves: hardwood bars
struck("snb",39,190.0,1,0.14,0.5,4500.0,3.0,6.0)           # snare shell
struck("tom",41,92.0,1,0.55,0.35,2500.0,4.0,8.0)           # floor tom
struck("kck",36,58.0,1,0.30,0.12,900.0,5.0,2.0)           # bass drum: a big head hit softly
struck("cw",56,560.0,3,0.45,0.85,5000.0,1.0,13.0)           # cowbell: a plate with its inharmonic modes
wires("snr",38,0.0,3000.0,0.9,160.0,-4.0)                  # snare wires
struck("crp",49,3100.0,3,1.6,0.9,9000.0,2.0,-6.0)           # crash: a plate
wires("crs",49,0.0,5200.0,0.7,1300.0,-10.0,"hp")
N.append(("master","Mixer",{})); N.append(("room","Reverb",{"size":0.6,"decay":0.9,"damping":0.5,"mix":0.08}))
for p in ("mar","cab","gus","gul","snr","crs","crp","slp","clv","cgh","cgl","bgh","bgl","snb","tom","kck","cw"):
    A.append((p+"Lvl","out","master","in"))
N.extend([("trim","Gain",{"gain":0.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})])
A.extend([("master","out","room","in"),("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")])
P=[(0,"cghMod","decay","High conga ring","conga_high",0.1,1.0,"Congas","seconds"),
   (1,"cglMod","decay","Low conga ring","conga_low",0.1,1.0,"Congas","seconds"),
   (2,"marShk","frequency","Maracas tone","maracas_tone",3000,9000,"Shakers","hertz"),
   (3,"cwMod","frequency","Cowbell pitch","cowbell_pitch",400,900,"Cowbell","hertz")]
gen.build("latin-perc","Latin percussion and kit",
"""Hand percussion for a Latin band and a small punk kit, played by note number from one MIDI file or several.
Notes: 36 bass drum, 38 snare, 39 snare body, 41 floor tom, 49 crash, 56 cowbell, 60 high bongo, 61 low bongo,
62 conga slap, 63 high conga, 64 low conga, 69 cabasa, 70 maracas, 73 short guiro, 74 long guiro, 75 claves.
Struck things are a brief burst of noise through a low-pass (the stick, the palm) into a val:ModalBank tuned to
the modes of the object: a circular membrane for drums, wooden bars for claves, a plate for the cowbell and crash.
The snare adds wires as filtered noise. The maracas, cabasa and guiro are val:Shaker, which models the collisions
of particles in a shell instead of filtering noise. A small room closes the sum.
https://en.wikipedia.org/wiki/Latin_percussion""",
["Percussion","Acoustic","Latin"],"AudioInstrument","Midi",N,A,P,out("latin-perc.ttl"))

# ---- vocal guide: a plain "ah" voice with a quick attack, for hearing where each syllable sits
N=[("pitch","MidiPitch",{}),("detune","ControlMultiply",{"b":1.004}),
 ("saw1","Oscillator",{"shape":1}),("saw2","Oscillator",{"shape":1}),("src","Mixer",{}),
 ("f1","StateVariable",{"cutoff":750.0,"resonance":4.0}),("f2","StateVariable",{"cutoff":1250.0,"resonance":4.0}),
 ("f3","StateVariable",{"cutoff":2700.0,"resonance":3.0}),
 ("f1Lvl","Gain",{"gain":0.0}),("f2Lvl","Gain",{"gain":-5.0}),("f3Lvl","Gain",{"gain":-12.0}),("vowel","Mixer",{}),
 ("env","Envelope",{"attack":14.0,"decay":80.0,"sustain":0.9,"release":70.0}),("vca","VCA",{}),
 ("vib","Chorus",{"rate":5.3,"depth":0.3,"mix":0.4}),
 ("trim","Gain",{"gain":-3.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("pitch","out","saw1","frequency"),("pitch","out","detune","a"),("detune","out","saw2","frequency"),
 ("saw1","out","src","in"),("saw2","out","src","in"),
 ("src","out","f1","in"),("src","out","f2","in"),("src","out","f3","in"),
 ("f1","bp","f1Lvl","in"),("f2","bp","f2Lvl","in"),("f3","bp","f3Lvl","in"),
 ("f1Lvl","out","vowel","in"),("f2Lvl","out","vowel","in"),("f3Lvl","out","vowel","in"),
 ("vowel","out","vca","in"),("env","out","vca","cv"),("vca","out","vib","in"),("vib","out","trim","in"),
 ("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"env","attack","Attack","attack",2,200,"Envelope","ms"),(1,"env","release","Release","release",10,500,"Envelope","ms")]
gen.build("vocal-guide","Vocal guide voice",
"""A plain "ah" voice for a guide track: two slightly apart saws through three formant filters at the frequencies of
an "ah" (about 750, 1250 and 2700 Hz), a quick attack so short syllables are heard, and a light vibrato. It is
meant to show a singer the notes and the rhythm, not to sound like a singer.
https://en.wikipedia.org/wiki/Formant""",
["Voice","Guide"],"AudioInstrument","Midi",N,A,P,out("vocal-guide.ttl"))
