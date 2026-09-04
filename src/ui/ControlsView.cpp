// src/ui/ControlsView.cpp

#include "ui/ControlsView.h"

#include "plugin/ValisProcessor.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

namespace valis {

namespace {
constexpr int kKnobWidth      = 104;
constexpr int kKnobHeight     = 152;
constexpr int kNameHeight     = 18;
constexpr int kValueHeight    = 20;
constexpr int kTargetHeight   = 14;
constexpr int kSectionSepHeight = 16;  // thin row separator with section name
}

juce::String ControlsView::Knob::readout() const
{
    if (slider == nullptr)
        return {};

    const auto real = minimum + slider->getValue() * (maximum - minimum);
    const auto span = maximum - minimum;
    const int decimals = span > 100.0 ? 0 : (span > 1.0 ? 2 : 3);
    return juce::String(real, decimals) + unit;
}

ControlsView::ControlsView(ValisProcessor& p) : processor(p)
{
    p.addChangeListener(this);
    emptyMessage.setJustificationType(juce::Justification::centred);
    emptyMessage.setColour(juce::Label::textColourId, juce::Colours::grey);
    emptyMessage.setFont(juce::FontOptions(15.0f));
    emptyMessage.setText("This circuit declares no val:Param bindings.\n"
                         "Add one in the Code tab to put a knob here.",
                         juce::dontSendNotification);
    addAndMakeVisible(emptyMessage);

    rebuild();
    startTimerHz(2);
}

ControlsView::~ControlsView()
{
    processor.removeChangeListener(this);
    // Attachments must go before the sliders they reference.
    knobs.clear();
}

void ControlsView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    rebuild();
}

void ControlsView::timerCallback()
{
    // Cheap poll: the panel only has to change when the circuit does.
    const auto& model = processor.circuit();
    const auto bindCount = static_cast<int>(model.params().size());
    const auto elemCount = static_cast<int>(model.elements().size());
    if (bindCount != lastBindingCount || elemCount != lastElementCount)
        rebuild();

    // Refresh meter readouts from the engine's control store.
    for (auto& m : meters)
    {
        const auto peak = processor.getControlOutput(m.nodeId, "peak");
        const auto rms  = processor.getControlOutput(m.nodeId, "rms");
        const auto freq = processor.getControlOutput(m.nodeId, "frequency");

        const auto fmt = [](std::optional<float> v, const char* unit, int decimals)
        {
            return v ? juce::String(*v, decimals) + unit : juce::String("--");
        };

        m.readout->setText("Peak: " + fmt(peak, "", 3) +
                           "   RMS: " + fmt(rms, "", 3) +
                           "   Freq: " + fmt(freq, " Hz", 0),
                           juce::dontSendNotification);
    }
}

void ControlsView::rebuild()
{
    knobs.clear();
    meters.clear();

    const auto& model = processor.circuit();
    lastBindingCount = static_cast<int>(model.params().size());
    lastElementCount = static_cast<int>(model.elements().size());

    for (const auto& binding : model.params())
    {
        const auto* element = model.findElement(binding.targetNode);
        if (element == nullptr || element->type == nullptr)
            continue;

        const auto* port = element->type->findProperty(binding.propertySymbol);
        if (port == nullptr)
            continue;

        Knob knob;

        knob.minimum = port->minimum;
        knob.maximum = port->maximum;
        knob.unit    = port->unitSymbol.empty() ? juce::String()
                                                : " " + juce::String(port->unitSymbol);
        knob.target  = juce::String(vocab::shortName(binding.targetNode)) + "." +
                       juce::String(binding.propertySymbol);

        knob.name = std::make_unique<juce::Label>();
        knob.name->setJustificationType(juce::Justification::centred);
        knob.name->setColour(juce::Label::textColourId, juce::Colour(0xffabb2bf));
        knob.name->setFont(juce::FontOptions(13.0f, juce::Font::bold));
        knob.name->setText(binding.name.empty() ? port->name : binding.name,
                           juce::dontSendNotification);
        addAndMakeVisible(*knob.name);

        knob.sectionName = binding.section;

        const auto paramId = "p" + juce::String(binding.slot).paddedLeft('0', 2);

        if (port->enumeration && ! port->scalePoints.empty())
        {
            knob.comboBox = std::make_unique<juce::ComboBox>();
            knob.comboBox->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2d35));
            knob.comboBox->setColour(juce::ComboBox::textColourId, juce::Colour(0xffabb2bf));
            knob.comboBox->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a3f4b));
            knob.comboBox->setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff61afef));
            for (const auto& [value, label] : port->scalePoints)
                knob.comboBox->addItem(label, static_cast<int>(value) + 1);
            addAndMakeVisible(*knob.comboBox);
            knob.comboAttachment =
                std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                    processor.state(), paramId, *knob.comboBox);
        }
        else
        {
            knob.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                         juce::Slider::NoTextBox);
            knob.slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff61afef));
            knob.slider->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff3a3f4b));
            knob.slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffabb2bf));
            knob.slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible(*knob.slider);
            knob.attachment =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor.state(), paramId, *knob.slider);
            knob.slider->onValueChange = [this] { repaint(); };
        }

        knobs.push_back(std::move(knob));
    }

    // Build meters for any Oscilloscope elements in the circuit.
    for (const auto& elem : model.elements())
    {
        if (elem.type == nullptr || elem.type->implementation != "Oscilloscope")
            continue;

        Meter m;
        m.nodeId = elem.id;

        const juce::String label = elem.label.empty()
            ? juce::String(vocab::shortName(elem.id))
            : juce::String(elem.label);

        m.name = std::make_unique<juce::Label>();
        m.name->setJustificationType(juce::Justification::centred);
        m.name->setColour(juce::Label::textColourId, juce::Colour(0xffabb2bf));
        m.name->setFont(juce::FontOptions(13.0f, juce::Font::bold));
        m.name->setText(label, juce::dontSendNotification);
        addAndMakeVisible(*m.name);

        m.readout = std::make_unique<juce::Label>();
        m.readout->setJustificationType(juce::Justification::centred);
        m.readout->setColour(juce::Label::textColourId, juce::Colour(0xff98c379));
        m.readout->setFont(juce::FontOptions(12.0f));
        m.readout->setText("Peak: --   RMS: --   Freq: --", juce::dontSendNotification);
        addAndMakeVisible(*m.readout);

        meters.push_back(std::move(m));
    }

    emptyMessage.setVisible(knobs.empty() && meters.empty());
    resized();
    repaint();
}

