# scripts/halloween/mkrpp.py
# Writes a Reaper project that places each MIDI file from song.py on its own track.
# Run song.py first. Levels and pans are set afterwards through reaper-mcp.
import os
D=os.path.expanduser("~/Music/dead-at-midnight")
L=178.58
T=[("DrumsLow","drumslow"),("DrumsHigh","drumshigh"),("Bass","bass"),("Organ","organ"),
   ("Guitar","guitar"),("Theremin","theremin"),("Ornaments1","orn1"),("Ornaments2","orn2")]
o=['<REAPER_PROJECT 0.1 "7.83/linux-x86_64" 1793285344 0','  TEMPO 140 4 4','  SAMPLERATE 44100 0 0']
for i,(n,f) in enumerate(T):
    o+=[f'  <TRACK','    NAME "%s"'%n,'    VOLPAN 1 0 -1 -1 1','    TRACKID {00000000-0000-4000-8000-%012d}'%(i+1),
        '    <ITEM','      POSITION 0',f'      LENGTH {L}','      LOOP 0',f'      NAME "{f}.mid"',
        '      <SOURCE MIDI',f'        FILE "{D}/midi/{f}.mid"','      >','    >','  >']
o.append('>')
open(D+"/dead-at-midnight-midi.rpp","w").write("\n".join(o)+"\n")
