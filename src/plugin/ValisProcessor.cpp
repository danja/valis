// src/plugin/ValisProcessor.cpp

#include "plugin/ValisProcessor.h"

#include "ui/ValisEditor.h"
#include "valis/TurtleStore.h"

namespace valis {

namespace {
juce::String slotId(int i)   { return "p" + juce::String(i).paddedLeft('0', 2); }
juce::String slotName(int i) { return "Param " + juce::String(i); }
}  // namespace

// ---------------------------------------------------------------------------
// ValisParameter
// ---------------------------------------------------------------------------

ValisParameter::ValisParameter(int slotIndex)
    : juce::AudioParameterFloat(juce::ParameterID{slotId(slotIndex), 1},
                                slotName(slotIndex),
                                juce::NormalisableRange<float>{0.0f, 1.0f},
                                0.0f),
      slot(slotIndex),
      label(slotName(slotIndex))
{
}

void ValisParameter::bind(std::string targetNodeId, std::string targetProperty,
                          juce::String displayName, double minimum, double maximum,
                          juce::String unit)
{
    node       = std::move(targetNodeId);
    property   = std::move(targetProperty);
    label      = displayName.isNotEmpty() ? displayName : juce::String(property);
    lo         = minimum;
    hi         = maximum > minimum ? maximum : minimum + 1.0;
    unitSymbol = std::move(unit);
    bound      = true;
}

void ValisParameter::unbind()
{
    bound = false;
    node.clear();
    property.clear();
    label = slotName(slot);
    unitSymbol.clear();
    lo = 0.0;
    hi = 1.0;
}

double ValisParameter::realValue() const
{
    return lo + static_cast<double>(get()) * (hi - lo);
}

void ValisParameter::setRealValue(double value)
{
    const auto clamped = juce::jlimit(lo, hi, value);
    setValueNotifyingHost(static_cast<float>((clamped - lo) / (hi - lo)));
}

juce::String ValisParameter::getName(int maximumLength) const
{
    return label.substring(0, maximumLength);
}

juce::String ValisParameter::getLabel() const
{
    return unitSymbol;
}

juce::String ValisParameter::getText(float normalised, int maximumLength) const
{
    if (! bound)
        return juce::String(normalised, 3).substring(0, maximumLength);

    const auto real = lo + static_cast<double>(normalised) * (hi - lo);

    // A wide range reads better with fewer decimals; a 0-1 control needs them.
    const int decimals = (hi - lo) > 100.0 ? 0 : ((hi - lo) > 1.0 ? 2 : 3);
    return juce::String(real, decimals).substring(0, maximumLength);
}

juce::AudioProcessorValueTreeState::ParameterLayout ValisProcessor::makeParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // All slots are plain normalised 0-1. A val:Param binding supplies the name,
    // range and unit for display; unbound slots stay inert and hidden.
    for (int i = 0; i < kNumParamSlots; ++i)
        layout.add(std::make_unique<ValisParameter>(i));

    return layout;
}

ValisProcessor::ValisProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "VALIS", makeParameterLayout())
{
    registry = makeDefaultRegistry();

    std::vector<std::string> ontologyErrors;

    // Units first: named units such as units:hz get their symbols from here,
    // and they are resolved as the element ports are read.
    vocabulary.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", ontologyErrors);

    if (! vocabulary.loadFile(VALIS_VOCABS_DIR "/valis.ttl", ontologyErrors))
        for (const auto& e : ontologyErrors)
            diagnostics.push_back({e, {}});

    // Persist state across focus loss and crashes via platform settings file.
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Valis";
    opts.filenameSuffix  = ".props";
    opts.folderName      = "Valis";
    appProperties.setStorageParameters(opts);

    // Try to restore the last session; fall back to the bundled example.
    bool restored = false;
    if (auto* settings = appProperties.getUserSettings())
    {
        const auto saved = settings->getValue("turtle");
        if (saved.isNotEmpty())
        {
            restored = true;
            setTurtle(saved);
        }
    }
    if (! restored)
        setTurtle(juce::File(VALIS_EXAMPLES_DIR "/basic.ttl").loadFileAsString());

    for (int i = 0; i < kNumParamSlots; ++i)
        if (auto* parameter = apvts.getParameter(slotId(i)))
            parameter->addListener(this);

   #if VALIS_WITH_MCP
    // Created here; started only when the user enables it via Settings → MCP Server.
    mcpServer = std::make_unique<McpServer>([this] { return ops(); });
   #endif

    // Retired graphs are freed here, and parameter changes are applied here.
    startTimerHz(30);
}

