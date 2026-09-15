// src/ui/SelectorStrip.cpp

#include "ui/SelectorStrip.h"

#include <cmath>

namespace valis {

SelectorStrip::SelectorStrip(juce::RangedAudioParameter& p,
                             std::vector<std::pair<double, juce::String>> points,
                             double lo, double hi)
    : parameter(p),
      attachment(p, [this](float) { repaint(); }, nullptr),
      choices(std::move(points)),
      minimum(lo),
      maximum(hi > lo ? hi : lo + 1.0)
{
    attachment.sendInitialUpdate();
}

int SelectorStrip::selectedIndex() const
{
    if (choices.empty())
        return 0;

    const double value = minimum + static_cast<double>(parameter.getValue()) * (maximum - minimum);

    int best = 0;
    double bestDistance = std::abs(choices[0].first - value);
    for (int i = 1; i < static_cast<int>(choices.size()); ++i)
    {
        const double distance = std::abs(choices[static_cast<std::size_t>(i)].first - value);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

juce::Rectangle<float> SelectorStrip::cellBounds(int index) const
{
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    if (choices.empty())
        return area;

    // Vertical when the labels would be unreadably narrow side by side, which
    // for a knob-width column is anything past two.
    const bool stacked = choices.size() > 2;
    const float span = (stacked ? area.getHeight() : area.getWidth())
                     / static_cast<float>(choices.size());
    const auto at = static_cast<float>(index) * span;

    return stacked ? area.withY(area.getY() + at).withHeight(span)
                   : area.withX(area.getX() + at).withWidth(span);
}

void SelectorStrip::mouseDown(const juce::MouseEvent& event)
{
    for (int i = 0; i < static_cast<int>(choices.size()); ++i)
    {
        if (! cellBounds(i).contains(event.position))
            continue;

        const double value = choices[static_cast<std::size_t>(i)].first;
        const auto normalised = static_cast<float>((value - minimum) / (maximum - minimum));
        attachment.setValueAsCompleteGesture(
            parameter.convertFrom0to1(juce::jlimit(0.0f, 1.0f, normalised)));
        return;
    }
}

void SelectorStrip::paint(juce::Graphics& g)
{
    const juce::Colour edgeDark(theme.edgeDark);
    const juce::Colour edgeLight(theme.edgeLight);
    const juce::Colour dim(theme.dimText);
    const juce::Colour accent(theme.accent);

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(theme.comboBg));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(edgeDark);
    g.drawRoundedRectangle(area, 4.0f, 1.4f);

    const int selected = selectedIndex();
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));

    for (int i = 0; i < static_cast<int>(choices.size()); ++i)
    {
        const auto cell = cellBounds(i).reduced(2.0f);
        const bool lit  = i == selected;

        if (lit)
        {
            g.setColour(accent.withAlpha(0.85f));
            g.fillRoundedRectangle(cell, 3.0f);
            g.setColour(edgeLight.withAlpha(0.6f));
            g.drawRoundedRectangle(cell, 3.0f, 1.0f);
        }

        g.setColour(lit ? juce::Colour(theme.panel) : dim);
        g.drawText(choices[static_cast<std::size_t>(i)].second.toUpperCase(),
                   cell, juce::Justification::centred, false);
    }
}

}  // namespace valis
