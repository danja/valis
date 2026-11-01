// src/ui/SamplerBox.cpp

#include "ui/SamplerBox.h"

#include "dsp/ResolveFile.h"
#include "plugin/ValisProcessor.h"
#include "valis/SampleTable.h"

namespace valis {

namespace {
constexpr int kHeader = 28;
constexpr int kFooter = 36;
}  // namespace

SamplerBox::SamplerBox(ValisProcessor& p, std::string node, juce::String boxTitle)
    : processor(p), nodeId(std::move(node)), title(std::move(boxTitle))
{
    addAndMakeVisible(addButton);
    addAndMakeVisible(copyButton);
    addAndMakeVisible(revertButton);

    addButton.onClick = [this] { addSounds(); };
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard(juce::String(processor.sampleFor(nodeId)));
        message = "Table copied. Paste it into val:samples to keep this kit in the circuit.";
        repaint();
    };
    revertButton.onClick = [this]
    {
        processor.clearSample(nodeId);
        message = "Back to the kit the circuit declares.";
        refresh();
    };

    refresh();
}

SamplerBox::~SamplerBox() = default;

void SamplerBox::setTheme(const EquipmentTheme& t)
{
    theme = t;
    for (auto* b : { &addButton, &copyButton, &revertButton })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(theme.comboBg));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(theme.labelText));
    }
    for (auto& s : strips)
        s->setTheme(theme);
    repaint();
}

int SamplerBox::preferredHeight() const
{
    return kHeader + juce::jmax(1, static_cast<int>(strips.size())) * PadStrip::kHeight + kFooter + 12;
}

std::string SamplerBox::pathFor(const juce::File& file) const
{
    // A sound inside the source tree is written relative to it, so a kit saved from here still loads elsewhere.
    const juce::File root(VALIS_ROOT_DIR);
    if (file.isAChildOf(root))
        return file.getRelativePathFrom(root).replaceCharacter('\\', '/').toStdString();
    return file.getFullPathName().toStdString();
}

void SamplerBox::commit(const std::function<bool(SampleTable&)>& change)
{
    auto table = SampleTable::parse(processor.sampleFor(nodeId));
    if (! change(table))
        return;

    std::string error;
    if (processor.setSample(nodeId, table.toString(), error))
        message.clear();
    else
    {
        // The previous kit stays in use.
        message = juce::String(error).fromLastOccurrenceOf(": ", false, false);
        if (message.isEmpty()) message = juce::String(error);
    }
    refresh();
}

void SamplerBox::request(std::function<bool(SampleTable&)> change)
{
    // A strip's own callback must not end by destroying the strip, which a new table can do (a removed pad).
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SamplerBox>(this), change = std::move(change)]
    {
        if (safe != nullptr)
            safe->commit(change);
    });
}

void SamplerBox::addSounds()
{
    const juce::File root(VALIS_ROOT_DIR);
    auto start = root.getChildFile("audio");
    if (! start.isDirectory()) start = root;

    chooser = std::make_unique<juce::FileChooser>("Add sounds to " + title, start,
                                                  "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectMultipleItems,
                         [this](const juce::FileChooser& fc)
    {
        const auto files = fc.getResults();
        if (files.isEmpty())
            return;
        // Each takes the next free key from 36, in the order chosen, so a folder of hits lays itself out as a kit.
        commit([&](SampleTable& table)
        {
            for (const auto& f : files)
                table.addPad(pathFor(f), f.getFileNameWithoutExtension().toStdString());
            return true;
        });
    });
}

void SamplerBox::chooseFor(int pad)
{
    const auto pads = SampleTable::parse(processor.sampleFor(nodeId)).pads();
    if (pad < 0 || pad >= static_cast<int>(pads.size()))
        return;

    auto start = dsp::resolveFile(pads[static_cast<std::size_t>(pad)].file).getParentDirectory();
    if (! start.isDirectory())
        start = juce::File(VALIS_ROOT_DIR).getChildFile("audio");

    chooser = std::make_unique<juce::FileChooser>("Choose a sound for " + juce::String(pads[static_cast<std::size_t>(pad)].name),
                                                  start, "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, pad](const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        commit([&](SampleTable& table) { return table.setFile(pad, pathFor(file)); });
    });
}

