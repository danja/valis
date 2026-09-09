// src/ui/EquipmentLookAndFeel.cpp

#include "ui/EquipmentLookAndFeel.h"

namespace valis {

void EquipmentLookAndFeel::setScheme(const EquipmentTheme& theme)
{
    tickInk    = juce::Colour(theme.dimText);
    dialFace   = juce::Colour(theme.knobBody);
    dialRim    = juce::Colour(theme.knobRim);
    dialCap    = juce::Colour(theme.knobCap);
    knurlLight = juce::Colour(theme.edgeLight);
    knurlDark  = juce::Colour(theme.edgeDark);
    pointer    = juce::Colour(theme.accent);
    halo       = juce::Colour(theme.glow);
}

void EquipmentLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPosProportional,
                                            float rotaryStartAngle, float rotaryEndAngle,
                                            juce::Slider&)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                               static_cast<float>(width), static_cast<float>(height));
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 8.0f;
    if (radius <= 0.0f)
        return;

    // Silkscreen tick arc.
    g.setColour(tickInk);
    constexpr int kTicks = 11;
    for (int i = 0; i < kTicks; ++i)
    {
        const float angle = rotaryStartAngle +
            (rotaryEndAngle - rotaryStartAngle) * (static_cast<float>(i) / (kTicks - 1));
        const juce::Line<float> tick(centre.getPointOnCircumference(radius + 6.0f, angle),
                                     centre.getPointOnCircumference(radius + 2.0f, angle));
        g.drawLine(tick, i % 5 == 0 ? 2.0f : 1.0f);
    }

    // Drop shadow, then the dial body.
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(centre.x - radius + 1.0f, centre.y - radius + 3.0f,
                  radius * 2.0f, radius * 2.0f);
    g.setColour(dialRim);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(dialFace);
    g.fillEllipse(centre.x - radius + 3.0f, centre.y - radius + 3.0f,
                  (radius - 3.0f) * 2.0f, (radius - 3.0f) * 2.0f);

    // Knurling: alternating light/dark nicks around the rim.
    constexpr int kKnurl = 28;
    for (int i = 0; i < kKnurl; ++i)
    {
        const float angle = juce::MathConstants<float>::twoPi * (static_cast<float>(i) / kKnurl);
        g.setColour(i % 2 == 0 ? knurlLight : knurlDark);
        g.drawLine(juce::Line<float>(centre.getPointOnCircumference(radius - 0.5f, angle),
                                     centre.getPointOnCircumference(radius - 3.5f, angle)),
                   1.5f);
    }

    // Bevel highlight, top-left.
    juce::Path bevel;
    bevel.addArc(centre.x - radius + 5.0f, centre.y - radius + 5.0f,
                 (radius - 5.0f) * 2.0f, (radius - 5.0f) * 2.0f,
                 juce::MathConstants<float>::pi * 0.9f,
                 juce::MathConstants<float>::pi * 1.6f, true);
    g.setColour(knurlLight.withAlpha(0.5f));
    g.strokePath(bevel, juce::PathStrokeType(2.5f));

    // Pointer with a halo, then the centre cap.
    const float toAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    g.setColour(halo);
    g.drawLine(juce::Line<float>(centre.getPointOnCircumference(radius * 0.15f, toAngle),
                                 centre.getPointOnCircumference(radius * 0.85f, toAngle)),
               6.0f);
    g.setColour(pointer);
    g.drawLine(juce::Line<float>(centre.getPointOnCircumference(radius * 0.15f, toAngle),
                                 centre.getPointOnCircumference(radius * 0.85f, toAngle)),
               2.5f);
    g.setColour(dialCap);
    g.fillEllipse(centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);
}

}  // namespace valis
