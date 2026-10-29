from gen import build_poly
VN=[("pitch","MidiPitch",{}),("env","Envelope",{"attack":4.0,"decay":100.0,"sustain":1.0,"release":70.0}),("bars","Mixer",{}),
    ("click","Noise",{"colour":0.2}),("clickBp","StateVariable",{"cutoff":3200.0,"resonance":1.5}),
    ("clickEnv","Envelope",{"attack":0.2,"decay":12.0,"sustain":0.0,"release":5.0}),("clickVca","VCA",{}),("clickLvl","Gain",{"gain":-14.0}),
    ("sum","Mixer",{}),("vca","VCA",{})]
VA=[("click","out","clickBp","in"),("clickBp","bp","clickVca","in"),("clickEnv","out","clickVca","cv"),("clickVca","out","clickLvl","in"),
    ("clickLvl","out","sum","in"),("bars","out","sum","in"),("sum","out","vca","in"),("env","out","vca","cv")]
VP=[]
bars=[("16",0.5,-3.0),("513",1.5,-7.0),("8",1.0,0.0),("4",2.0,-4.0),("223",3.0,-9.0),("2",4.0,-8.0)]
for n,r,g in bars:
    VN+=[("b"+n+"Ratio","ControlMultiply",{"b":r}),("b"+n+"Osc","Oscillator",{"shape":0}),("b"+n+"Gain","Gain",{"gain":g})]
    VA+=[("pitch","out","b"+n+"Ratio","a"),("b"+n+"Ratio","out","b"+n+"Osc","frequency"),("b"+n+"Osc","out","b"+n+"Gain","in"),("b"+n+"Gain","out","bars","in")]
    VP.append(("bar"+n,"Drawbar "+n+"'",g,-40,0,"b"+n+"Gain","gain","db",False))
VP.append(("keyclick","Key click",-14.0,-40,0,"clickLvl","gain","db",False))
N=[("dirt","Tanh",{"gain":2.5}),("scan","Chorus",{"rate":6.2,"depth":2.6,"mix":0.55}),("tone","OnePole",{"mode":0,"cutoff":5200.0}),
   ("room","Reverb",{"size":1.5,"decay":4.5,"damping":0.55,"mix":0.35}),("trim","Gain",{"gain":-12.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("poly","out","dirt","in"),("dirt","out","scan","in"),("scan","out","tone","in"),("tone","out","room","in"),("room","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(i,"bar"+b[0],"Drawbar "+b[0]+"'","drawbar_"+b[0],"Drawbars","db") for i,b in enumerate(bars)]+[(6,"keyclick","Key click","key_click","Tone","db")]
# shared stage params need element-targeting; add as plain params below
import gen
gen.build_poly("halloween-organ","Halloween: church organ",
"""A polyphonic drawbar organ for the song's gothic chords: eight voices, each six sine
drawbars at the Hammond footages (16', 5 1/3', 8', 4', 2 2/3', 2') with a short filtered key
click. Shared after the voices: a little overdrive, a chorus standing in for the rotating
speaker's scanner vibrato, a tone control and a large dark room.
https://en.wikipedia.org/wiki/Hammond_organ""",
["Organ","Gothic","Keys"],VN,VA,VP,("vca","out"),8,N,A,P,"/chalet/github/valis/examples/halloween-organ.ttl")
