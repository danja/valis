// include/valis/FileOption.h
//
// Elements that read a file name it in a val: option. The session's choice of
// file (the Controls view's Load button, set_sample) overrides that option, so
// the plugin, the Controls view and the ops all need to know which one.

#pragma once

#include <string_view>

namespace valis {

/// Whether the Controls view gives an element a file slot, and so whether a session can swap
/// the file it reads. These are the elements that play or run one file on their own account;
/// val:Granulator also reads a file but records into the same buffer, so a swapped file would
/// be overwritten by what it hears.
inline bool hasFileSlot(std::string_view implementation)
{
    return implementation == "SampleLoad" || implementation == "NamModel"
        || implementation == "TimeStretch";
}

/// The option key naming the file an element reads, by registry key.
inline std::string_view fileOptionKey(std::string_view implementation)
{
    return implementation == "NamModel" ? "model" : "file";
}

}  // namespace valis
