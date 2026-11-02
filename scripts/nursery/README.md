# Nursery instruments

`nursery.py` writes eight circuits to `examples/nursery-*.ttl`: a kick, a bass, a lead, an arpeggio voice, a pad, a snare, a hi-hat and a riser. They are meant for a 140 bpm dance track whose tunes come from nursery rhymes. Run it from anywhere:

    python3 scripts/nursery/nursery.py

Edit the script and run it again, not the Turtle files. `tests/engine/NurseryInstrumentsTest.cpp` checks that each is silent until played, in range, tuned where it is pitched, quiet after release, and does its particular thing (the kick falls, a hard hat rings longer than a soft one, the riser builds).

Timings that depend on the tempo (the arp echo at 321 ms, the pad's 1.1667 Hz rocking, the riser's 5 second build) are set for 140 bpm and are controls.
