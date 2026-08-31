// src/ui/ControlsView.cpp

#include "ui/ControlsView.h"

#include "plugin/ValisProcessor.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

namespace valis {

namespace {
constexpr int kKnobWidth    = 104;
constexpr int kKnobHeight   = 152;
constexpr int kNameHeight   = 18;
constexpr int kValueHeight  = 20;
constexpr int kTargetHeight = 14;
constexpr int kSectionHeight = 28;
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
                         "Add one in the Turtle view to put a knob here.",
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
    const auto count = static_cast<int>(processor.circuit().params().size());
    if (count != lastBindingCount)
        rebuild();
}

void ControlsView::rebuild()
{
    knobs.clear();

    const auto& model = processor.circuit();
    lastBindingCount = static_cast<int>(model.params().size());

    std::string currentSection;

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

        if (!binding.section.empty() && binding.section != currentSection)
        {
            currentSection = binding.section;
            knob.sectionLabel = std::make_unique<juce::Label>();
            knob.sectionLabel->setJustificationType(juce::Justification::centredLeft);
            knob.sectionLabel->setColour(juce::Label::textColourId, juce::Colour(0xff61afef));
            knob.sectionLabel->setFont(juce::FontOptions(12.0f, juce::Font::bold));
            knob.sectionLabel->setText(binding.section, juce::dontSendNotification);
            addAndMakeVisible(*knob.sectionLabel);
        }

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

    emptyMessage.setVisible(knobs.empty());
    resized();
    repaint();
}

void ControlsView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e22));

    for (const auto& knob : knobs)
    {
        if (knob.sectionLabel)
        {
            const auto& b = knob.sectionLabel->getBounds();
            g.setColour(juce::Colour(0xff3a3f4b));
            g.fillRect(b.getX(), b.getBottom() - 1, b.getWidth(), 1);
        }

        // Enum knobs use a ComboBox that labels itself; only dials need a readout.
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

    // Measure total height needed by simulating the layout pass.
    auto measure = [&]() -> int
    {
        int y        = 0;
        int colInRow = 0;
        for (const auto& knob : knobs)
        {
            if (knob.sectionLabel)
            {
                if (colInRow > 0) { y += kKnobHeight; colInRow = 0; }
                y += kSectionHeight;
            }
            ++colInRow;
            if (colInRow >= perRow) { y += kKnobHeight; colInRow = 0; }
        }
        if (colInRow > 0 || knobs.empty()) y += kKnobHeight;
        return y + 24;
    };

    const int needed = measure();
    if (needed != getHeight())
    {
        setSize(w, needed);
        return;
    }

    emptyMessage.setBounds(getLocalBounds().reduced(40));

    const int originX = getLocalBounds().reduced(12).getX();
    const int originY = getLocalBounds().reduced(12).getY();

    int curY     = originY;
    int colInRow = 0;

    for (auto& knob : knobs)
    {
        if (knob.sectionLabel)
        {
            if (colInRow > 0) { curY += kKnobHeight; colInRow = 0; }
            knob.sectionLabel->setBounds(originX, curY, w - 24, kSectionHeight);
            curY += kSectionHeight;
        }

        const int x = originX + colInRow * kKnobWidth;
        juce::Rectangle<int> cell(x, curY, kKnobWidth, kKnobHeight);

        knob.name->setBounds(cell.removeFromTop(kNameHeight));
        cell.removeFromBottom(kTargetHeight);

        if (knob.isEnum())
            knob.comboBox->setBounds(cell.removeFromTop(28).reduced(4, 2));
        else
            knob.slider->setBounds(cell.reduced(2, 2));

        ++colInRow;
        if (colInRow >= perRow) { curY += kKnobHeight; colInRow = 0; }
    }
}

}  // namespace valis
