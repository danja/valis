// src/shot/ShotMain.cpp
//
// valis-shot - listening for a caller that cannot hear. Ideas ported from earshot
// (https://github.com/Leonxlnx/earshot), which gives coding agents measurements of a sound in place of ears.
// The work is in valis/ShotReport.h, shared with the operation surface and so with MCP.
//
//   valis-shot measure file.wav        one JSON object: signal, loudness and envelope
//   valis-shot compare a.wav b.wav     how far apart two renders are, in dB, and how their loudness differs
//   valis-shot is file.wav "a trumpet"  yes, maybe or no, and what it sounds closest to (CLAP against common rivals)
//   valis-shot listen file.wav [QUERY...]  what the sound is (AudioSet labels) and how well each sentence fits it;
//                                      is and listen need -DVALIS_WITH_ONNX=ON and scripts/shot/fetch-models.sh

#include "valis/ShotReport.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int print(const valis::ShotResult& r)
{
    if (! r.ok)
    {
        std::fprintf(stderr, "valis-shot: %s\n", r.error.c_str());
        return 1;
    }
    std::printf("%s\n", r.json.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "measure" && argc == 3)
        return print(valis::shotMeasure(argv[2]));
    if (cmd == "compare" && argc == 4)
        return print(valis::shotCompare(argv[2], argv[3]));
    if (cmd == "is" && argc == 4)
        return print(valis::shotIs(argv[2], argv[3]));
    if (cmd == "listen" && argc >= 3)
        return print(valis::shotListen(argv[2], std::vector<std::string>(argv + 3, argv + argc)));
    std::fprintf(stderr, "usage: valis-shot measure FILE | compare A B | is FILE SENTENCE | listen FILE [QUERY...]\n");
    return 2;
}
