// tests/shot/FrontendTest.cpp
//
// The three front ends of the neural ears, which are plain arithmetic and so need no model: CLAP's ten-second padding and
// log-mel, AST's filterbank, and the byte-pair tokenizer. The tokenizer test reads the model's vocabulary and is skipped
// when scripts/shot/fetch-models.sh has not been run.

#include "valis/AstFrontend.h"
#include "valis/BpeTokenizer.h"
#include "valis/ClapFrontend.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> tone(double hz, double rate, double seconds, float amplitude = 0.5f)
{
    std::vector<float> out(static_cast<std::size_t>(rate * seconds));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = amplitude * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    return out;
}

std::size_t strongestBand(const std::vector<float>& mel, int frame, int bands)
{
    std::size_t best = 0;
    for (int m = 0; m < bands; ++m)
        if (mel[static_cast<std::size_t>(frame) * bands + static_cast<std::size_t>(m)] > mel[static_cast<std::size_t>(frame) * bands + best])
            best = static_cast<std::size_t>(m);
    return best;
}

void testClapPadding()
{
    const std::vector<float> hit{1.0f, 2.0f, 3.0f};
    const auto repeated = clapPrepare(std::vector<float>(1000, 0.5f));
    assert(repeated.size() == static_cast<std::size_t>(kClapSamples));
    assert(repeated[479999] == 0.5f);                                            // 480 whole repeats fill it
    std::vector<float> odd(70001, 1.0f);                                        // 6 repeats = 420006, the rest silence
    const auto padded = clapPrepare(odd);
    assert(padded[420005] == 1.0f && padded[420006] == 0.0f);
    assert(clapPrepare(std::vector<float>(1000, 0.5f), true)[1000] == 0.0f);     // zero pad: one hit, then silence
    assert(clapPrepare(std::vector<float>(900000, 0.25f)).size() == static_cast<std::size_t>(kClapSamples));
    assert(clapPrepare(hit).size() == static_cast<std::size_t>(kClapSamples));   // even a three-sample sound is made usable
}

void testClapLogMel()
{
    const auto silent = clapLogMel(std::vector<float>(kClapSamples, 0.0f));
    assert(silent.size() == static_cast<std::size_t>(kClapFrames) * kClapMels);
    for (float v : silent)
        assert(std::abs(v + 100.0f) < 1.0e-3f);                                  // the floor, 10 log10(1e-10)

    // Higher tones land in higher bands, and the loudness in dB follows the amplitude.
    std::size_t last = 0;
    for (double hz : {200.0, 800.0, 3000.0, 9000.0})
    {
        const auto mel = clapLogMel(tone(hz, kClapRate, 10.0));
        const auto band = strongestBand(mel, 500, kClapMels);
        std::printf("  clap: %.0f Hz peaks in mel band %zu of 64\n", hz, band);
        assert(band > last);
        last = band;
    }
    const auto quiet = clapLogMel(tone(1000.0, kClapRate, 10.0, 0.05f));
    const auto loud = clapLogMel(tone(1000.0, kClapRate, 10.0, 0.5f));
    const std::size_t band = strongestBand(loud, 500, kClapMels);
    const float difference = loud[500 * kClapMels + band] - quiet[500 * kClapMels + band];
    std::printf("  clap: ten times the amplitude reads %.2f dB more\n", difference);
    assert(std::abs(difference - 20.0f) < 0.5f);
}

void testAstFilterbank()
{
    const auto bank = astFilterbank(tone(1000.0, kAstRate, 3.0));
    assert(bank.size() == static_cast<std::size_t>(kAstFrames) * kAstMels);
    // Frames past the end of a 3 s sound are padding, all one value.
    const float pad = bank[static_cast<std::size_t>(kAstFrames - 1) * kAstMels];
    for (int m = 0; m < kAstMels; ++m)
        assert(bank[static_cast<std::size_t>(kAstFrames - 1) * kAstMels + static_cast<std::size_t>(m)] == pad);
    assert(bank[100 * kAstMels + 40] != pad || bank[100 * kAstMels + 60] != pad);

    std::size_t last = 0;
    for (double hz : {300.0, 1000.0, 3000.0})
    {
        const auto b = astFilterbank(tone(hz, kAstRate, 3.0));
        const auto band = strongestBand(b, 100, kAstMels);
        std::printf("  ast: %.0f Hz peaks in mel band %zu of 128\n", hz, band);
        assert(band > last);
        last = band;
    }
    assert(resampleLinear(tone(100.0, 48000, 1.0), 48000, 16000).size() == 16000);
}

void testTokenizer()
{
    const char* env = std::getenv("VALIS_MODELS");
    const std::string dir = env ? env : std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.local/share/valis/models";
    BpeTokenizer tokenizer;
    std::string error;
    if (! tokenizer.load(dir + "/clap/vocab.json", dir + "/clap/merges.txt", error))
    {
        std::printf("  tokenizer: skipped (%s)\n", error.c_str());
        return;
    }
    // Known RoBERTa ids: <s> Hello world </s>.
    const auto hello = tokenizer.encode("Hello world");
    assert((hello == std::vector<long long>{0, 31414, 232, 2}));
    assert(tokenizer.encode("").size() == 2);
    const auto a = tokenizer.encode("a deep thocky mechanical keyboard press");
    assert(a.front() == 0 && a.back() == 2 && a.size() > 6);
    assert(tokenizer.encode("it's  spaced") == tokenizer.encode("it's  spaced"));
    assert(tokenizer.encode("It's") != tokenizer.encode("its"));
    std::printf("  tokenizer: 'Hello world' -> %zu ids as expected\n", hello.size());
}

}  // namespace

int main()
{
    testClapPadding();
    testClapLogMel();
    testAstFilterbank();
    testTokenizer();
    std::printf("FrontendTest PASSED\n");
    return 0;
}
