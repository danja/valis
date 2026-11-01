// tests/ops/SongMixTest.cpp
//
// The mixdown of a song: each stage alone against arithmetic done here, then the whole mix on synthetic stems whose
// answer is known, then the files (a stem that is missing is named, not guessed).

#include "valis/SongMix.h"

#include "valis/Song.h"
#include "valis/TurtleStore.h"

#include <juce_core/juce_core.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double hz, double seconds, double amplitude, double fromSeconds = 0.0, double toSeconds = 1.0e9)
{
    std::vector<float> x(static_cast<std::size_t>(seconds * kRate), 0.0f);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double>(i) / kRate;
        if (t >= fromSeconds && t < toSeconds)
            x[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * t));
    }
    return x;
}

double rmsOf(const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    to = std::min(to, x.size());
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}

double db(double x) { return 20.0 * std::log10(x + 1.0e-12); }

/// 4 bars of 2 seconds each at 120 bpm, in two sections.
Song makeSong(const std::string& extra = {})
{
    const std::string text = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix : <urn:test:mix#> .
:s a val:Song ; val:title "Mix Test" ; val:tempo 120 ; val:tailSeconds 2 ; val:roomAmount 0 ; val:drive 1 )" + extra + R"( ;
    val:hasSection :a , :b ; val:hasPart :p .
:a a val:Section ; val:index 1 ; val:bars 2 ; val:level 0 .
:b a val:Section ; val:index 2 ; val:bars 2 ; val:level -6 .
:p a val:Part ; val:circuit "x.ttl" .
)";
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> errors;
    const bool parsed = store.parse(text, "urn:test:mix", errors);
    assert(parsed);
    Song song;
    std::vector<Diagnostic> diagnostics;
    const bool read = Song::read(store, song, diagnostics);
    assert(read);
    return song;
}

MixStem stem(const std::string& name, const std::vector<float>& left, const std::vector<float>& right, double level,
             double pan = 0.0, double highpass = 0.0)
{
    MixStem s;
    s.name = name;
    s.audio.sampleRate = kRate;
    s.audio.left = left;
    s.audio.right = right;
    s.levelDb = level;
    s.pan = pan;
    s.highpassHz = highpass;
    return s;
}

void testPlayingRmsIgnoresSilence()
{
    // A tone that plays for a quarter of the time, at an RMS of 0.1, measures 0.1 and not a quarter of it.
    const auto burst = sine(440.0, 8.0, 0.1 * std::sqrt(2.0), 2.0, 4.0);
    assert(std::abs(songmix::playingRmsDb(burst, kRate) - db(0.1)) < 0.05);
    assert(songmix::playingRmsDb(std::vector<float>(48000, 0.0f), kRate) == -120.0);
    assert(songmix::playingRmsDb({}, kRate) == -120.0);
}

void testFftConvolveMatchesTheDirectSum()
{
    std::vector<float> signal(5000), impulse(700);
    for (std::size_t i = 0; i < signal.size(); ++i) signal[i] = static_cast<float>(std::sin(0.01 * i * i) + 0.1 * (i % 7));
    for (std::size_t i = 0; i < impulse.size(); ++i) impulse[i] = static_cast<float>(std::exp(-0.01 * i) * std::cos(0.3 * i));

    const auto fast = songmix::fftConvolve(signal, impulse);
    assert(fast.size() == signal.size());
    double worst = 0.0;
    for (std::size_t n = 0; n < signal.size(); ++n)
    {
        double direct = 0.0;
        for (std::size_t k = 0; k < impulse.size() && k <= n; ++k)
            direct += static_cast<double>(signal[n - k]) * impulse[k];
        worst = std::max(worst, std::abs(direct - fast[n]));
    }
    if (worst > 1.0e-3)
        std::printf("  fft convolution is %g from the direct sum\n", worst);
    assert(worst < 1.0e-3);

    // A signal longer than one block takes the overlap-add path.
    std::vector<float> longer(200000, 0.0f);
    longer[0] = 1.0f;
    longer[150000] = 0.5f;
    const auto echoes = songmix::fftConvolve(longer, impulse);
    assert(std::abs(echoes[3] - impulse[3]) < 1.0e-4);
    assert(std::abs(echoes[150003] - 0.5f * impulse[3]) < 1.0e-4);
}

