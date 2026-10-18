// include/valis/FileOption.h
//
// Elements that read a file name it in a val: option. The session's choice of
// file (the Controls view's Load button, set_sample) overrides that option, so
// the plugin, the Controls view and the ops all need to know which one.

#pragma once

#include <string_view>

namespace valis {

/// The option key naming the file an element reads, by registry key.
inline std::string_view fileOptionKey(std::string_view implementation)
{
    return implementation == "NamModel" ? "model" : "file";
}

}  // namespace valis
