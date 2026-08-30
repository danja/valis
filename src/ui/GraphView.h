// src/ui/GraphView.h
//
// The node-and-arc view. Adapted from JUCE's AudioPluginHost GraphEditorPanel
// for the connector-drag, pin hit-testing and bezier routing; its
// AudioProcessorGraph runtime is deliberately not used, because Valis has its
// own engine and its own rules about feedback.
//
// Staged, as docs/plan.md sets out:
//   1. render, and drag node positions
//   2. create and delete arcs
//   3. add and delete nodes
//
// Structural edits go through OpDispatcher, so they take exactly the path the
// Turtle view and the MCP server take. That path re-serialises the document,
// which loses hand formatting and comments - hence the warning on first use.

#pragma once

#include "valis/Ops.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <optional>
#include <string>

namespace valis {

class ValisProcessor;

class GraphView final : public juce::Component,
                        private juce::Timer
{
public:
    explicit GraphView(ValisProcessor&);
    ~GraphView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    void rebuild();
    void autolayout();

private:
    struct Pin
    {
        std::string node, port;
        bool input = false, control = false;
        juce::Point<float> position;
    };

    struct NodeBox
    {
        std::string id, label, typeLabel;
        juce::Rectangle<float> bounds;
        std::vector<Pin> pins;
    };

    void timerCallback() override;
    void layout();
    OpDispatcher ops();

    const NodeBox* nodeAt(juce::Point<float>) const;
    const Pin* pinAt(juce::Point<float>) const;
    std::optional<Arc> arcAt(juce::Point<float>) const;
    void showMenuFor(const NodeBox*, juce::Point<int>);
    void showArcMenu(const Arc&, juce::Point<int>);
    void warnAboutReformatting();

    ValisProcessor& processor;

    std::vector<NodeBox> nodes;
    std::map<std::string, juce::Point<float>> positions;   ///< editor metadata

    // Stage 1: dragging a node moves editor metadata only, never the circuit.
    NodeBox* draggedNode = nullptr;
    juce::Point<float> dragOffset;

    // Stage 2: dragging from a pin creates an arc.
    std::optional<Pin> draggingFrom;
    juce::Point<float> dragTo;

    juce::String message;
    bool warnedAboutReformatting = false;
    int lastElementCount = -1, lastArcCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphView)
};

}  // namespace valis
