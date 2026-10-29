// tests/ui/FileOptionTest.cpp
//
// Which elements get a file slot in the Controls view, and which option key a
// session's chosen file replaces. Header-only, so no GUI is needed.

#include "valis/FileOption.h"

#include <cassert>
#include <cstdio>

using namespace valis;

int main()
{
    // The players and the model runner have a slot; the rest of the elements do not.
    for (const char* name : { "SampleLoad", "NamModel", "TimeStretch" })
        assert(hasFileSlot(name));

    // The granulator reads a file but records into the same buffer, so a swap would not last.
    for (const char* name : { "Granulator", "Oscillator", "Reverb", "Output", "" })
        assert(! hasFileSlot(name));

    assert(fileOptionKey("NamModel") == "model");
    assert(fileOptionKey("SampleLoad") == "file");
    assert(fileOptionKey("TimeStretch") == "file");

    std::puts("FileOptionTest PASSED");
    return 0;
}
