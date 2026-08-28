// src/plugin/ValisProcessor.cpp

#include "plugin/ValisProcessor.h"

#include "ui/ValisEditor.h"

namespace valis {

namespace {
juce::String slotId(int i)   { return "p" + juce::String(i).paddedLeft('0', 2); }
juce::String slotName(int i) { return "Param " + juce::String(i); }
}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ValisProcessor::makeParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // All slots are plain normalised 0-1. A val:Param binding supplies the name,
    // range and unit for display; unbound slots stay inert and hidden.
    for (int i = 0; i < kNumParamSlots; ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{slotId(i), 1}, slotName(i),
            juce::NormalisableRange<float>{0.0f, 1.0f}, 0.0f));

    return layout;
}

ValisProcessor::ValisProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "VALIS", makeParameterLayout())
{
}

void ValisProcessor::prepareToPlay(double, int)
{
    // TODO(M4): prepare the engine and preallocate the buffer pool here.
}

void ValisProcessor::releaseResources() {}

bool ValisProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void ValisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // TODO(M4): swap in the compiled circuit and run it. Pass-through until then.
}

juce::AudioProcessorEditor* ValisProcessor::createEditor()
{
    return new ValisEditor(*this);
}

juce::String ValisProcessor::getTurtle() const
{
    const juce::ScopedLock sl(turtleLock);
    return turtleSource;
}

void ValisProcessor::setTurtle(const juce::String& turtle)
{
    const juce::ScopedLock sl(turtleLock);
    turtleSource = turtle;
    // TODO(M5): compile and hand the result to the engine.
}

void ValisProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto tree = apvts.copyState();
    tree.setProperty("turtle", getTurtle(), nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void ValisProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml(*xml);
    setTurtle(tree.getProperty("turtle", juce::String()).toString());
    apvts.replaceState(tree);
}

}  // namespace valis

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new valis::ValisProcessor();
}
