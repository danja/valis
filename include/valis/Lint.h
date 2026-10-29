// include/valis/Lint.h
//
// Static checks on a circuit that has already compiled. The compiler refuses what cannot
// run; this reports what runs and does not do what its author meant. Each finding is
// something that has cost somebody an afternoon: a value that a control arc quietly
// overwrites, a knob that moves nothing, an effect with nothing to process, a part that
// can never be heard.
//
// Message thread only. It reads the model and writes nothing.

#pragma once

#include <string>
#include <vector>

namespace valis {

class CircuitModel;

struct LintFinding
{
    enum class Severity { error, warning, info };

    Severity severity = Severity::warning;
    std::string code;      ///< a short stable name, such as "control-overrides-value"
    std::string node;      ///< the element's IRI, or empty
    std::string port;      ///< the port or property involved, or empty
    std::string message;   ///< what is wrong and what to do about it
};

/// Errors first, then warnings, then information; within each, in the order found.
std::vector<LintFinding> lintCircuit(const CircuitModel& model);

/// The findings as one JSON object with counts.
std::string lintToJson(const std::vector<LintFinding>& findings);

}  // namespace valis