void SamplerBox::rebuildStrips()
{
    strips.clear();
    const auto pads = SampleTable::parse(shown).pads();
    for (std::size_t i = 0; i < pads.size(); ++i)
    {
        auto strip = std::make_unique<PadStrip>();
        const int index = static_cast<int>(i);
        const int lowKey = pads[i].lowKey;

        strip->onAudition = [this, lowKey](bool down)
        {
            if (down) processor.injectNoteOn(lowKey, 0.8f);
            else      processor.injectNoteOff(lowKey);
        };
        strip->onLevel = [this, index](float db) { request([=](SampleTable& t) { return t.setLevel(index, db); }); };
        strip->onPan   = [this, index](float p)  { request([=](SampleTable& t) { return t.setPan(index, p); }); };
        strip->onName  = [this, index](const juce::String& n)
        {
            request([=](SampleTable& t) { return t.setName(index, n.toStdString()); });
        };
        strip->onKeys  = [this, index](const juce::String& k)
        {
            request([=](SampleTable& t) { return t.setKeys(index, k.trim().toStdString()); });
        };
        strip->onLoad   = [this, index] { chooseFor(index); };
        strip->onRemove = [this, index] { request([=](SampleTable& t) { return t.removePad(index); }); };

        strip->setTheme(theme);
        addAndMakeVisible(*strip);
        strips.push_back(std::move(strip));
    }
    resized();
}

void SamplerBox::refresh()
{
    const auto table = processor.sampleFor(nodeId);
    const auto pads = SampleTable::parse(table).pads();

    // A different number of instruments, or a table that moved under us, means new strips; otherwise only
    // the values change, so a text field being typed in is not disturbed.
    const bool sameShape = pads.size() == strips.size();
    shown = table;
    if (! sameShape)
    {
        rebuildStrips();
        if (auto* parent = getParentComponent())
            parent->resized();
    }
    for (std::size_t i = 0; i < pads.size() && i < strips.size(); ++i)
        strips[i]->show(pads[i]);

    if (message.isEmpty())
        if (const juce::String problem(processor.problemWith(nodeId)); problem.isNotEmpty())
            message = problem;
    repaint();
}

void SamplerBox::resized()
{
    auto area = getLocalBounds().reduced(10);
    area.removeFromTop(kHeader);

    auto footer = area.removeFromBottom(kFooter - 6);
    addButton.setBounds(footer.removeFromLeft(110));
    footer.removeFromLeft(6);
    copyButton.setBounds(footer.removeFromLeft(100));
    footer.removeFromLeft(6);
    revertButton.setBounds(footer.removeFromLeft(140));

    for (auto& strip : strips)
        strip->setBounds(area.removeFromTop(PadStrip::kHeight));
}

void SamplerBox::paint(juce::Graphics& g)
{
    const auto plate = getLocalBounds().toFloat().reduced(4.0f);
    g.setColour(juce::Colour(theme.plateBg));
    g.fillRoundedRectangle(plate, 4.0f);
    g.setColour(juce::Colour(theme.edgeDark));
    g.drawRoundedRectangle(plate, 4.0f, 1.4f);

    auto area = getLocalBounds().reduced(10);
    g.setColour(juce::Colour(theme.labelText));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    auto header = area.removeFromTop(kHeader);
    g.drawText(title.toUpperCase(), header.removeFromLeft(220), juce::Justification::centredLeft, true);

    g.setColour(juce::Colour(theme.dimText));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(strips.empty() ? "No sounds yet. Add some with the button below."
                              : "Each row is one sound: key, name, file, level and pan.",
               header, juce::Justification::centredLeft, true);

    if (message.isNotEmpty())
    {
        g.setColour(juce::Colour(theme.accent));
        g.drawFittedText(message, getLocalBounds().reduced(10).removeFromBottom(kFooter - 6).withTrimmedLeft(370),
                         juce::Justification::centredLeft, 2, 0.8f);
    }
}

}  // namespace valis
