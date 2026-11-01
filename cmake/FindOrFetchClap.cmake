# cmake/FindOrFetchClap.cmake
#
# JUCE has no CLAP wrapper of its own, so the format comes from the third-party
# clap-juce-extensions. It is behind VALIS_WITH_CLAP because it is the one
# format that pulls an extra repository.

include(ValisDeps)

FetchContent_Declare(clap-juce-extensions
  GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions
  GIT_TAG main
  GIT_SHALLOW TRUE
  GIT_SUBMODULES_RECURSE TRUE)

valis_make_available(clap-juce-extensions)
