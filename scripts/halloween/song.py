import os, random, midiw
random.seed(666)
BPM=140; OUT=os.path.expanduser("~/Music/dead-at-midnight")
os.makedirs(OUT+"/midi",exist_ok=True)
S=0.25  # a sixteenth, in beats
# section list: name, start bar, bars
SEC=[("intro",0,8),("verse1",8,16),("build1",24,8),("drop1",32,16),("break",48,8),("verse2",56,16),("drop2",72,16),("outro",88,16)]
def where(bar):
    for n,s,l in SEC:
        if s<=bar<s+l: return n,bar-s,l
CH=[(52,55,59),(50,54,57),(48,52,55),(47,51,54)]     # Em D C B
ROOT=[40,38,36,35]
def chord(bar): return (bar//2)%4
perf={k:[] for k in "drumslow drumshigh bass organ guitar theremin orn1 orn2".split()}
def add(inst,bar,step,length_steps,pitch,vel):
    perf[inst].append(((bar*16+step)*S,length_steps*S,pitch,vel))
for bar in range(104):
    sec,i,l=where(bar); c=chord(bar); last=(i==l-1)
    drop=sec.startswith("drop"); verse=sec.startswith("verse")
    # ---- low drums
    if verse or drop:
        add("drumslow",bar,0,2,36,120)
        add("drumslow",bar,10,2,36,100)
        if drop: add("drumslow",bar,7,1,36,90)
        add("drumslow",bar,8,2,39,115)
        if i%4==3 and not last or (last and verse):
            for st,n in [(12,45),(13,41),(14,41),(15,45)]: add("drumslow",bar,st,1,n,95)
        if last and drop:
            for st,n in [(10,45),(11,45),(12,41),(13,41),(14,41),(15,39)]: add("drumslow",bar,st,1,n,105)
    if sec=="intro" and i>=4:
        add("drumslow",bar,0,4,41,70+i*5)
        if i%2==1: add("drumslow",bar,8,4,45,80)
    if sec=="build1":
        if i<4:
            add("drumslow",bar,0,2,36,100); add("drumslow",bar,8,2,36,100)
        else:
            for st in range(0,16,4): add("drumslow",bar,st,2,36,95+i*3)
        if i>=6:
            for st in range(0,16,2): add("drumslow",bar,st,1,39,70+st*3)
    if sec=="break" and i in (0,4): add("drumslow",bar,0,2,41,100)
    if sec=="break" and last:
        for st in (8,10,12,13,14,15): add("drumslow",bar,st,1,45 if st%2 else 41,100)
    if sec=="outro":
        if i%2==0 and i<12: add("drumslow",bar,0,2,36,110-i*4); 
        if i in (12,): add("drumslow",bar,0,4,41,80)
    # ---- high drums
    if verse or drop:
        for st in range(0,16,2 if verse else 1):
            v=70 if st%4==0 else 45
            if drop and st%2==1: v=35
            add("drumshigh",bar,st,1,42,v+random.randint(-5,8))
        add("drumshigh",bar,14,2,46,85)
        if drop: add("drumshigh",bar,6,2,46,70)
        if verse:
            for st in (3,11): add("drumshigh",bar,st,1,75,70+random.randint(0,15))
            for st in range(16): add("drumshigh",bar,st,1,70,30+(15 if st%4==0 else 0)+random.randint(0,10))
        if drop:
            add("drumshigh",bar,8,2,39,110)
            if i%2==1: add("drumshigh",bar,3,1,75,90)
    if sec in ("intro","break","outro","build1"):
        for st in range(0,16,2 if sec!="build1" else 1):
            if sec=="intro" and i<2: continue
            add("drumshigh",bar,st,1,70,25+int(i*(40 if sec=="build1" else 3))+random.randint(0,8))
        if sec in ("break",) and i%2==1: add("drumshigh",bar,11,1,75,80)
        if sec=="build1" and i>=4:
            for st in range(0,16,2): add("drumshigh",bar,st,1,42,50+i*8)
    # ---- bass
    r=ROOT[c]
    if verse and i>=4:
        add("bass",bar,0,12,r,105)
    if drop:
        add("bass",bar,0,5,r,115); add("bass",bar,6,1,r+12,110); add("bass",bar,10,2,r+7,112); add("bass",bar,12,3,r,115)
    if sec=="build1" and i>=4: add("bass",bar,0,14,r,100)
    if sec=="outro" and i<8: add("bass",bar,0,14,r,100-i*5)
    # ---- organ
    if sec in ("intro","verse1","verse2","break","outro","drop1","drop2","build1") and i%2==0 and not (sec=="intro" and i<2) :
        ln=30 if sec!="outro" or i<12 else 62
        if sec!="drop1" and sec!="drop2" or i%4==0:
            for p in CH[c]: add("organ",bar,0,32 if sec!="drop1" and sec!="drop2" else 62,p,80 if sec in("break","intro") else 65)
    # ---- guitar
    if verse or drop or sec=="outro":
        a,b,cc=[(64,67,59),(62,66,57),(60,64,55),(59,63,54)][c]
        add("guitar",bar,0,3,a,96); add("guitar",bar,6,3,b,84); add("guitar",bar,10,4,cc,90)
        if drop and i%2==1: add("guitar",bar,13,2,a,100)
    # ---- theremin: 8-beat phrases per chord pair
    if sec in ("intro","break","drop2","outro","verse2") and i%2==0 and not (sec=="intro" and i<4) and not (sec=="verse2" and i<8) and not(sec=="outro" and i>=12):
        phr=[[(71,0,12),(69,12,4),(67,16,16)],[(69,0,12),(66,12,4),(69,16,16)],[(67,0,12),(64,12,4),(67,16,16)],[(66,0,12),(63,12,4),(71,16,16)]][c]
        for p,st,ln in phr: add("theremin",bar,st,ln,p,100)
    # ---- ornaments 1: held fifth above each chord, ghost choir + bell
    if sec in ("intro","break","outro","verse1","verse2") and i%2==0 and not(sec in("verse1","verse2") and i%4!=0):
        add("orn1",bar,0,30,[59,57,55,54][c]+(12 if sec=="break" else 0),90)
    # ---- ornaments 2: stingers
    if last and sec in ("intro","verse1","build1","drop1","break","verse2","drop2"):
        add("orn2",bar,8,4,45+c,110); 
        if sec=="build1": add("orn2",bar,12,3,57,110)
    if sec=="build1" and i>=2: add("orn2",bar,4,2,40+i,90)
    if sec=="break" and i in (1,3,5): add("orn2",bar,random.choice([2,6,10]),3,random.choice([57,64,69,52]),95)
    if sec=="outro" and i in (3,9,14): add("orn2",bar,4,6,52,80)
for k,v in perf.items():
    midiw.write(f"{OUT}/midi/{k}.mid",v,BPM)
    print(k,len(v),"notes")
