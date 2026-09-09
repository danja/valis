// include/valis/UiTheme.h
//
// Cold-war test-equipment themes for the Controls tab. Pure data (ARGB,
// no JUCE dependency) so the schemes are unit-testable and the UI layer
// just converts them. Two schemes: Dark (the default) and Light.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace valis {

/// One faceplate scheme. Every field is 0xAARRGGBB.
struct EquipmentTheme
{
    const char* name;
    uint32_t panel;       ///< outer chassis
    uint32_t faceplate;   ///< knob field
    uint32_t edgeDark;    ///< engraved edges, plates
    uint32_t edgeLight;   ///< raised highlights, knurl alternate
    uint32_t labelText;   ///< silkscreen labels
    uint32_t dimText;     ///< secondary print
    uint32_t accent;      ///< pointer lines, pilot jewel, combo arrows
    uint32_t glow;        ///< halo around lit parts
    uint32_t knobBody;    ///< dial face
    uint32_t knobRim;     ///< dial edge
    uint32_t knobCap;     ///< dial centre
    uint32_t meterBg;     ///< dark meter glass, both schemes
    uint32_t meterText;   ///< meter readout ink
    uint32_t plateBg;     ///< section name plates
    uint32_t screw;       ///< chassis screws
    uint32_t screwSlot;   ///< screw slots
    uint32_t comboBg;     ///< enum switch bodies
};

inline const std::vector<EquipmentTheme>& equipmentThemes()
{
    // Dark first: it is the default. Silkscreen cream on crackle black;
    // light is hammer-tone grey with black stencil and bakelite dials.
    static const std::vector<EquipmentTheme> themes = {
        {"Dark",
         0xff191a1e, 0xff22242a, 0xff0c0d10, 0xff3a3d45,
         0xffe9dcbd, 0xff8f8875, 0xffffa63d, 0x66ffa63d,
         0xff2b2d33, 0xff101114, 0xff3a3d44,
         0xff0b0e0a, 0xff9ee06a,
         0xff141519, 0xff4a4d55, 0xff1a1b1f, 0xff141519},
        {"Light",
         0xffb3ae9c, 0xffcfc9b9, 0xff857e6e, 0xffe9e3d3,
         0xff1d1a14, 0xff5a544a, 0xffc22e1f, 0x66c22e1f,
         0xff3a3129, 0xff171310, 0xff554839,
         0xff0b0e0a, 0xff9ee06a,
         0xffa39c88, 0xff6b6558, 0xff2e2a24, 0xffb0a890},
    };
    return themes;
}

/// Index of the default scheme (Dark).
inline int defaultThemeIndex()
{
    return 0;
}

/// The scheme called `name`, or the default when unknown.
inline const EquipmentTheme& themeByName(const std::string& name)
{
    for (const auto& theme : equipmentThemes())
        if (name == theme.name)
            return theme;
    return equipmentThemes()[static_cast<std::size_t>(defaultThemeIndex())];
}

}  // namespace valis
