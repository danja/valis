// tests/ui/UiThemeTest.cpp
//
// The equipment schemes are pure data: two entries, Dark default, every
// colour opaque, and the schemes visibly distinct.

#include "valis/UiTheme.h"

#include <cassert>
#include <string>

using namespace valis;

namespace {

bool opaque(uint32_t colour)
{
    return (colour & 0xff000000u) == 0xff000000u;
}

}  // namespace

void testThemeTable()
{
    const auto& themes = equipmentThemes();
    assert(themes.size() == 2);
    assert(std::string(themes[static_cast<std::size_t>(defaultThemeIndex())].name) == "Dark");

    assert(std::string(themeByName("Light").name) == "Light");
    assert(std::string(themeByName("Dark").name) == "Dark");
    assert(std::string(themeByName("Nope").name) == "Dark");

    for (const auto& theme : themes)
    {
        assert(opaque(theme.panel));
        assert(opaque(theme.faceplate));
        assert(opaque(theme.edgeDark));
        assert(opaque(theme.edgeLight));
        assert(opaque(theme.labelText));
        assert(opaque(theme.dimText));
        assert(opaque(theme.accent));
        assert(opaque(theme.knobBody));
        assert(opaque(theme.knobRim));
        assert(opaque(theme.knobCap));
        assert(opaque(theme.meterBg));
        assert(opaque(theme.meterText));
        assert(opaque(theme.plateBg));
        assert(opaque(theme.screw));
        assert(opaque(theme.screwSlot));
        assert(opaque(theme.comboBg));
    }

    // Light and dark must actually differ everywhere but the meter glass.
    const auto& dark = themeByName("Dark");
    const auto& light = themeByName("Light");
    assert(dark.panel != light.panel);
    assert(dark.faceplate != light.faceplate);
    assert(dark.labelText != light.labelText);
    assert(dark.accent != light.accent);
    assert(dark.knobBody != light.knobBody);
    assert(dark.meterBg == light.meterBg);
}

int main()
{
    testThemeTable();
    return 0;
}