OpDispatcher ValisProcessor::ops()
{
    OpContext ctx;
    ctx.ontology = &vocabulary;
    ctx.engine   = &engine;
    ctx.readTurtle = [this] { return getTurtle().toStdString(); };
    ctx.writeTurtle = [this](const std::string& turtle, std::vector<Diagnostic>& out)
    {
        return setTurtle(juce::String(turtle), out);
    };
    ctx.readModel = [this]() -> const CircuitModel* { return &model; };
    return OpDispatcher(ctx);
}

ValisProcessor::~ValisProcessor()
{
   #if VALIS_WITH_MCP
    if (mcpServer != nullptr)
        mcpServer->stop();
   #endif

    for (int i = 0; i < kNumParamSlots; ++i)
        if (auto* parameter = apvts.getParameter(slotId(i)))
            parameter->removeListener(this);
}

void ValisProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    engine.prepare(sampleRate, maximumExpectedSamplesPerBlock);
    monoScratch.setSize(2, maximumExpectedSamplesPerBlock, false, true, true);

    // Reinstalling rebuilds every element against the new rate.
    setTurtle(getTurtle());
}

void ValisProcessor::releaseResources() {}

bool ValisProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void ValisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Note events reach the engine before the block runs. Sample-accurate
    // placement within the block is a later refinement; the control grid is
    // 32 samples, so the error is bounded by that.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
            engine.noteOn(message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff())
            engine.noteOff(message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.allNotesOff();
    }

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    for (int i = numIn; i < numOut; ++i)
        buffer.clear(i, 0, numSamples);

    if (! engine.hasCircuit() || numSamples > monoScratch.getNumSamples())
        return;

    // The circuit model is mono, so sum in and fan out. Per-channel circuits
    // are a later milestone.
    float* mono = monoScratch.getWritePointer(0);
    if (numIn > 0)
    {
        juce::FloatVectorOperations::copy(mono, buffer.getReadPointer(0), numSamples);
        for (int c = 1; c < numIn; ++c)
            juce::FloatVectorOperations::add(mono, buffer.getReadPointer(c), numSamples);
        if (numIn > 1)
            juce::FloatVectorOperations::multiply(mono, 1.0f / static_cast<float>(numIn), numSamples);
    }
    else
    {
        juce::FloatVectorOperations::clear(mono, numSamples);
    }

    float* rendered = monoScratch.getWritePointer(1);
    engine.process(mono, rendered, numSamples);

    for (int c = 0; c < numOut; ++c)
        juce::FloatVectorOperations::copy(buffer.getWritePointer(c), rendered, numSamples);
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
    std::vector<Diagnostic> ignored;
    setTurtle(turtle, ignored);
}

bool ValisProcessor::setTurtle(const juce::String& turtle, std::vector<Diagnostic>& out)
{
    // The editor's text is the user's text, kept verbatim even while it is
    // mid-edit and broken. What must not change on failure is the circuit.
    {
        const juce::ScopedLock sl(turtleLock);
        turtleSource = turtle;
    }

    out.clear();

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    if (! store.parse(turtle.toStdString(), "urn:valis:circuit", parseErrors))
    {
        for (const auto& e : parseErrors)
            out.push_back({e.message, {}, e.line, e.col});
    }

    CircuitModel candidate;
    if (out.empty() && candidate.build(store, vocabulary, out))
    {
        CompiledCircuit compiled;
        CircuitCompiler compiler;

        if (compiler.compile(candidate, vocabulary, compiled, out))
        {
            std::string error;
            if (engine.load(compiled, registry, error))
            {
                model = std::move(candidate);
                setLatencySamples(engine.latencyInSamples());
                rebindParameters();

                {
                    const juce::ScopedLock sl(turtleLock);
                    diagnostics = out;
                }
                autoSaveTicks = 90;  // ~3 s at 30 Hz — save after brief idle
                sendChangeMessage();
                return true;
            }
            out.push_back({error, {}});
        }
    }

    // The previous circuit keeps playing: a typo must not silence the plugin.
    {
        const juce::ScopedLock sl(turtleLock);
        diagnostics = out;
    }
    sendChangeMessage();
    return false;
}

