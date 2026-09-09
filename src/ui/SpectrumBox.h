// src/ui/SpectrumBox.h
//
// Graphic spectrum analyzer for one val:FreqAnalyzer node: a view box the size
// of two knobs showing log-spaced level bars plus the low/mid/high/centroid
// readout. The FFT runs here on the message thread over the engine's waveform
// tap, so the audio thread only performs a bounded copy per block.
// Owned by ControlsView, like ScopeBox.

#pragma once

#include "valis/UiTheme.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

namespace valis {

class ValisProcessor;

class SpectrumBox final : public juce::Component
{
public:
    /// Two knob cells wide, one tall: 2 x 104 by 152.
    static constexpr int kWidth  = 208;
    static constexpr int kHeight = 152;

    SpectrumBox(ValisProcessor& processor, std::string nodeId, juce::String title);

    void setTheme(const EquipmentTheme& theme) { this->theme = theme; }

    /// Message thread. Pulls the latest tap samples, runs the spectrum, then
    /// repaints. Cheap enough to run at the view's refresh rate.
    void refresh();

    void paint(juce::Graphics& g) override;

private:
    ValisProcessor& processor;
    std::string nodeId;
    juce::String title;
    EquipmentTheme theme = themeByName("Dark");

    static constexpr int kFftOrder = 11;               // 2048 points
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kBands    = 48;
    static constexpr float kFloorDb = -72.0f;

    std::vector<float> samples;    ///< scratch for the tap read
    std::vector<float> fftWork;    ///< interleaved FFT workspace
    std::vector<float> levels;     ///< smoothed 0..1 bar levels
    juce::dsp::FFT fft{kFftOrder}; ///< twiddles built once, message thread
    juce::String readout = "Low: --   Mid: --   High: --";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumBox)
};

}  // namespace valis
