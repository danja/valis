// src/plugin/ValisProcessor.h

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace valis {

/// The plugin's parameter list is fixed at construction: VST3, LV2 and CLAP all
/// require a static list, so parameters cannot appear when a new circuit loads.
/// Turtle `val:Param` declarations bind a slot to an element property instead.
inline constexpr int kNumParamSlots = 64;

class ValisProcessor final : public juce::AudioProcessor
{
public:
    ValisProcessor();
    ~ValisProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;  // keep the double-precision overload visible

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Valis"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return apvts; }

    /// The circuit's Turtle source. Saved verbatim in plugin state, so a session
    /// is self-contained. Message thread only.
    juce::String getTurtle() const;
    void setTurtle(const juce::String& turtle);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Guards turtleSource against the editor and the MCP thread. Never taken on
    // the audio callback.
    mutable juce::CriticalSection turtleLock;
    juce::String turtleSource;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValisProcessor)
};

}  // namespace valis