std::vector<Diagnostic> ValisProcessor::lastDiagnostics() const
{
    const juce::ScopedLock sl(turtleLock);
    return diagnostics;
}

ValisParameter* ValisProcessor::slot(int index)
{
    if (index < 0 || index >= kNumParamSlots)
        return nullptr;

    return dynamic_cast<ValisParameter*>(apvts.getParameter(slotId(index)));
}

void ValisProcessor::rebindParameters()
{
    // The host's parameter list cannot change, so rebinding repoints the fixed
    // slots at whatever the new circuit declares and asks the host to re-read
    // their names and ranges.
    for (int i = 0; i < kNumParamSlots; ++i)
        if (auto* parameter = slot(i))
            parameter->unbind();

    for (const auto& binding : model.params())
    {
        auto* parameter = slot(binding.slot);
        if (parameter == nullptr)
            continue;

        const auto* element = model.findElement(binding.targetNode);
        if (element == nullptr || element->type == nullptr)
            continue;

        const auto* port = element->type->findProperty(binding.propertySymbol);
        if (port == nullptr)
            continue;

        parameter->bind(binding.targetNode, binding.propertySymbol,
                        binding.name.empty() ? juce::String(port->name) : juce::String(binding.name),
                        binding.minimum.value_or(port->minimum),
                        binding.maximum.value_or(port->maximum),
                        juce::String(port->unitSymbol));

        // Take the circuit's own value as the slot's starting point, so opening
        // a circuit does not immediately overwrite what it declares.
        parameter->setRealValue(element->valueOf(binding.propertySymbol));
    }

    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}
                          .withParameterInfoChanged(true));

    applyParameterBindings();
}

void ValisProcessor::applyParameterBindings()
{
    // Push each bound slot's current value into the engine, so a reload keeps
    // whatever the host has already automated.
    for (int i = 0; i < kNumParamSlots; ++i)
    {
        auto* parameter = slot(i);
        if (parameter == nullptr || ! parameter->isBound())
            continue;

        engine.setControl(parameter->targetNode(), parameter->targetProperty(),
                          static_cast<float>(parameter->realValue()));
    }
}

void ValisProcessor::parameterValueChanged(int, float)
{
    // Called from the host's automation thread. Flag it and let the timer do
    // the work: setControl walks the graph, which is not for this thread.
    parametersDirty.store(true, std::memory_order_release);
}

void ValisProcessor::timerCallback()
{
    engine.collectGarbage();

    if (parametersDirty.exchange(false, std::memory_order_acq_rel))
        applyParameterBindings();

    if (autoSaveTicks > 0 && --autoSaveTicks == 0)
    {
        if (auto* settings = appProperties.getUserSettings())
        {
            settings->setValue("turtle", getTurtle());
            settings->saveIfNeeded();
        }
    }
}

void ValisProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto tree = apvts.copyState();
    tree.setProperty("turtle", getTurtle(), nullptr);
   #if VALIS_WITH_MCP
    tree.setProperty("mcpEnabled", isMcpRunning(), nullptr);
    tree.setProperty("mcpPort",    mcpPort(),       nullptr);
   #endif
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
    autoSaveTicks = -1;  // setTurtle already scheduled save; don't double-fire

   #if VALIS_WITH_MCP
    if ((bool) tree.getProperty("mcpEnabled", false))
        startMcp((int) tree.getProperty("mcpPort", 7676));
   #endif
}

#if VALIS_WITH_MCP
bool ValisProcessor::startMcp(int requestedPort, const std::string& token)
{
    if (mcpServer == nullptr) return false;
    if (mcpServer->isRunning()) mcpServer->stop();

    const int portToUse = requestedPort > 0 ? requestedPort
        : juce::SystemStats::getEnvironmentVariable("VALIS_MCP_PORT", "7676").getIntValue();

    return mcpServer->start(portToUse, token);
}

void ValisProcessor::stopMcp()
{
    if (mcpServer != nullptr)
        mcpServer->stop();
}

bool ValisProcessor::isMcpRunning() const
{
    return mcpServer != nullptr && mcpServer->isRunning();
}

int ValisProcessor::mcpPort() const
{
    return mcpServer != nullptr ? mcpServer->boundPort() : 0;
}
#endif

}  // namespace valis

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new valis::ValisProcessor();
}
