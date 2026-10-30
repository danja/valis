# scripts/psych/circuits.py: the instruments for "Autumn Leaves" (acid rock): a fuzz bass, a tonewheel organ,
# a rotating-speaker cabinet, a horn section and a fuzz lead. Writes into examples/.
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "halloween", "circuits"))
import gen
EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples")
def out(n): return os.path.join(EX, n)
TEMPO = 112.0
DOTTED_EIGHTH_MS = 0.75 * 60000.0 / TEMPO

# ---- fuzz bass: a saw and a square an octave down into a ladder filter, then a fuzz
N=[("pitch","MidiPitch",{}),("osc2Ratio","ControlMultiply",{"b":0.502}),
 ("osc1","Oscillator",{"shape":1}),("osc2","Oscillator",{"shape":2}),
 ("osc1Lvl","Gain",{"gain":-3.0}),("osc2Lvl","Gain",{"gain":-3.0}),("mix","Mixer",{}),
 ("filtEnv","Envelope",{"attack":1.0,"decay":220.0,"sustain":0.3,"release":90.0}),
 ("filtScale","Scale",{"min":220.0,"max":1500.0}),
 ("track","ControlMultiply",{"b":0.8}),("cutoff","ControlAdd",{}),
 ("vcf","Ladder",{"resonance":0.28,"drive":2.0}),
 ("ampEnv","Envelope",{"attack":2.0,"decay":500.0,"sustain":0.85,"release":70.0}),("vca","VCA",{}),
 ("fuzz","Tanh",{"gain":6.0}),("fuzzTone","OnePole",{"mode":0,"cutoff":2600.0}),
 ("dc","OnePole",{"mode":1,"cutoff":30.0}),("trim","Gain",{"gain":-14.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("pitch","out","osc1","frequency"),("pitch","out","osc2Ratio","a"),("osc2Ratio","out","osc2","frequency"),
 ("osc1","out","osc1Lvl","in"),("osc2","out","osc2Lvl","in"),("osc1Lvl","out","mix","in"),("osc2Lvl","out","mix","in"),
 ("filtEnv","out","filtScale","in"),("pitch","out","track","a"),("filtScale","out","cutoff","a"),("track","out","cutoff","b"),
 ("cutoff","out","vcf","cutoff"),("mix","out","vcf","in"),("vcf","out","vca","in"),("ampEnv","out","vca","cv"),
 ("vca","out","fuzz","in"),("fuzz","out","fuzzTone","in"),("fuzzTone","out","dc","in"),("dc","out","trim","in"),
 ("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"filtScale","min","Cutoff","cutoff",60,1500,"Filter","hertz"),
 (1,"filtScale","max","Envelope amount","env_amount",200,6000,"Filter","hertz"),
 (2,"vcf","resonance","Emphasis","emphasis",0,0.95,"Filter",None),
 (3,"fuzz","gain","Fuzz","fuzz",1,30,"Fuzz",None),
 (4,"fuzzTone","cutoff","Fuzz tone","fuzz_tone",600,9000,"Fuzz","hertz"),
 (5,"ampEnv","decay","Amp decay","amp_decay",20,2000,"Amplifier","ms"),
 (6,"ampEnv","release","Amp release","amp_release",5,1000,"Amplifier","ms")]
gen.build("psych-bass","Psych fuzz bass",
"""A monophonic bass for acid rock: a saw at the played pitch and a square an octave below, a
24 dB per octave ladder filter opened by an envelope, and then a fuzz (a hard tanh stage and a tone
control), which gives the pounding, slightly fizzy sound of a bass played through a fuzz pedal.
https://en.wikipedia.org/wiki/Fuzzbox
Play it from E1 to E3.""",
["Bass","Fuzz","Rock"],"AudioInstrument","Midi",N,A,P,out("psych-bass.ttl"))

# ---- tonewheel organ: nine drawbars, a decaying percussion harmonic and a key click
VN=[("pitch","MidiPitch",{}),("env","Envelope",{"attack":3.0,"decay":100.0,"sustain":1.0,"release":55.0}),("bars","Mixer",{}),
    ("click","Noise",{"colour":0.2}),("clickBp","StateVariable",{"cutoff":3200.0,"resonance":1.5}),
    ("clickEnv","Envelope",{"attack":0.2,"decay":12.0,"sustain":0.0,"release":5.0}),("clickVca","VCA",{}),("clickLvl","Gain",{"gain":-16.0}),
    ("percRatio","ControlMultiply",{"b":3.0}),("percOsc","Oscillator",{"shape":0}),
    ("percEnv","Envelope",{"attack":0.5,"decay":260.0,"sustain":0.0,"release":20.0}),("percVca","VCA",{}),("percLvl","Gain",{"gain":-5.0}),
    ("sum","Mixer",{}),("vca","VCA",{})]
VA=[("click","out","clickBp","in"),("clickBp","bp","clickVca","in"),("clickEnv","out","clickVca","cv"),("clickVca","out","clickLvl","in"),
    ("clickLvl","out","sum","in"),("bars","out","sum","in"),
    ("pitch","out","percRatio","a"),("percRatio","out","percOsc","frequency"),("percOsc","out","percVca","in"),("percEnv","out","percVca","cv"),
    ("percVca","out","percLvl","in"),("percLvl","out","sum","in"),("sum","out","vca","in"),("env","out","vca","cv")]
VP=[]
bars=[("16",0.5,0.0),("513",1.5,-40.0),("8",1.0,0.0),("4",2.0,-3.0),("223",3.0,-40.0),("2",4.0,-40.0),("135",5.0,-40.0),("113",6.0,-40.0),("1",8.0,-40.0)]
for n,r,g in bars:
    VN+=[("b"+n+"Ratio","ControlMultiply",{"b":r}),("b"+n+"Osc","Oscillator",{"shape":0}),("b"+n+"Gain","Gain",{"gain":g})]
    VA+=[("pitch","out","b"+n+"Ratio","a"),("b"+n+"Ratio","out","b"+n+"Osc","frequency"),("b"+n+"Osc","out","b"+n+"Gain","in"),("b"+n+"Gain","out","bars","in")]
    VP.append(("bar"+n,"Drawbar "+n+"'",g,-40,0,"b"+n+"Gain","gain","db",False))
VP.append(("perc","Percussion",-5.0,-40,0,"percLvl","gain","db",False))
VP.append(("keyclick","Key click",-16.0,-40,0,"clickLvl","gain","db",False))
N=[("preamp","Tanh",{"gain":2.2}),("tone","OnePole",{"mode":0,"cutoff":7500.0}),
   ("trim","Gain",{"gain":-12.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","preamp","in"),("preamp","out","tone","in"),("tone","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(i,"bar"+b[0],"Drawbar "+b[0]+"'","drawbar_"+b[0],"Drawbars","db") for i,b in enumerate(bars)]
P+=[(9,"perc","Percussion","percussion","Tone","db"),(10,"keyclick","Key click","key_click","Tone","db")]
gen.build_poly("tonewheel-organ","Tonewheel organ",
"""A polyphonic drawbar organ in the manner of the Hammond B-3 (https://en.wikipedia.org/wiki/Hammond_organ):
eight voices, each nine sine drawbars at the footages 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3' and 1',
a third-harmonic percussion that decays on each key, and a short filtered key click. The default
registration is 88 8300 000 with the percussion on. A tube preamp stage follows the voices. The
rotating speaker is a separate circuit, leslie.ttl, so that the cabinet can be played on its own.""",
["Organ","Keys","Rock"],VN,VA,VP,("vca","out"),8,N,A,P,out("tonewheel-organ.ttl"))

# ---- Leslie: a rotating speaker cabinet, with a speed switch played from a MIDI note
N=[("in","Input",{}),("speedGate","NoteGate",{"note":60}),
   ("hornSlew","Slew",{"rise":900.0,"fall":1800.0}),("drumSlew","Slew",{"rise":3500.0,"fall":5000.0}),
   ("hornRate","Scale",{"min":0.8,"max":6.7}),("drumRate","Scale",{"min":0.67,"max":5.7}),
   ("hornLfo","LFO",{"shape":0}),("drumLfo","LFO",{"shape":0}),
   ("hornAm","Scale",{"min":0.55,"max":1.0}),("drumAm","Scale",{"min":0.7,"max":1.0}),
   ("split","Crossover",{"frequency":800.0}),
   ("hornDoppler","Chorus",{"depth":0.9,"mix":1.0}),("drumDoppler","Chorus",{"depth":1.2,"mix":1.0}),
   ("hornVca","VCA",{}),("drumVca","VCA",{}),("mix","Mixer",{}),
   ("cabinet","OnePole",{"mode":0,"cutoff":6500.0}),("room","Reverb",{"size":0.7,"decay":1.2,"damping":0.6,"mix":0.12}),
   ("trim","Gain",{"gain":0.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("speedGate","gate","hornSlew","in"),("speedGate","gate","drumSlew","in"),
   ("hornSlew","out","hornRate","in"),("drumSlew","out","drumRate","in"),
   ("hornRate","out","hornLfo","rate"),("drumRate","out","drumLfo","rate"),
   ("hornRate","out","hornDoppler","rate"),("drumRate","out","drumDoppler","rate"),
   ("hornLfo","out","hornAm","in"),("drumLfo","out","drumAm","in"),
   ("hornAm","out","hornVca","cv"),("drumAm","out","drumVca","cv"),
   ("in","out","split","in"),("split","high","hornDoppler","in"),("split","low","drumDoppler","in"),
   ("hornDoppler","out","hornVca","in"),("drumDoppler","out","drumVca","in"),
   ("hornVca","out","mix","in"),("drumVca","out","mix","in"),
   ("mix","out","cabinet","in"),("cabinet","out","room","in"),("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"split","frequency","Crossover","crossover",300,2000,"Rotors","hertz"),
   (1,"hornAm","min","Horn depth","horn_depth",0.2,1.0,"Rotors",None),
   (2,"drumAm","min","Drum depth","drum_depth",0.2,1.0,"Rotors",None),
   (3,"room","mix","Room","room",0,0.5,"Cabinet",None)]
gen.build("leslie","Rotating speaker",
"""A rotating speaker cabinet for an organ, after the Leslie 122 (https://en.wikipedia.org/wiki/Leslie_speaker).
A crossover at 800 Hz splits the sound between the horn, which turns quickly, and the drum, which is
heavier and turns more slowly. Each rotor makes two effects: a tremolo, as the speaker turns toward
and away from the microphone, and a Doppler shift (a short delay swept by the same rotation, standing
in for the changing distance).
The speed switch is a MIDI note. While note 60 is held both rotors run fast ("tremolo"), 0.8 and 0.67 Hz
slow ("chorale") otherwise. The horn speeds up in about a second and slows in about two; the heavy drum takes
three and a half and five seconds, as the real rotors do. Mono in and out.""",
["Effect","Organ","Rotary"],"Effect","Audio",N,A,P,out("leslie.ttl"))

# ---- horn section: brass stabs
VN=[("pitch","MidiPitch",{}),("detune","ControlMultiply",{"b":1.0055}),("sub","ControlMultiply",{"b":0.5}),
 ("oscA","Oscillator",{"shape":1}),("oscB","Oscillator",{"shape":1}),("oscC","Oscillator",{"shape":2}),
 ("oscALvl","Gain",{"gain":-3.0}),("oscBLvl","Gain",{"gain":-3.0}),("oscCLvl","Gain",{"gain":-11.0}),("mix","Mixer",{}),
 ("filtEnv","Envelope",{"attack":14.0,"decay":150.0,"sustain":0.4,"release":70.0}),
 ("filtScale","Scale",{"min":900.0,"max":5200.0}),
 ("track","ControlMultiply",{"b":1.0}),("cutoff","ControlAdd",{}),
 ("vcf","Ladder",{"resonance":0.12,"drive":1.8}),
 ("ampEnv","Envelope",{"attack":20.0,"decay":180.0,"sustain":0.75,"release":80.0}),("vca","VCA",{})]
VA=[("pitch","out","oscA","frequency"),("pitch","out","detune","a"),("detune","out","oscB","frequency"),
 ("pitch","out","sub","a"),("sub","out","oscC","frequency"),
 ("oscA","out","oscALvl","in"),("oscB","out","oscBLvl","in"),("oscC","out","oscCLvl","in"),
 ("oscALvl","out","mix","in"),("oscBLvl","out","mix","in"),("oscCLvl","out","mix","in"),
 ("filtEnv","out","filtScale","in"),("pitch","out","track","a"),("filtScale","out","cutoff","a"),("track","out","cutoff","b"),
 ("cutoff","out","vcf","cutoff"),("mix","out","vcf","in"),("vcf","out","vca","in"),("ampEnv","out","vca","cv")]
VP=[("cutoff","Cutoff",900.0,200,4000,"filtScale","min","hertz",True),
 ("envamt","Envelope amount",5200.0,500,9000,"filtScale","max","hertz",True),
 ("attack","Attack",20.0,2,500,"ampEnv","attack","ms",True),
 ("release","Release",80.0,10,1500,"ampEnv","release","ms",True)]
N=[("grit","Tanh",{"gain":1.9}),("gritTone","OnePole",{"mode":0,"cutoff":8000.0}),
 ("chorus","Chorus",{"rate":0.6,"depth":1.2,"mix":0.25}),("room","Reverb",{"size":0.9,"decay":1.4,"damping":0.5,"mix":0.14}),
 ("trim","Gain",{"gain":-10.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","grit","in"),("grit","out","gritTone","in"),("gritTone","out","chorus","in"),("chorus","out","room","in"),("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"cutoff","Cutoff","cutoff","Filter","hertz"),(1,"envamt","Envelope amount","env_amount","Filter","hertz"),
 (2,"attack","Attack","attack","Envelope","ms"),(3,"release","Release","release","Envelope","ms")]
gen.build_poly("horn-section","Horn section",
"""A brass section for short stabs: six voices, each two slightly apart saws and a square an octave
below into a ladder filter that a fast envelope opens on the attack, which is where a trumpet or a
trombone "blats". A little soft clipping, a slow chorus (the players are never quite together) and a small room
follow the voices. This is a synthesiser's brass, not a sampled one.
https://en.wikipedia.org/wiki/Brass_section_(popular_music)""",
["Brass","Poly","Rock"],VN,VA,VP,("vca","out"),6,N,A,P,out("horn-section.ttl"))

# ---- fuzz lead: glide, a ladder filter, a fuzz, a tape echo and a room
N=[("pitch","MidiPitch",{}),("glide","Slew",{"rise":55.0,"fall":55.0}),("detune","ControlMultiply",{"b":1.006}),
 ("oscA","Oscillator",{"shape":1}),("oscB","Oscillator",{"shape":2}),
 ("oscALvl","Gain",{"gain":-3.0}),("oscBLvl","Gain",{"gain":-6.0}),("mix","Mixer",{}),
 ("filtEnv","Envelope",{"attack":6.0,"decay":260.0,"sustain":0.5,"release":160.0}),
 ("filtScale","Scale",{"min":1200.0,"max":5200.0}),
 ("track","ControlMultiply",{"b":1.0}),("cutoff","ControlAdd",{}),
 ("vcf","Ladder",{"resonance":0.4,"drive":2.5}),
 ("ampEnv","Envelope",{"attack":8.0,"decay":300.0,"sustain":0.85,"release":180.0}),("vca","VCA",{}),
 ("vib","Chorus",{"rate":5.2,"depth":0.35,"mix":0.5}),
 ("fuzz","Tanh",{"gain":16.0}),("fuzz2","AsymClip",{"drive":3.0,"posVf":0.3,"negVf":0.6}),("fuzzTone","OnePole",{"mode":0,"cutoff":4800.0}),
 ("dc","OnePole",{"mode":1,"cutoff":40.0}),("drive","Gain",{"gain":-14.0}),
 ("echo","Delay",{"time":DOTTED_EIGHTH_MS,"feedback":0.42}),("echoTone","OnePole",{"mode":0,"cutoff":3200.0}),("echoLvl","Gain",{"gain":-7.0}),
 ("sum","Mixer",{}),("room","Reverb",{"size":1.4,"decay":3.0,"damping":0.5,"mix":0.2}),
 ("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("pitch","out","glide","in"),("glide","out","oscA","frequency"),("glide","out","detune","a"),("detune","out","oscB","frequency"),
 ("oscA","out","oscALvl","in"),("oscB","out","oscBLvl","in"),("oscALvl","out","mix","in"),("oscBLvl","out","mix","in"),
 ("filtEnv","out","filtScale","in"),("glide","out","track","a"),("filtScale","out","cutoff","a"),("track","out","cutoff","b"),
 ("cutoff","out","vcf","cutoff"),("mix","out","vcf","in"),("vcf","out","vca","in"),("ampEnv","out","vca","cv"),
 ("vca","out","vib","in"),("vib","out","fuzz","in"),("fuzz","out","fuzz2","in"),("fuzz2","out","fuzzTone","in"),("fuzzTone","out","dc","in"),
 ("dc","out","drive","in"),("drive","out","sum","in"),
 ("drive","out","echo","in"),("echo","out","echoTone","in"),("echoTone","out","echoLvl","in"),("echoLvl","out","sum","in"),
 ("sum","out","room","in"),("room","out","limit","in"),("limit","out","out","in")]
P=[(0,"filtScale","min","Cutoff","cutoff",300,4000,"Filter","hertz"),
 (1,"filtScale","max","Envelope amount","env_amount",600,9000,"Filter","hertz"),
 (2,"vcf","resonance","Emphasis","emphasis",0,0.95,"Filter",None),
 (3,"fuzz","gain","Fuzz","fuzz",1,50,"Fuzz",None),
 (4,"fuzzTone","cutoff","Fuzz tone","fuzz_tone",800,9000,"Fuzz","hertz"),
 (5,"glide","rise","Glide up","glide_up",0,2000,"Pitch","ms"),
 (6,"glide","fall","Glide down","glide_down",0,2000,"Pitch","ms"),
 (7,"echo","feedback","Echo feedback","echo_feedback",0,0.9,"Echo",None),
 (8,"echoLvl","gain","Echo level","echo_level",-40,0,"Echo","db")]
gen.build("fuzz-lead","Fuzz lead",
"""A monophonic lead for call lines. A saw and a slightly sharp square go through a resonant ladder filter
with an envelope, then a light vibrato, then a heavy fuzz (a tanh stage into an asymmetric diode clipper and a tone
control). A glide (val:Slew on the pitch) slides between notes. A tape-style echo repeats at a dotted eighth note of
112 beats per minute and the sum goes through a hall. The gaps between the phrases are left to the echo and the room,
so that a guitar can answer.
https://en.wikipedia.org/wiki/Fuzzbox""",
["Lead","Fuzz","Rock"],"AudioInstrument","Midi",N,A,P,out("fuzz-lead.ttl"))
