// src/ui/ScopeBox.cpp

#include "ui/ScopeBox.h"

#include "plugin/ValisProcessor.h"

namespace valis {

namespace {
constexpr int kTitleHeight   = 18;
constexpr int kReadoutHeight = 20;
}  // namespace

ScopeBox::ScopeBox(ValisProcessor& p, std::string id, juce::String t)
    : processor(p), nodeId(std::move(id)), title(std::move(t))
{
    samples.reserve(static_cast<std::size_t>(kSamples));
}

void ScopeBox::refresh()
{
    samples.resize(static_cast<std::size_t>(kSamples));
    const int n = processor.readTap(nodeId, samples.data(), kSamples);
    samples.resize(static_cast<std::size_t>(n));

    const auto peak = processor.getControlOutput(nodeId, "peak");
    const auto rms  = processor.getControlOutput(nodeId, "rms");
    const auto freq = processor.getControlOutput(nodeId, "frequency");

    const auto fmt = [](std::optional<float> v, const char* unit, int decimals)
    {
        return v ? juce::String(*v, decimals) + unit : juce::String("--");
    };

    readout = "Peak: " + fmt(peak, "", 3) +
              "   RMS: " + fmt(rms, "", 3) +
              "   Freq: " + fmt(freq, " Hz", 0);
    repaint();
}

void ScopeBox::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const juce::Colour edgeDark(theme.edgeDark);
    const juce::Colour edgeLight(theme.edgeLight);

    // Dark meter glass, matching the old text meters.
    const auto glass = bounds.reduced(2.0f);
    g.setColour(juce::Colour(theme.meterBg));
    g.fillRoundedRectangle(glass, 4.0f);
    g.setColour(edgeDark);
    g.drawRoundedRectangle(glass, 4.0f, 1.0f);
    g.setColour(edgeLight.withAlpha(0.3f));
    g.drawLine(glass.getX() + 6.0f, glass.getY() + 1.5f,
               glass.getRight() - 6.0f, glass.getY() + 1.5f, 1.0f);

    g.setColour(juce::Colour(theme.labelText));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(title,
               glass.getX(), glass.getY() + 2.0f, glass.getWidth(),
               static_cast<float>(kTitleHeight),
               juce::Justification::centred, true);

    const auto trace = juce::Rectangle<float>(
        glass.getX() + 8.0f, glass.getY() + kTitleHeight + 4.0f,
        glass.getWidth() - 16.0f,
        glass.getHeight() - kTitleHeight - kReadoutHeight - 8.0f);

    // Graticule: centre line plus quarter divisions.
    g.setColour(juce::Colour(theme.meterText).withAlpha(0.25f));
    for (const float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        g.drawLine(trace.getX(), trace.getY() + trace.getHeight() * frac,
                   trace.getRight(), trace.getY() + trace.getHeight() * frac, 1.0f);

    if (samples.size() > 1)
    {
        juce::Path path;
        const auto n = static_cast<float>(samples.size());
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const float x = trace.getX() + trace.getWidth() * static_cast<float>(i) / (n - 1.0f);
            const float clamped = juce::jlimit(-1.0f, 1.0f, samples[i]);
            const float y = trace.getCentreY() - clamped * trace.getHeight() * 0.5f;
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.setColour(juce::Colour(theme.meterText));
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }
    else
    {
        g.setColour(juce::Colour(theme.dimText));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("no signal", trace, juce::Justification::centred, true);
    }

    g.setColour(juce::Colour(theme.meterText));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(readout,
               glass.getX(), glass.getBottom() - static_cast<float>(kReadoutHeight),
               glass.getWidth(), static_cast<float>(kReadoutHeight),
               juce::Justification::centred, true);
}

}  // namespace valis
