// src/ui/SpectrumBox.cpp

#include "ui/SpectrumBox.h"

#include "plugin/ValisProcessor.h"

#include <cmath>

namespace valis {

namespace {
constexpr int kTitleHeight   = 18;
constexpr int kReadoutHeight = 20;
constexpr float kLowEdgeHz   = 20.0f;
constexpr float kHighEdgeHz  = 20000.0f;
}  // namespace

SpectrumBox::SpectrumBox(ValisProcessor& p, std::string id, juce::String t)
    : processor(p), nodeId(std::move(id)), title(std::move(t)),
      samples(static_cast<std::size_t>(kFftSize)),
      fftWork(static_cast<std::size_t>(kFftSize) * 2, 0.0f),
      levels(static_cast<std::size_t>(kBands), 0.0f)
{
}

void SpectrumBox::refresh()
{
    const int n = processor.readTap(nodeId, samples.data(), kFftSize);

    if (n >= kFftSize / 4)
    {
        // Hann window over the newest samples, zero-padded to the FFT size.
        for (int i = 0; i < kFftSize; ++i)
        {
            const int src = i - (kFftSize - n);
            const float s = src >= 0 ? samples[static_cast<std::size_t>(src)] : 0.0f;
            const float w = 0.5f * (1.0f - std::cos(6.283185307179586f * i / (kFftSize - 1)));
            fftWork[static_cast<std::size_t>(i)] = s * w;
        }

        fft.performFrequencyOnlyForwardTransform(fftWork.data());

        // Peak magnitude per log-spaced band, normalised so a full-scale sine
        // reads 0 dB. performFrequencyOnlyForwardTransform leaves magnitudes
        // scaled by fftSize / 2.
        const double rate = processor.engineSampleRate();
        const float norm = static_cast<float>(kFftSize) * 0.5f;
        std::vector<float> peaks(static_cast<std::size_t>(kBands), 0.0f);
        for (int b = 0; b < kFftSize / 2; ++b)
        {
            const float hz = static_cast<float>(b) * static_cast<float>(rate) / kFftSize;
            if (hz < kLowEdgeHz || hz > kHighEdgeHz)
                continue;
            const float frac = std::log(hz / kLowEdgeHz) / std::log(kHighEdgeHz / kLowEdgeHz);
            const int band = std::min(kBands - 1, static_cast<int>(frac * kBands));
            peaks[static_cast<std::size_t>(band)] =
                std::max(peaks[static_cast<std::size_t>(band)],
                         fftWork[static_cast<std::size_t>(b)] / norm);
        }

        for (int b = 0; b < kBands; ++b)
        {
            const float db = 20.0f * std::log10(peaks[static_cast<std::size_t>(b)] + 1e-9f);
            const float target = juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / -kFloorDb);
            auto& level = levels[static_cast<std::size_t>(b)];
            level = std::max(target, level - 0.08f);  // instant attack, steady fall
        }
    }
    else
    {
        for (auto& level : levels)
            level = std::max(0.0f, level - 0.08f);
    }

    const auto low  = processor.getControlOutput(nodeId, "low");
    const auto mid  = processor.getControlOutput(nodeId, "mid");
    const auto high = processor.getControlOutput(nodeId, "high");
    const auto cent = processor.getControlOutput(nodeId, "centroid");

    const auto fmt = [](std::optional<float> v, int decimals)
    {
        return v ? juce::String(*v, decimals) : juce::String("--");
    };

    readout = "L:" + fmt(low, 2) + " M:" + fmt(mid, 2) + " H:" + fmt(high, 2) +
              " C:" + (cent ? juce::String(*cent, 0) + "Hz" : juce::String("--"));
    repaint();
}

void SpectrumBox::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const juce::Colour edgeDark(theme.edgeDark);
    const juce::Colour edgeLight(theme.edgeLight);

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

    const auto bars = juce::Rectangle<float>(
        glass.getX() + 8.0f, glass.getY() + kTitleHeight + 4.0f,
        glass.getWidth() - 16.0f,
        glass.getHeight() - kTitleHeight - kReadoutHeight - 8.0f);

    const float slot = bars.getWidth() / kBands;
    const float barW = std::max(1.0f, slot - 1.0f);
    g.setColour(juce::Colour(theme.meterText));
    for (int b = 0; b < kBands; ++b)
    {
        const float h = levels[static_cast<std::size_t>(b)] * bars.getHeight();
        if (h < 0.5f)
            continue;
        const float x = bars.getX() + b * slot + (slot - barW) * 0.5f;
        g.fillRect(x, bars.getBottom() - h, barW, h);
    }

    g.setColour(juce::Colour(theme.meterText));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(readout,
               glass.getX(), glass.getBottom() - static_cast<float>(kReadoutHeight),
               glass.getWidth(), static_cast<float>(kReadoutHeight),
               juce::Justification::centred, true);
}

}  // namespace valis
