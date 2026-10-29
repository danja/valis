import struct
PPQ=480
def vlq(n):
    b=[n&0x7f]; n>>=7
    while n: b.append((n&0x7f)|0x80); n>>=7
    return bytes(reversed(b))
def write(path, notes, bpm):
    """notes: [(start_beats, length_beats, pitch, velocity)]"""
    ev=[]
    for s,l,p,v in notes:
        ev.append((round(s*PPQ),1,p,max(1,min(127,v)))); ev.append((round((s+l)*PPQ),0,p,0))
    ev.sort(key=lambda e:(e[0],e[1]))  # offs before ons at the same tick
    tr=bytearray(); tr+=b'\x00\xff\x51\x03'+struct.pack('>I',int(60000000/bpm))[1:]
    t=0
    for tick,on,p,v in ev:
        tr+=vlq(tick-t); t=tick
        tr+=bytes([0x90 if on else 0x80,p,v if on else 0])
    tr+=b'\x00\xff\x2f\x00'
    open(path,'wb').write(b'MThd'+struct.pack('>IHHH',6,0,1,PPQ)+b'MTrk'+struct.pack('>I',len(tr))+bytes(tr))