void ControlsView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e22));

    const int w = getWidth();

    for (const auto& sep : sectionSeps)
    {
        g.setColour(juce::Colour(0xff3a3f4b));
        g.fillRect(12, sep.y + kSectionSepHeight / 2, w - 24, 1);

        if (sep.name.isNotEmpty())
        {
            g.setColour(juce::Colour(0xff61afef));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(sep.name, 16, sep.y, w - 32, kSectionSepHeight,
                       juce::Justification::centredLeft, true);
        }
    }

    for (const auto& knob : knobs)
    {
        const juce::Rectangle<int> area = knob.isEnum()
            ? knob.comboBox->getBounds()
            : knob.slider->getBounds();

        if (! knob.isEnum())
        {
            g.setColour(juce::Colour(0xffabb2bf));
            g.setFont(juce::FontOptions(13.0f));
            g.drawText(knob.readout(),
                       area.getX(), area.getBottom() - kValueHeight, area.getWidth(), kValueHeight,
                       juce::Justification::centred, false);
        }

        g.setColour(juce::Colour(0xff5a6070));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(knob.target,
                   area.getX(), area.getBottom(), area.getWidth(), kTargetHeight,
                   juce::Justification::centred, true);
    }

    for (const auto& m : meters)
    {
        if (m.readout)
        {
            const auto& rb = m.readout->getBounds();
            g.setColour(juce::Colour(0xff2a2d35));
            g.fillRoundedRectangle(rb.toFloat().reduced(2.0f), 4.0f);
        }
    }
}

void ControlsView::parentSizeChanged()
{
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        setSize(vp->getMaximumVisibleWidth(), getHeight());
}

void ControlsView::resized()
{
    const int w = getWidth();
    if (w <= 0)
        return;

    const int perRow = juce::jmax(1, (w - 24) / kKnobWidth);

    // Returns true when a section separator should appear before knob index i
    // (i.e. it starts a new named section and is the first knob in its row).
    auto needsSep = [&](int i, int colAtI) -> bool
    {
        if (colAtI != 0)
            return false;
        const auto& sec = knobs[static_cast<std::size_t>(i)].sectionName;
        if (sec.isEmpty())
            return false;
        if (i == 0)
            return true;
        return knobs[static_cast<std::size_t>(i - 1)].sectionName != sec;
    };

    // --- measure pass ---
    auto measure = [&]() -> int
    {
        int y = 0, col = 0;
        for (int i = 0; i < static_cast<int>(knobs.size()); ++i)
        {
            if (needsSep(i, col)) y += kSectionSepHeight;
            ++col;
            if (col >= perRow) { y += kKnobHeight; col = 0; }
        }
        if (col > 0 || (knobs.empty() && meters.empty())) y += kKnobHeight;

        if (!meters.empty())
        {
            if (col > 0) y += kKnobHeight;   // flush last knob row
            y += kSectionSepHeight;           // "Monitors" separator
            for (std::size_t i = 0; i < meters.size(); ++i)
                y += Meter::kHeight;
        }
        return y + 24;
    };

    sectionSeps.clear();

    const int needed = measure();
    if (needed != getHeight())
    {
        setSize(w, needed);
        return;
    }

    emptyMessage.setBounds(getLocalBounds().reduced(40));

    const int originX = getLocalBounds().reduced(12).getX();
    const int originY = getLocalBounds().reduced(12).getY();

    int curY = originY;
    int col  = 0;

    for (int i = 0; i < static_cast<int>(knobs.size()); ++i)
    {
        auto& knob = knobs[static_cast<std::size_t>(i)];

        if (needsSep(i, col))
        {
            sectionSeps.push_back({ curY, knob.sectionName });
            curY += kSectionSepHeight;
        }

        const int x = originX + col * kKnobWidth;
        juce::Rectangle<int> cell(x, curY, kKnobWidth, kKnobHeight);

        knob.name->setBounds(cell.removeFromTop(kNameHeight));
        cell.removeFromBottom(kTargetHeight);

        if (knob.isEnum())
            knob.comboBox->setBounds(cell.removeFromTop(28).reduced(4, 2));
        else
            knob.slider->setBounds(cell.reduced(2, 2));

        ++col;
        if (col >= perRow) { curY += kKnobHeight; col = 0; }
    }
    if (col > 0) curY += kKnobHeight;

    if (!meters.empty())
    {
        sectionSeps.push_back({ curY, "Monitors" });
        curY += kSectionSepHeight;

        for (auto& m : meters)
        {
            m.name->setBounds(originX, curY, w - 24, kNameHeight);
            curY += kNameHeight;
            m.readout->setBounds(originX, curY, w - 24, Meter::kHeight - kNameHeight);
            curY += Meter::kHeight - kNameHeight;
        }
    }

    repaint();
}

}  // namespace valis
