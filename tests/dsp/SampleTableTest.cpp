// tests/dsp/SampleTableTest.cpp
#include "valis/SampleTable.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using valis::SampleTable;

namespace {

const char* kKit = R"(
# kit
file=audio/kick.ogg key=36 vel=1-79 gain=-4 tune=-0.2
file=audio/kick.ogg key=36 vel=80-127
file=audio/snare.ogg key=38 gain=-2 name="Snare drum"
# a pitched one
file=audio/tom.ogg keys=41-43 root=41 loopStart=0.1 loopEnd=0.5
)";

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

} // namespace

int main()
{
    auto table = SampleTable::parse(kKit);
    auto pads = table.pads();

    // Two kick zones are one instrument; the others are one each.
    assert(pads.size() == 3);
    assert(pads[0].keys == "36" && pads[0].zones == 2 && pads[0].name == "kick" && near(pads[0].levelDb, -4.0f));
    assert(pads[1].name == "Snare drum" && pads[1].file == "audio/snare.ogg");
    assert(pads[2].keys == "41-43" && pads[2].lowKey == 41);

    // A level change moves every layer by the same amount, so the soft layer stays 4 dB under the hard one.
    assert(table.setLevel(0, -1.0f));
    auto text = table.toString();
    assert(text.find("gain=-1 ") != std::string::npos || text.find("gain=-1\n") != std::string::npos);
    assert(text.find("gain=3") != std::string::npos);   // the hard layer, which had none, rose by the same 3 dB
    assert(near(table.pads()[0].levelDb, -1.0f));
    assert(table.setLevel(0, -4.0f) && near(table.pads()[0].levelDb, -4.0f));

    // Pan reaches every zone of the pad and clamps.
    assert(table.setPan(0, 0.4f));
    assert(table.toString().find("pan=0.4") != std::string::npos);
    assert(table.setPan(1, 5.0f) && near(table.pads()[1].pan, 1.0f));
    assert(! table.setPan(9, 0.0f) && ! table.setLevel(-1, 0.0f));

    // What the panel has no control for survives, and so do comments.
    text = table.toString();
    assert(text.find("loopStart=0.1 loopEnd=0.5") != std::string::npos);
    assert(text.find("# kit") != std::string::npos && text.find("# a pitched one") != std::string::npos);
    assert(text.find("name=\"Snare drum\"") != std::string::npos);

    // The text it writes reads back to the same instruments.
    const auto again = SampleTable::parse(text).pads();
    assert(again.size() == 3 && again[1].name == "Snare drum" && near(again[1].pan, 1.0f));

    // Rename, re-key, swap the file.
    assert(table.setName(2, "High tom") && table.pads()[2].name == "High tom");
    assert(table.setKeys(2, "45") && table.pads()[2].keys == "45");
    assert(table.setFile(0, "audio/kick2.ogg"));
    text = table.toString();
    assert(text.find("kick.ogg") == std::string::npos && text.find("kick2.ogg") != std::string::npos);

    // A new pad takes the first free key from 36 up.
    const int added = table.addPad("audio/clap.ogg", "");
    assert(table.pads()[static_cast<std::size_t>(added)].keys == "37");
    assert(table.pads()[static_cast<std::size_t>(added)].name == "clap");
    assert(table.addPad("audio/rim.ogg", "Rim", "40-44") >= 0);
    const int next = table.addPad("audio/x.ogg", "X");
    assert(table.pads()[static_cast<std::size_t>(next)].keys == "39");   // 36 kick, 37 clap, 38 snare, 39 free

    // Removing a pad takes all its zones.
    assert(table.removePad(0));
    assert(table.toString().find("kick2.ogg") == std::string::npos);
    assert(! table.removePad(99));

    // A line that is not key=value words is kept, as a comment, not dropped.
    const auto odd = SampleTable::parse("this is not a zone\nfile=a.wav key=1");
    assert(odd.pads().size() == 1);
    assert(odd.toString().find("this is not a zone") != std::string::npos);

    // ';' separates zones outside a comment and not inside one.
    assert(SampleTable::parse("# a; b\nfile=a.wav key=1; file=b.wav key=2").pads().size() == 2);

    std::puts("SampleTableTest passed");
    return 0;
}
