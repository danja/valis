Rings circuit — further Rings/Elements extensions:
- Beam resonator (stiff string, dispersion): val:StiffString element with allpass dispersion in feedback
- Rings-style "bowed string" exciter: val:Bow element (friction/Stribeck model, requires resonator feedback)
- UI enhancements for integer/enum parameters: dropdown selector, radio buttons, checkboxes in Knobs view
  (requires: new port annotation in ontology e.g. lv2:enumeration, new component in ControlsView.cpp)

## Recurring - once the above is clear

* update README.md and docs for recent changes
* check code for long files - if found, refactor
* check test coverage
* check MISTAKES.md for patterns, update CLAUDE.md as appropriate