void testHighpassRemovesLowAndKeepsHigh()
{
    auto low = sine(20.0, 2.0, 1.0);
    auto high = sine(4000.0, 2.0, 1.0);
    songmix::highpass(low, 200.0, kRate);
    songmix::highpass(high, 200.0, kRate);
    // two poles: 40 dB per decade, so a tone a decade below the corner is down by 40 dB
    assert(db(rmsOf(low, 48000)) < db(1.0 / std::sqrt(2.0)) - 38.0);
    assert(std::abs(db(rmsOf(high, 48000)) - db(1.0 / std::sqrt(2.0))) < 0.1);
    // the corner is 3 dB down
    auto corner = sine(200.0, 2.0, 1.0);
    songmix::highpass(corner, 200.0, kRate);
    assert(std::abs(db(rmsOf(corner, 48000)) - db(1.0 / std::sqrt(2.0)) + 3.0) < 0.3);

    auto untouched = sine(100.0, 1.0, 1.0);
    const auto copy = untouched;
    songmix::highpass(untouched, 0.0, kRate);
    assert(untouched == copy);
}

void testArcStepsBetweenSectionLevelsAndIsSmooth()
{
    const auto song = makeSong();      // 4 s at 0 dB, then 4 s at -6 dB
    const auto curve = songmix::arcCurve(song, kRate, static_cast<std::size_t>(10 * kRate));
    assert(std::abs(curve[static_cast<std::size_t>(1.0 * kRate)] - 1.0f) < 1.0e-6f);
    assert(std::abs(db(curve[static_cast<std::size_t>(6.0 * kRate)]) + 6.0) < 1.0e-3);
    assert(std::abs(db(curve[static_cast<std::size_t>(9.9 * kRate)]) + 6.0) < 1.0e-3);     // held past the end
    // halfway through the change is halfway between the two levels, and no sample jumps
    assert(std::abs(db(curve[static_cast<std::size_t>(4.0 * kRate)]) + 3.0) < 0.05);
    for (std::size_t i = 1; i < curve.size(); ++i)
        assert(std::abs(curve[i] - curve[i - 1]) < 1.0e-4f);
}

void testEachStemIsLevelledAndPanned()
{
    const auto song = makeSong();
    // Two tones at different levels, both told to sit at -20 dB, one hard left and one hard right.
    std::vector<MixStem> stems;
    stems.push_back(stem("quiet", sine(300.0, 10.0, 0.01), sine(300.0, 10.0, 0.01), -20.0, -1.0));
    stems.push_back(stem("loud", sine(1100.0, 10.0, 0.5), sine(1100.0, 10.0, 0.5), -20.0, 1.0));
    MixOptions options;
    MixReport report;
    std::string error;
    const bool mixed = mixStems(song, stems, options, report, error);
    assert(mixed);

    assert(report.stems.size() == 2);
    assert(std::abs(report.stems[0].measuredDb - db(0.01 / std::sqrt(2.0))) < 0.1);
    assert(std::abs(report.stems[1].measuredDb - db(0.5 / std::sqrt(2.0))) < 0.1);
    for (const auto& r : report.stems)
        assert(std::abs(r.measuredDb + r.gainDb + 20.0) < 1.0e-9);        // the gain is what takes it to its level

    // Hard left puts the quiet one in the left ear only; hard right, the loud one in the right only. The mix is
    // brought to -1 dBFS at the peak, so compare the two sides' levels, which were both set to -20 dB.
    const auto& m = report.mix;
    const double left = rmsOf(m.left, 24000, 240000), right = rmsOf(m.right, 24000, 240000);
    assert(std::abs(db(left) - db(right)) < 0.3);

    // Centre is constant power: a mono stem split to both ears is 3 dB down in each.
    std::vector<MixStem> centred;
    centred.push_back(stem("c", sine(500.0, 10.0, 0.3), sine(500.0, 10.0, 0.3), -20.0, 0.0));
    MixReport centreReport;
    assert(mixStems(song, centred, options, centreReport, error));
    const double both = rmsOf(centreReport.mix.left, 24000, 240000);
    assert(std::abs(both - rmsOf(centreReport.mix.right, 24000, 240000)) < 1.0e-6);
    std::vector<MixStem> hard;
    hard.push_back(stem("c", sine(500.0, 10.0, 0.3), sine(500.0, 10.0, 0.3), -20.0, -1.0));
    MixReport hardReport;
    assert(mixStems(song, hard, options, hardReport, error));
    assert(rmsOf(hardReport.mix.right) < 1.0e-6);        // nothing in the far ear
    assert(rmsOf(hardReport.mix.left, 24000, 240000) > 0.1);
}

