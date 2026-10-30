from gen import build
N=[("pitch","MidiPitch",{}),("dc","OnePole",{"mode":1,"cutoff":35.0}),("pluckEnv","Envelope",{"attack":0.1,"decay":6.0,"sustain":0.0,"release":2.0}),
 ("noise","Noise",{"colour":0.15}),("pluckVca","VCA",{}),("pick","StateVariable",{"cutoff":3800.0,"resonance":0.9}),
 ("string","StiffString",{"feedback":0.988,"damping":0.28,"dispersion":0.015}),
 ("ampEnv","Envelope",{"attack":0.5,"decay":2800.0,"sustain":0.0,"release":450.0}),("ampVca","VCA",{}),
 ("pickup","StateVariable",{"cutoff":2600.0,"resonance":1.1}),("body","Mixer",{}),("bodyLp","OnePole",{"mode":0,"cutoff":6500.0}),
 ("amp","Tanh",{"gain":2.2}),
 ("spring","Reverb",{"size":0.35,"decay":1.6,"damping":0.2,"mix":0.3}),
 ("echo","BucketBrigade",{"time":375.0,"feedback":0.5,"bandwidth":0.28}),("wet","Mixer",{}),
 ("trem","Tape",{"drive":4.0,"bump":2.0,"wow":0.4,"flutter":0.3,"hf":9000.0}),
 ("trim","Gain",{"gain":6.0}),("limit","Limiter",{"ceiling":-1.0}),("out","Output",{})]
A=[("pitch","out","string","frequency"),("pluckEnv","out","pluckVca","cv"),("noise","out","pluckVca","in"),
 ("pluckVca","out","pick","in"),("pick","lp","string","in"),
 ("string","out","ampVca","in"),("ampEnv","out","ampVca","cv"),
 ("ampVca","out","dc","in"),("dc","out","pickup","in"),("pickup","bp","body","in"),("dc","out","body","in"),
 ("body","out","bodyLp","in"),("bodyLp","out","amp","in"),("amp","out","spring","in"),
 ("spring","out","echo","in"),("spring","out","wet","in"),("echo","out","wet","in"),
 ("wet","out","trem","in"),("trem","out","trim","in"),("trim","out","limit","in"),("limit","out","out","in")]
P=[(0,"string","feedback","Sustain","sustain",0.9,0.998,"String",None),
 (1,"string","damping","Damping","damping",0,1,"String",None),
 (2,"string","dispersion","Dispersion","dispersion",0,0.2,"String",None),
 (3,"pick","cutoff","Pick brightness","pick_bright",800,9000,"String","hertz"),
 (4,"amp","gain","Amp drive","amp_drive",1,12,"Amp",None),
 (5,"spring","mix","Spring mix","spring_mix",0,1,"Amp",None),
 (6,"echo","time","Echo time","echo_time",80,700,"Echo","ms"),
 (7,"echo","feedback","Echo feedback","echo_fb",0,1.0,"Echo",None),
 (8,"ampEnv","decay","Ring time","ring_time",300,6000,"String","ms"),
 (9,"trem","wow","Wobble","wobble",0,1,"Amp",None)]
build("halloween-guitar","Halloween: twang guitar",
"""A plucked electric guitar for the song's high, echoing figure: a short noise burst
through a state-variable low-pass excites a stiff string (a comb filter with allpass
dispersion), a bandpass stands in for the pickup, a soft clip for the amp, a small
spring-style reverb, and a bucket-brigade echo set to a dotted eighth that darkens as
it repeats. The tape stage adds a slow wobble.

The string is tuned across its range (see tests/dsp/StiffStringTest.cpp). It plays single notes only,
as the riff does. The echo time is in milliseconds; 375 ms is a dotted eighth at 120 BPM.""",
["Guitar","Surf","Gothic"],"AudioInstrument","Midi",N,A,P,"/chalet/github/valis/examples/halloween-guitar.ttl")
