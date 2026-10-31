// tests/console/RenderMidiTest.cpp
//
// valis-render --midi, run as the real program on standard MIDI files built here. The cases are the ones MISTAKES.md says
// went wrong before: a note whose time is not a multiple of the block size must still sound, at the right place.
// The tool's path is given at build time (VALIS_RENDER_EXE).

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 512;
constexpr int kDivision = 480;   // ticks per quarter note; 120 bpm makes a tick 50 samples at 48 kHz, so ticks 100, 700 ... fall off the block grid

struct Event
{
    int tick = 0;
    bool on = false;
    int note = 60;
};

void varlen(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    std::uint8_t bytes[4];
    int n = 0;
    bytes[n++] = v & 0x7f;
    while ((v >>= 7) != 0)
        bytes[n++] = static_cast<std::uint8_t>((v & 0x7f) | 0x80);
    while (n > 0)
        out.push_back(bytes[--n]);
}

/// A format 0 file with one track. `tempoMicros` > 0 adds a tempo meta event at the start.
void writeMidi(const std::string& path, std::vector<Event> events, int tempoMicros = 0)
{
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.tick < b.tick; });
    std::vector<std::uint8_t> track;
    if (tempoMicros > 0)
    {
        track.insert(track.end(), {0x00, 0xff, 0x51, 0x03, static_cast<std::uint8_t>(tempoMicros >> 16),
                                   static_cast<std::uint8_t>(tempoMicros >> 8), static_cast<std::uint8_t>(tempoMicros)});
    }
    int last = 0;
    for (const auto& e : events)
    {
        varlen(track, static_cast<std::uint32_t>(e.tick - last));
        last = e.tick;
        track.insert(track.end(), {static_cast<std::uint8_t>(e.on ? 0x90 : 0x80), static_cast<std::uint8_t>(e.note),
                                   static_cast<std::uint8_t>(e.on ? 100 : 0)});
    }
    track.insert(track.end(), {0x00, 0xff, 0x2f, 0x00});

    std::ofstream f(path, std::ios::binary);
    auto be32 = [&](std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) f.put(static_cast<char>(v >> s)); };
    auto be16 = [&](std::uint16_t v) { f.put(static_cast<char>(v >> 8)); f.put(static_cast<char>(v)); };
    f.write("MThd", 4); be32(6); be16(0); be16(1); be16(kDivision);
    f.write("MTrk", 4); be32(static_cast<std::uint32_t>(track.size()));
    f.write(reinterpret_cast<const char*>(track.data()), static_cast<std::streamsize>(track.size()));
}

/// The left channel of a 24-bit stereo wav as written by valis-render.
std::vector<float> readWav(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto at = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    std::size_t pos = 12;
    int channels = 2, bits = 24;
    while (pos + 8 <= bytes.size())
    {
        const std::string id(bytes.data() + pos, 4);
        const std::size_t size = at(pos + 4) | (at(pos + 5) << 8) | (at(pos + 6) << 16) | (static_cast<std::size_t>(at(pos + 7)) << 24);
        if (id == "fmt ")
        {
            channels = at(pos + 10) | (at(pos + 11) << 8);
            bits = at(pos + 22) | (at(pos + 23) << 8);
        }
        if (id == "data")
        {
            assert(bits == 24 && "this reader handles 24-bit files");
            std::vector<float> left;
            for (std::size_t i = pos + 8; i + 3 * static_cast<std::size_t>(channels) <= pos + 8 + size; i += 3 * static_cast<std::size_t>(channels))
            {
                int v = at(i) | (at(i + 1) << 8) | (at(i + 2) << 16);
                if (v & 0x800000)
                    v -= 0x1000000;
                left.push_back(static_cast<float>(v) / 8388608.0f);
            }
            return left;
        }
        pos += 8 + size + (size & 1);
    }
    return {};
}

double rms(const std::vector<float>& x, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to && i < static_cast<int>(x.size()); ++i)
        sum += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
    return std::sqrt(sum / std::max(1, to - from));
}

int samplesAt(int tick, double tempo = 120.0)
{
    return static_cast<int>(static_cast<double>(tick) / kDivision * 60.0 / tempo * kRate);
}