void testASparseStemIsLevelledByWhenItPlays()
{
    const auto song = makeSong();
    // The same tone, steady and for a quarter of the time, both told to sit at the same level: the sparse one is
    // given the same gain on the stretch it plays, so the two end up equally loud while they sound.
    std::vector<MixStem> stems;
    stems.push_back(stem("steady", sine(400.0, 10.0, 0.2), sine(400.0, 10.0, 0.2), -20.0, -1.0));
    stems.push_back(stem("sparse", sine(900.0, 10.0, 0.2, 2.0, 4.0), sine(900.0, 10.0, 0.2, 2.0, 4.0), -20.0, 1.0));
    MixReport report;
    std::string error;
    assert(mixStems(song, stems, MixOptions{}, report, error));
    const double steady = rmsOf(report.mix.left, 24000, 96000);
    const double sparse = rmsOf(report.mix.right, 96000 + 1000, 192000 - 1000);
    assert(std::abs(db(steady) - db(sparse)) < 0.3);
}

void testTheSectionsLevelsShapeTheMixOnlyWithArc()
{
    const auto song = makeSong();
    const auto make = [&]
    {
        std::vector<MixStem> stems;
        stems.push_back(stem("s", sine(300.0, 10.0, 0.2), sine(300.0, 10.0, 0.2), -20.0));
        return stems;
    };
    std::string error;
    MixReport flat, shaped;
    auto a = make(), b = make();
    assert(mixStems(song, a, MixOptions{}, flat, error));
    MixOptions arc;
    arc.arc = true;
    assert(mixStems(song, b, arc, shaped, error));

    assert(flat.sections.size() == 2 && shaped.sections.size() == 2);
    assert(flat.sections[0].first == "a" && flat.sections[1].first == "b");
    assert(std::abs(flat.sections[0].second - flat.sections[1].second) < 0.2);
    // 6 dB lower in the second section, less the two-second smoothing across the join
    const double drop = shaped.sections[0].second - shaped.sections[1].second;
    assert(drop > 4.0 && drop < 6.5);
}

void testTheMixEndsInAFadeAndIsTrimmed()
{
    const auto song = makeSong();       // 8 s of bars, 2 s tail; the mix keeps 8 s + 4 s
    std::vector<MixStem> stems;
    stems.push_back(stem("s", sine(300.0, 20.0, 0.2), sine(300.0, 20.0, 0.2), -20.0));
    MixReport report;
    std::string error;
    assert(mixStems(song, stems, MixOptions{}, report, error));
    assert(report.mix.frames() == static_cast<std::size_t>(12 * kRate));
    assert(rmsOf(report.mix.left, report.mix.frames() - 48) < 0.01 * rmsOf(report.mix.left, 24000, 48000 * 5));
    assert(rmsOf(report.mix.left, 24000, 48000 * 5) > 0.1);
}

void testThePeakIsMinusOneAndTheLoudnessTargetIsMet()
{
    const auto song = makeSong();
    const auto make = [&]
    {
        std::vector<MixStem> stems;
        stems.push_back(stem("s", sine(300.0, 20.0, 0.2), sine(300.0, 20.0, 0.2), -20.0));
        return stems;
    };
    std::string error;
    MixReport untargeted, targeted;
    auto a = make(), b = make();
    assert(mixStems(song, a, MixOptions{}, untargeted, error));
    float peak = 0.0f;
    for (const float v : untargeted.mix.left) peak = std::max(peak, std::abs(v));
    assert(std::abs(db(peak) + 1.0) < 0.01);
    assert(! untargeted.targetLufs);

    MixOptions aim;
    aim.loudness = -23.0;
    assert(mixStems(song, b, aim, targeted, error));
    assert(targeted.targetLufs && *targeted.targetLufs == -23.0);
    if (! targeted.limitedByPeak)
        assert(std::abs(targeted.loudness.integratedLufs + 23.0) < 0.1);
    assert(targeted.loudness.truePeakDbtp <= -0.9);

    // Aimed louder than the peak ceiling allows, the ceiling wins.
    MixReport loud;
    auto c = make();
    aim.loudness = -0.5;
    assert(mixStems(song, c, aim, loud, error));
    assert(loud.limitedByPeak && loud.loudness.truePeakDbtp <= -0.9 && loud.loudness.integratedLufs < -0.5);
}

