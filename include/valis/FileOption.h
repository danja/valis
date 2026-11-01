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

/// A val:Sampler is chosen by a table of sounds, not by one file. The session's table sits on top of the
/// document's, exactly as a chosen file does, and the Controls view gives it a pad panel of its own.
inline bool hasSampleTable(std::string_view implementation)
{
    return implementation == "Sampler";
}

/// The option key naming the file (or, for a sampler, the table of files) an element reads, by registry key.
inline std::string_view fileOptionKey(std::string_view implementation)
{
    if (implementation == "Sampler")
        return "samples";
    return implementation == "NamModel" ? "model" : "file";
}

}  // namespace valis
