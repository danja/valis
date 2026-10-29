from gen import build
N=[("pitch","MidiPitch",{}),
 ("subHalf","ControlMultiply",{"b":0.5}),
 ("detune","ControlMultiply",{"b":1.006}),
 ("env","Envelope",{"attack":4.0,"decay":400.0,"sustain":0.85,"release":220.0}),
 # layer 1: sub sine one octave down
 ("sub","Oscillator",{"shape":0}),("subLvl","Gain",{"gain":-2.0}),
 # layer 2: detuned saw pair through a ladder that wobbles in time with the host
 ("sawA","Oscillator",{"shape":1}),("sawB","Oscillator",{"shape":1}),("sawMix","Mixer",{}),
 ("wob","LFO",{"shape":1,"sync":0.75}),("wobScale","Scale",{"min":1000.0,"max":1900.0}),
 ("ladder","Ladder",{"resonance":0.6,"drive":5.0}),("wobLvl","Gain",{"gain":-6.0}),
 # layer 3: harmonic wavetable growl, position swept slowly, then clipped
 ("growl","Wavetable",{"set":1}),("growlLfo","LFO",{"shape":0,"sync":4.0}),("growlScale","Scale",{"min":0.55,"max":0.4}),
 ("growlClip","Tanh",{"gain":7.0}),("growlTone","OnePole",{"mode":0,"cutoff":2400.0}),("growlLvl","Gain",{"gain":-12.0}),
 ("sum","Mixer",{}),("vca","VCA",{}),("dc","OnePole",{"mode":1,"cutoff":22.0}),("comp","Compressor",{"threshold":-14.0,"ratio":4.0}),
 ("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("pitch","out","subHalf","a"),("pitch","out","detune","a"),
 ("subHalf","out","sub","frequency"),("pitch","out","sawA","frequency"),("detune","out","sawB","frequency"),
 ("pitch","out","growl","frequency"),
 ("sub","out","subLvl","in"),("sawA","out","sawMix","in"),("sawB","out","sawMix","in"),
 ("wob","out","wobScale","in"),("wobScale","out","ladder","cutoff"),("sawMix","out","ladder","in"),("ladder","out","wobLvl","in"),
 ("growlLfo","out","growlScale","in"),("growlScale","out","growl","position"),
 ("growl","out","growlClip","in"),("growlClip","out","growlTone","in"),("growlTone","out","growlLvl","in"),
 ("subLvl","out","sum","in"),("wobLvl","out","sum","in"),("growlLvl","out","sum","in"),
 ("sum","out","vca","in"),("env","out","vca","cv"),("vca","out","dc","in"),("dc","out","comp","in"),("comp","out","limit","in"),("limit","out","out","in")]
P=[(0,"wobScale","max","Wobble top","wob_top",1200,6000,"Wobble","hertz"),
 (1,"wobScale","min","Wobble centre","wob_centre",300,2500,"Wobble","hertz"),
 (2,"ladder","resonance","Wobble resonance","wob_res",0,0.95,"Wobble",None),
 (3,"ladder","drive","Wobble drive","wob_drive",1,12,"Wobble",None),
 (4,"subLvl","gain","Sub level","sub_level",-30,6,"Mix","db"),
 (5,"wobLvl","gain","Wobble level","wob_level",-30,6,"Mix","db"),
 (6,"growlLvl","gain","Growl level","growl_level",-40,0,"Mix","db"),
 (7,"growlClip","gain","Growl drive","growl_drive",1,30,"Growl",None),
 (8,"env","release","Release","release",20,1500,"Envelope","ms"),
 (9,"env","decay","Decay","decay",20,2000,"Envelope","ms")]
build("halloween-bass","Halloween: layered bass",
"""Three bass layers on one keyboard voice, for the half-time drop of a dubstep take on
"Bela Lugosi's Dead": a sine sub an octave below the note, a pair of detuned saws through
a ladder filter whose cutoff wobbles once every dotted eighth note of the host's tempo,
and a harmonic wavetable growl whose position drifts and is then clipped.

The wobble LFO is bipolar, so the Scale maps it around its centre: the cutoff sweeps from
(2 x centre - top) up to top. Play it low; the sub is an octave below the played note.""",
["Dubstep","Bass","Gothic"],"AudioInstrument","Midi",N,A,P,"/chalet/github/valis/examples/halloween-bass.ttl")
