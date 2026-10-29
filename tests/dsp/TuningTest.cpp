// tests/dsp/TuningTest.cpp
//
// Scala tuning files, and the two elements that read them: val:MidiPitch turns
// a note into a frequency, val:Quantise snaps a pitch to the scale.

#include "dsp/Tuning.h"
#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace valis;

namespace {

bool near(double a, double b, double tolerance = 1.0e-6) { return std::abs(a - b) < tolerance; }

void testParsesRatiosCentsAndComments()
{
    dsp::Tuning t;
    std::string error;
    const bool ok = t.parse("! example.scl\n!\nA test scale\n 3\n!\n 9/8\n 400.0 cents\n 2\n", error);
    assert(ok);
    assert(t.size() == 3);
    assert(t.description() == "A test scale");
    assert(near(t.centsOfDegree(1), 1200.0 * std::log2(9.0 / 8.0)));
    assert(near(t.centsOfDegree(2), 400.0));
    assert(near(t.centsOfDegree(3), 1200.0));
    // Laps add the period, below the root as well as above.
    assert(near(t.centsOfDegree(4), 1200.0 + t.centsOfDegree(1)));
    assert(near(t.centsOfDegree(-1), t.centsOfDegree(2) - 1200.0));
}

void testRejectsBadFiles()
{
    const char* bad[] = {
        "",                                       // nothing
        "only a description\n",                   // no count
        "d\nthree\n",                             // count is not a number
        "d\n0\n",                                 // no notes
        "d\n3\n 100.0\n 200.0\n",                 // promised three, gave two
        "d\n2\n 3/0\n 2/1\n",                     // zero denominator
        "d\n2\n abc\n 2/1\n",                     // not a pitch
        "d\n2\n 700.0\n 300.0\n",                 // falls
        "d\n1\n -100.0\n",                        // period below the root
        "d\n1000\n 2/1\n",                        // far too many
    };
    for (const char* text : bad)
    {
        dsp::Tuning t;
        std::string error;
        const bool ok = t.parse(text, error);
        assert(! ok);
        assert(! error.empty());
        assert(t.empty());
    }

    dsp::Tuning missing;
    std::string error;
    assert(! missing.load("tunings/does-not-exist.scl", error));
    assert(error.find("not found") != std::string::npos);
}

void testShippedFilesLoad()
{
    dsp::Tuning just, edo31;
    std::string error;
    assert(just.load("tunings/just-5limit.scl", error));
    assert(just.size() == 7);
    assert(near(just.centsOfDegree(4), 1200.0 * std::log2(1.5)));

    assert(edo31.load("tunings/31edo.scl", error));
    assert(edo31.size() == 31);
    assert(near(edo31.centsOfDegree(31), 1200.0, 1.0e-3));
}

void testEmptyTuningIsTwelveTone()
{
    const dsp::Tuning t;
    assert(near(t.frequency(69, 69, 440.0), 440.0));
    assert(near(t.frequency(81, 69, 440.0), 880.0, 1.0e-9));
    assert(near(t.frequency(60, 69, 440.0), 261.6255653, 1.0e-6));
}

void testMidiPitchFollowsReferenceAndTuning()
{
    {
        ElementTestFixture rig("MidiPitch");
        rig.setNote(69);
        rig.run({});
        assert(near(rig.lastControlOut[0], 440.0, 1.0e-3));

        rig.set("reference", 432.0f);
        rig.run({});
        assert(near(rig.lastControlOut[0], 432.0, 1.0e-3));

        rig.set("reference", 600.0f);   // clamped to the stated range
        rig.run({});
        assert(near(rig.lastControlOut[0], 480.0, 1.0e-3));
    }

    {
        // Just intonation from middle C: keys count scale degrees, not semitones.
        ElementTestFixture rig("MidiPitch");
        std::string error;
        const bool ok = rig.element->setOption("tuning", "tunings/just-5limit.scl", error);
        assert(ok);
        rig.set("root", 60.0f);

        rig.setNote(60);
        rig.run({});
        const double c = rig.lastControlOut[0];
        assert(near(c, 261.6255653, 1.0e-2));

        rig.setNote(64);   // four keys up is the fourth degree, 3/2, not an E
        rig.run({});
        assert(near(rig.lastControlOut[0] / c, 3.0 / 2.0, 1.0e-5));

        rig.setNote(67);   // seven notes up is the octave
        rig.run({});
        assert(near(rig.lastControlOut[0] / c, 2.0, 1.0e-5));

        rig.setNote(59);   // one key below the root is the top degree an octave down
        rig.run({});
        assert(near(rig.lastControlOut[0] / c, 15.0 / 16.0, 1.0e-5));
    }

    {
        ElementTestFixture rig("MidiPitch");
        std::string error;
        assert(! rig.element->setOption("tuning", "tunings/does-not-exist.scl", error));
        assert(! error.empty());
        assert(rig.element->setOption("something-else", "x", error));   // unknown keys are not failures
    }
}

void testQuantiseCustomScale()
{
    ElementTestFixture rig("Quantise");
    rig.set("scale", 7.0f);

    // No tuning yet: Custom leaves the value alone.
    rig.set("in", 3.3f);
    rig.run({});
    assert(near(rig.lastControlOut[0], 3.3, 1.0e-5));

    std::string error;
    const bool ok = rig.element->setOption("tuning", "tunings/31edo.scl", error);
    assert(ok);

    const double step = 12.0 / 31.0;
    rig.set("in", 3.3f);
    rig.run({});
    assert(near(rig.lastControlOut[0], std::round(3.3 / step) * step, 1.0e-4));

    // A root moves the scale with it.
    rig.set("root", 2.0f);
    rig.set("in", 5.0f);
    rig.run({});
    assert(near(rig.lastControlOut[0], 2.0 + std::round(3.0 / step) * step, 1.0e-4));

    // The built-in scales are as they were.
    rig.set("root", 0.0f);
    rig.set("scale", 4.0f);
    rig.set("in", 1.4f);
    rig.run({});
    assert(near(rig.lastControlOut[0], 2.0));
}

}  // namespace

int main()
{
    testParsesRatiosCentsAndComments();
    testRejectsBadFiles();
    testShippedFilesLoad();
    testEmptyTuningIsTwelveTone();
    testMidiPitchFollowsReferenceAndTuning();
    testQuantiseCustomScale();

    std::printf("TuningTest PASSED\n");
    return 0;
}
