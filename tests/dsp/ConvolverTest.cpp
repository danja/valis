// tests/dsp/ConvolverTest.cpp
//
// val:Convolver. The partitioned algorithm is judged against the plain definition of
// convolution on responses that fall either side of every partition boundary, at block sizes
// that do not divide the partition, and for the property the design exists to have: the
// output starts on the same sample as the input. The responses are written to the temporary
// directory so the test depends on no shipped file.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace valis;

namespace {

/// A mono 16-bit WAV. The values are returned as the file will read them, so the reference
/// convolution uses exactly what the element does.
std::string writeWav(const std::string& name, const std::vector<std::int16_t>& samples, int rate)
{
    const auto path = (std::filesystem::temp_directory_path() / ("valis_convolver_" + name + ".wav")).string();
    std::ofstream file(path, std::ios::binary);

    const auto put32 = [&](std::uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](std::uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);

    file.write("RIFF", 4); put32(36 + bytes); file.write("WAVE", 4);
    file.write("fmt ", 4); put32(16); put16(1); put16(1); put32(static_cast<std::uint32_t>(rate));
    put32(static_cast<std::uint32_t>(rate * 2)); put16(2); put16(16);
    file.write("data", 4); put32(bytes);
    for (const auto s : samples)
        put16(static_cast<std::uint16_t>(s));
    return path;
}

/// Decaying noise, the shape of a room, of the given length.
std::vector<std::int16_t> room(std::size_t length, unsigned seed)
{
    std::mt19937 gen(seed);
    std::normal_distribution<double> noise(0.0, 1.0);
    std::vector<std::int16_t> h(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        const double envelope = std::exp(-6.0 * static_cast<double>(i) / static_cast<double>(length));
        h[i] = static_cast<std::int16_t>(std::lround(std::clamp(noise(gen) * envelope * 8000.0, -32000.0, 32000.0)));
    }
    return h;
}

std::vector<float> noiseIn(std::size_t length, unsigned seed)
{
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    std::vector<float> x(length);
    for (auto& s : x) s = dist(gen);
    return x;
}

/// The definition: y[n] = sum_j h[j] x[n - j], with h scaled to unit energy.
std::vector<double> direct(const std::vector<float>& x, const std::vector<std::int16_t>& h)
{
    double energy = 0.0;
    for (const auto s : h) energy += static_cast<double>(s) * s;
    const double scale = 1.0 / std::sqrt(energy);

    std::vector<double> y(x.size(), 0.0);
    for (std::size_t n = 0; n < x.size(); ++n)
    {
        double sum = 0.0;
        const std::size_t top = std::min(n + 1, h.size());
        for (std::size_t j = 0; j < top; ++j)
            sum += static_cast<double>(h[j]) * static_cast<double>(x[n - j]);
        y[n] = sum * scale;
    }
    return y;
}

/// The input in blocks of `block` samples, as a host would cut it.
std::vector<float> runBlocks(ElementTestFixture& rig, const std::vector<float>& x, std::size_t block)
{
    std::vector<float> out;
    for (std::size_t at = 0; at < x.size(); at += block)
    {
        const auto end = std::min(at + block, x.size());
        const auto part = rig.run(std::vector<float>(x.begin() + static_cast<std::ptrdiff_t>(at),
                                                     x.begin() + static_cast<std::ptrdiff_t>(end)));
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

double worstError(const std::vector<float>& got, const std::vector<double>& want)
{
    assert(got.size() == want.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < got.size(); ++i)
        worst = std::max(worst, std::abs(static_cast<double>(got[i]) - want[i]));
    return worst;
}

void setFile(ElementTestFixture& rig, const std::string& path)
{
    std::string error;
    const bool ok = rig.element->setOption("file", path, error);
    if (! ok) std::printf("  %s\n", error.c_str());
    assert(ok);
}

}  // namespace

int main()
{
    // -- agrees with the definition, across partition boundaries and block sizes -------------
    {
        const std::size_t lengths[] = { 1, 50, 127, 128, 129, 256, 300, 5000, 48000 };
        const std::size_t blocks[]  = { 1, 32, 100, 128, 512, 4096 };
        const auto input = noiseIn(4000, 7);

        for (const auto length : lengths)
        {
            const auto h = room(length, 11 + static_cast<unsigned>(length));
            const auto path = writeWav("room" + std::to_string(length), h, 48000);
            const auto want = direct(input, h);

            for (const auto block : blocks)
            {
                ElementTestFixture rig("Convolver");
                setFile(rig, path);
                const auto got = runBlocks(rig, input, block);
                const double err = worstError(got, want);
                std::printf("  length %5zu, block %4zu: worst error %.2e\n", length, block, err);
                assert(err < 2.0e-4);
            }
        }
    }

    // -- no latency: the first sample of the response lands on the first sample of the input -----
    {
        std::vector<std::int16_t> h(2000, 0);
        h[0]    = 16000;
        h[1000] = 8000;
        ElementTestFixture rig("Convolver");
        setFile(rig, writeWav("pair", h, 48000));

        std::vector<float> impulse(3000, 0.0f);
        impulse[0] = 1.0f;
        const auto y = runBlocks(rig, impulse, 64);

        const double scale = 1.0 / std::sqrt(16000.0 * 16000.0 + 8000.0 * 8000.0);
        assert(std::abs(y[0] - 16000.0 * scale) < 1.0e-4);
        assert(std::abs(y[1000] - 8000.0 * scale) < 1.0e-4);
        for (std::size_t i = 1; i < y.size(); ++i)
            if (i != 1000)
                assert(std::abs(y[i]) < 1.0e-4);
    }

    // -- response scaled to unit energy: loud and quiet copies of one room sound alike -----------
    {
        auto loud = room(3000, 3);
        std::vector<std::int16_t> quiet(loud.size());
        for (std::size_t i = 0; i < loud.size(); ++i)
            quiet[i] = static_cast<std::int16_t>(loud[i] / 4);

        const auto input = noiseIn(6000, 5);
        ElementTestFixture a("Convolver"), b("Convolver");
        setFile(a, writeWav("loud", loud, 48000));
        setFile(b, writeWav("quiet", quiet, 48000));
        const auto ya = runBlocks(a, input, 256);
        const auto yb = runBlocks(b, input, 256);

        const float ra = ElementTestFixture::measureRms(ya);
        const float rb = ElementTestFixture::measureRms(yb);
        std::printf("  rms loud %.4f, quiet %.4f, input %.4f\n", ra, rb, ElementTestFixture::measureRms(input));
        assert(std::abs(ra - rb) < 0.02f * ra);
        // White noise through a unit-energy response keeps its power.
        assert(std::abs(ra - ElementTestFixture::measureRms(input)) < 0.25f * ra);
    }

    // -- mix, gain, reset -----------------------------------------------------------------------
    {
        const auto h = room(1000, 9);
        const auto path = writeWav("controls", h, 48000);
        const auto input = noiseIn(2000, 13);
        const auto wet = direct(input, h);

        {
            ElementTestFixture rig("Convolver");
            setFile(rig, path);
            rig.set("mix", 0.0f);
            const auto y = runBlocks(rig, input, 100);
            for (std::size_t i = 0; i < y.size(); ++i)
                assert(std::abs(y[i] - input[i]) < 1.0e-6f);
        }
        {
            ElementTestFixture rig("Convolver");
            setFile(rig, path);
            rig.set("gain", 6.0206f);
            const auto y = runBlocks(rig, input, 100);
            std::vector<double> doubled(wet);
            for (auto& v : doubled) v *= 2.0;
            assert(worstError(y, doubled) < 4.0e-4);
        }
        {
            ElementTestFixture rig("Convolver");
            setFile(rig, path);
            const auto first = runBlocks(rig, input, 77);
            rig.reset();
            const auto second = runBlocks(rig, input, 77);
            assert(first == second);
        }
    }

    // -- no file: the input passes; a bad file is a located failure ---------------------------------
    {
        ElementTestFixture rig("Convolver");
        const auto input = noiseIn(500, 1);
        const auto y = runBlocks(rig, input, 100);
        assert(y == input);

        std::string error;
        const bool ok = rig.element->setOption("file", "/no/such/impulse.wav", error);
        assert(! ok);
        assert(error.find("no such file") != std::string::npos);
        assert(runBlocks(rig, input, 100) == input);   // the failure left it as it was

        const bool silentOk = rig.element->setOption("file",
            writeWav("silent", std::vector<std::int16_t>(500, 0), 48000), error);
        assert(silentOk);
        assert(runBlocks(rig, input, 100) == input);
    }

    // -- a response at another rate is resampled; a declared hash is checked ---------------------------
    {
        std::vector<std::int16_t> h(100, 0);
        h[0] = 20000;
        ElementTestFixture rig("Convolver");
        std::string error;
        const bool declared = rig.element->setOption("sha256", std::string(64, '0'), error);
        assert(declared);
        const bool ok = rig.element->setOption("file", writeWav("hash", h, 44100), error);
        assert(! ok);
        assert(error.find("sha256") != std::string::npos);
    }

    // -- longer than the limit: cut, so the later part of the response is not heard ------------
    {
        const int rate = 8000;
        std::vector<std::int16_t> h(static_cast<std::size_t>(rate) * 14, 0);
        h[0] = 16000;
        h[static_cast<std::size_t>(rate) * 6]  = 16000;    // inside the 12 s kept
        h[static_cast<std::size_t>(rate) * 13] = 16000;    // beyond it
        ElementTestFixture rig("Convolver", rate);
        setFile(rig, writeWav("long", h, rate));

        std::vector<float> impulse(static_cast<std::size_t>(rate) * 14, 0.0f);
        impulse[0] = 1.0f;
        const auto y = runBlocks(rig, impulse, 512);
        assert(std::abs(y[static_cast<std::size_t>(rate) * 6]) > 0.3f);
        assert(std::abs(y[static_cast<std::size_t>(rate) * 13]) < 1.0e-4f);
    }

    std::printf("ConvolverTest: ok\n");
    return 0;
}
