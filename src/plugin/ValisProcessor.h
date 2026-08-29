// src/plugin/ValisProcessor.h

#pragma once

#include "valis/CircuitCompiler.h"
#include "valis/CircuitModel.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Ops.h"
#include "valis/ValisEngine.h"

#if VALIS_WITH_MCP
 #include "mcp/McpServer.h"
#endif

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace valis {

/// The plugin's parameter list is fixed at construction: VST3, LV2 and CLAP all
/// require a static list, so parameters cannot appear when a new circuit loads.
/// Turtle `val:Param` declarations bind a slot to an element property instead.
inline constexpr int kNumParamSlots = 64;

/// One host parameter slot. The host sees a fixed list of these; what each one
/// means is decided by the circuit's val:Param bindings and can change when a
/// new circuit loads, so the name, range and unit are mutable and the host is
/// told to re-read them.
class ValisParameter final : public juce::AudioParameterFloat
{
public:
    explicit ValisParameter(int slotIndex);

    /// Message thread. Points this slot at an element property.
    void bind(std::string node, std::string property,
              juce::String displayName, double minimum, double maximum, juce::String unit);
    void unbind();

    bool isBound() const { return bound; }
    const std::string& targetNode() const { return node; }
    const std::string& targetProperty() const { return property; }

    /// The slot's value in the property's own units.
    double realValue() const;
    void setRealValue(double value);

    juce::String getName(int maximumLength) const override;
    juce::String getLabel() const override;
    juce::String getText(float normalised, int maximumLength) const override;

private:
    int slot = 0;
    bool bound = false;
    std::string node, property;
    juce::String label, unitSymbol;
    double lo = 0.0, hi = 1.0;
};

class ValisProcessor final : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::Timer,
                             private juce::AudioProcessorParameter::Listener
{
public:
    ValisProcessor();
    ~ValisProcessor() override;

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

    /// Parses, validates, compiles and installs. Returns false and fills
    /// `diagnostics` if the circuit will not run; the previous circuit keeps
    /// playing in that case, so a typo never silences the plugin.
    bool setTurtle(const juce::String& turtle, std::vector<Diagnostic>& diagnostics);
    void setTurtle(const juce::String& turtle);

    /// Diagnostics from the most recent setTurtle, for the editor's gutter.
    std::vector<Diagnostic> lastDiagnostics() const;

    const CircuitModel& circuit() const { return model; }
    const Ontology& ontology() const { return vocabulary; }

    /// Builds the op surface over this processor. The three views and the MCP
    /// server all go through here.
    OpDispatcher ops();

   #if VALIS_WITH_MCP
    McpServer& mcp() { return *mcpServer; }

    /// Start or restart the MCP server. Port 0 falls back to VALIS_MCP_PORT or 7676.
    bool startMcp(int port = 0, const std::string& token = {});
    void stopMcp();
    bool isMcpRunning() const;
    int  mcpPort() const;
   #endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

    void timerCallback() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}

    ValisParameter* slot(int index);
    void rebindParameters();
    void applyParameterBindings();

    std::atomic<bool> parametersDirty{false};

    Ontology vocabulary;
    ElementRegistry registry;
    ValisEngine engine;
    CircuitModel model;
    std::vector<Diagnostic> diagnostics;
    juce::AudioBuffer<float> monoScratch;

   #if VALIS_WITH_MCP
    std::unique_ptr<McpServer> mcpServer;
   #endif

    juce::AudioProcessorValueTreeState apvts;

    // Guards turtleSource against the editor and the MCP thread. Never taken on
    // the audio callback.
    mutable juce::CriticalSection turtleLock;
    juce::String turtleSource;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValisProcessor)
};

}  // namespace valis