/// First sample at or after `from` whose magnitude passes `level`, or -1.
int firstAbove(const std::vector<float>& x, int from, float level)
{
    for (int i = from; i < static_cast<int>(x.size()); ++i)
        if (std::abs(x[static_cast<std::size_t>(i)]) > level)
            return i;
    return -1;
}

int run(const std::string& midi, const std::string& wav, const std::string& extra = {}, double tempo = 120.0, double seconds = 2.5)
{
    const std::string command = std::string("\"") + VALIS_RENDER_EXE + "\" \"" + VALIS_EXAMPLES_DIR + "/sh101.ttl\" --midi \"" + midi
                              + "\" --tempo " + std::to_string(tempo) + " --seconds " + std::to_string(seconds) + " " + extra
                              + " -o \"" + wav + "\" > /dev/null 2>&1";
    return std::system(command.c_str());
}

}  // namespace

int main()
{
    const auto dir = std::filesystem::temp_directory_path() / "valis-render-midi-test";
    std::filesystem::create_directories(dir);
    const auto midi = (dir / "t.mid").string(), wav = (dir / "t.wav").string();

    // Four notes, none starting on a block boundary, each 200 ticks (about 0.2 s) long.
    std::vector<Event> events;
    const int starts[] = {100, 700, 1300, 1900};
    for (int tick : starts)
    {
        events.push_back({tick, true, 60});
        events.push_back({tick + 200, false, 60});
    }
    for (int tick : starts)
        assert(samplesAt(tick) % kBlock != 0 && "the test notes must fall off the block grid");

    writeMidi(midi, events);
    assert(run(midi, wav) == 0);
    auto out = readWav(wav);
    assert(out.size() == static_cast<std::size_t>(2.5 * kRate));

    // Every note of the four sounds: a stretch of audio after each onset has level.
    for (int tick : starts)
        assert(rms(out, samplesAt(tick) + 200, samplesAt(tick) + 2200) > 0.005);

    // Each note alone, so an earlier note's release cannot hide the onset: silent before it, and the first sound within
    // one block of its time (the event fires in the block that contains it, at its own offset).
    for (int tick : starts)
    {
        writeMidi(midi, {{tick, true, 60}, {tick + 200, false, 60}});
        assert(run(midi, wav) == 0);
        const auto one = readWav(wav);
        const int onset = samplesAt(tick);
        const int heard = firstAbove(one, 0, 1.0e-4f);
        std::printf("  note at sample %d (block offset %d): first sound at %d\n", onset, onset % kBlock, heard);
        assert(heard >= onset && heard < onset + kBlock);
    }

    // A different block size puts the first note in the same place.
    writeMidi(midi, events);
    assert(run(midi, wav, "--block 64") == 0);
    const auto small = readWav(wav);
    assert(small.size() == out.size());
    assert(firstAbove(small, 0, 1.0e-4f) == firstAbove(out, 0, 1.0e-4f));

    // A note-on with no note-off sounds to the end of the render and does not stop the tool.
    writeMidi(midi, {{100, true, 60}});
    assert(run(midi, wav) == 0);
    out = readWav(wav);
    assert(rms(out, samplesAt(100) + 200, samplesAt(100) + 2200) > 0.005);
    assert(rms(out, out.size() - 4800, out.size()) > 0.005);

    // A tempo event in the file is ignored today: the note is placed by --tempo. If the tool learns to read it, this changes.
    writeMidi(midi, {{480, true, 60}, {960, false, 60}}, 1000000);   // 60 bpm in the file
    assert(run(midi, wav) == 0);
    out = readWav(wav);
    assert(rms(out, 0, samplesAt(480) - 10) < 1.0e-5);   // still one beat at 120 bpm (0.5 s), not 1 s
    assert(firstAbove(out, 0, 1.0e-4f) < samplesAt(480) + kBlock);

    // A file that is not there, and one that is not MIDI, fail with a status and no output.
    std::filesystem::remove(wav);
    assert(run((dir / "missing.mid").string(), wav) != 0);
    assert(! std::filesystem::exists(wav));
    std::ofstream((dir / "not.mid").string()) << "this is not a MIDI file";
    assert(run((dir / "not.mid").string(), wav) != 0);

    std::filesystem::remove_all(dir);
    std::printf("RenderMidiTest PASSED\n");
    return 0;
}