void testTheRoomAddsATailAndTheSameRoomEveryTime()
{
    auto song = makeSong();
    song.roomAmount = 1.0;
    const auto make = [&]
    {
        std::vector<MixStem> stems;
        // a click, then nothing
        std::vector<float> click(static_cast<std::size_t>(20 * kRate), 0.0f);
        click[24000] = 0.9f;
        click[24001] = -0.9f;
        std::vector<float> body = sine(300.0, 20.0, 0.2, 0.0, 0.2);
        for (std::size_t i = 0; i < body.size(); ++i) click[i] += body[i];
        stems.push_back(stem("s", click, click, -20.0));
        return stems;
    };
    std::string error;
    MixReport first, second;
    auto a = make(), b = make();
    assert(mixStems(song, a, MixOptions{}, first, error));
    assert(mixStems(song, b, MixOptions{}, second, error));
    assert(first.mix.left == second.mix.left);                 // deterministic

    auto dry = song;
    dry.roomAmount = 0.0;
    MixReport dryReport;
    auto c = make();
    assert(mixStems(dry, c, MixOptions{}, dryReport, error));
    // 0.3 s after the click, the dry mix is silent and the roomy one still rings
    const std::size_t after = 24000 + static_cast<std::size_t>(0.3 * kRate);
    assert(rmsOf(dryReport.mix.left, after, after + 4800) < 1.0e-4);
    assert(rmsOf(first.mix.left, after, after + 4800) > 5.0e-4);
}

void testBadInputsAreRefused()
{
    const auto song = makeSong();
    std::vector<MixStem> none;
    MixReport report;
    std::string error;
    assert(! mixStems(song, none, MixOptions{}, report, error) && ! error.empty());

    std::vector<MixStem> disagree;
    disagree.push_back(stem("a", sine(300.0, 1.0, 0.2), sine(300.0, 1.0, 0.2), -20.0));
    disagree.push_back(stem("b", sine(300.0, 1.0, 0.2), sine(300.0, 1.0, 0.2), -20.0));
    disagree.back().audio.sampleRate = 44100.0;
    error.clear();
    assert(! mixStems(song, disagree, MixOptions{}, report, error));
    assert(error.find("b is at") != std::string::npos);
}

void testFilesAMissingStemIsNamedAndTheMixGoesAhead()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("valis-songmix-test");
    directory.deleteRecursively();
    directory.createDirectory();
    const auto wavs = directory.getChildFile("wavs").getFullPathName().toStdString();

    const std::string text = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix : <urn:test:files#> .
:s a val:Song ; val:title "Files" ; val:tempo 120 ; val:tailSeconds 1 ; val:roomAmount 0 ;
    val:hasSection :a ; val:hasPart :here , :gone .
:a a val:Section ; val:index 1 ; val:bars 2 .
:here a val:Part ; val:index 1 ; val:circuit "x.ttl" ; val:level -18 .
:gone a val:Part ; val:index 2 ; val:circuit "y.ttl" .
)";
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> errors;
    assert(store.parse(text, "urn:test:files", errors));
    Song song;
    std::vector<Diagnostic> diagnostics;
    assert(Song::read(store, song, diagnostics));

    StereoAudio audio;
    audio.sampleRate = kRate;
    audio.left = sine(440.0, 6.0, 0.2);
    audio.right = audio.left;
    std::string error;
    const bool written = writeWavFile(wavs + "/here.wav", audio, error);
    assert(written);

    const auto output = directory.getChildFile("mixes").getChildFile("files-v1.wav").getFullPathName().toStdString();
    const auto result = mixSongFiles(song, wavs, output, MixOptions{});
    if (! result.ok)
        std::printf("  %s\n", result.error.c_str());
    assert(result.ok);
    assert(result.missing.size() == 1 && result.missing[0] == "gone");
    assert(juce::File(juce::String(output)).existsAsFile());

    StereoAudio back;
    const bool read = readAudioFile(output, back, error);
    assert(read);
    assert(back.sampleRate == kRate && back.frames() == static_cast<std::size_t>(8 * kRate));     // 4 s of bars and 4 s kept after them

    // nothing in the directory at all
    const auto empty = mixSongFiles(song, directory.getChildFile("nothing").getFullPathName().toStdString(), output, MixOptions{});
    assert(! empty.ok && empty.error.find("render the song first") != std::string::npos);

    directory.deleteRecursively();
}

} // namespace

int main()
{
    testPlayingRmsIgnoresSilence();
    testFftConvolveMatchesTheDirectSum();
    testHighpassRemovesLowAndKeepsHigh();
    testArcStepsBetweenSectionLevelsAndIsSmooth();
    testEachStemIsLevelledAndPanned();
    testASparseStemIsLevelledByWhenItPlays();
    testTheSectionsLevelsShapeTheMixOnlyWithArc();
    testTheMixEndsInAFadeAndIsTrimmed();
    testThePeakIsMinusOneAndTheLoudnessTargetIsMet();
    testTheRoomAddsATailAndTheSameRoomEveryTime();
    testBadInputsAreRefused();
    testFilesAMissingStemIsNamedAndTheMixGoesAhead();
    std::printf("SongMixTest: passed\n");
    return 0;
}
