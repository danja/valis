// src/ui/ToggleSwitch.cpp

#include "ui/ToggleSwitch.h"

namespace valis {

ToggleSwitch::ToggleSwitch(juce::RangedAudioParameter& p,
                           juce::String off, juce::String on)
    : parameter(p),
      attachment(p, [this](float) { repaint(); }, nullptr),
      offLabel(std::move(off)),
      onLabel(std::move(on))
{
    attachment.sendInitialUpdate();
}

void ToggleSwitch::mouseDown(const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture(
        parameter.convertFrom0to1(isOn() ? 0.0f : 1.0f));
}

void ToggleSwitch::paint(juce::Graphics& g)
{
    const bool on = isOn();

    const juce::Colour body(theme.comboBg);
    const juce::Colour edgeDark(theme.edgeDark);
    const juce::Colour edgeLight(theme.edgeLight);
    const juce::Colour dim(theme.dimText);
    const juce::Colour accent(theme.accent);

    auto area = getLocalBounds().toFloat().reduced(2.0f);

    // A bezel around a two-position rocker: the raised half is the one not
    // chosen, so the switch reads at a glance from across the room.
    g.setColour(body);
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(edgeDark);
    g.drawRoundedRectangle(area, 4.0f, 1.4f);

    const auto inner = area.reduced(3.0f);
    const auto topHalf    = inner.withHeight(inner.getHeight() * 0.5f);
    const auto bottomHalf = inner.withTrimmedTop(inner.getHeight() * 0.5f);

    // The chosen half is lit and sits proud; the other is sunk and dim.
    const auto lit    = on ? topHalf : bottomHalf;
    const auto unlit  = on ? bottomHalf : topHalf;

    g.setColour(juce::Colour(theme.knobBody));
    g.fillRoundedRectangle(unlit.reduced(1.0f), 3.0f);
    g.setColour(edgeDark.withAlpha(0.8f));
    g.drawRoundedRectangle(unlit.reduced(1.0f), 3.0f, 1.0f);

    g.setColour(accent.withAlpha(on ? 0.85f : 0.55f));
    g.fillRoundedRectangle(lit.reduced(1.0f), 3.0f);
    g.setColour(edgeLight.withAlpha(0.6f));
    g.drawRoundedRectangle(lit.reduced(1.0f), 3.0f, 1.0f);

    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));

    const auto drawLabel = [&](juce::Rectangle<float> r, const juce::String& text, bool isLit)
    {
        g.setColour(isLit ? juce::Colour(theme.panel) : dim);
        g.drawText(text.toUpperCase(), r, juce::Justification::centred, false);
    };

    drawLabel(topHalf,    onLabel,  on);
    drawLabel(bottomHalf, offLabel, ! on);
}

}  // namespace valis
