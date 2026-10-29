// include/valis/CircuitDiff.h
//
// What changed between two states of a circuit, in plain sentences: the elements added and removed,
// the values and options that were set, the arcs made and broken, the parameters bound. A
// comparison of what the circuits mean and not of their text, so reformatting is not a change and
// a renamed prefix does not hide one.

#pragma once

#include <string>
#include <vector>

namespace valis {

class CircuitModel;

/// One sentence per change, elements first, then arcs, then parameters. Empty when they are the same.
std::vector<std::string> diffCircuits(const CircuitModel& before, const CircuitModel& after);

}  // namespace valis
