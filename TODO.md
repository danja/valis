Add horizontal and vertical scrollbars to the graph layout (shown when graph goes out of display area), add a vertical scrollbar to  the knobs view.

The Add Element popup in graph view shows up way off the target area. It should also be possible to create and delete connections in the graph view using the mouse. This should auto-update the Turtle content and the knobs view. Similarly valid changes to the Turtle should auto-update the graph and knobs views.

New item on the Settings menu : Autolayout - this should run a layout algorithm on the graph view, optimising it for the visible window.
 
Ctl-s keystroke should save the current current file (if it's already been saved) or bring up the Save as... dialog.

I think there is some VST tesing code in ~/VST_SDK that might be useful for validation.

Maximise compatibility with /home/danny/github/transmission - try it as a test host using MCP. We maintain Transmission so can changes things over there if need be.

* test coverage — Envelope, Compressor, Scale, Mixer, Delay, DryWet have no dedicated behaviour tests (covered indirectly by engine/circuit tests)

## Recurring - check periodically

* for new material, check test coverage
* ensure README.md and docs are up-to-date