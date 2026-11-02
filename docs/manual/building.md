# Building

## Prerequisites

CMake 3.22 or newer, and a C and C++20 compiler. JUCE refuses to configure
unless C is among the project languages, so a C compiler is not optional.

JUCE 9.0.1 is added by `add_subdirectory`; `VALIS_JUCE_DIR` sets the path.

On Debian or Ubuntu, with `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0` set by the
build so neither WebKit nor libcurl is needed:

```sh
sudo apt install \
  build-essential cmake ninja-build pkg-config \
  libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libxi-dev
```

serd and sord are used through `libserd-dev` and `libsord-dev` when pkg-config
finds them, and otherwise fetched and compiled from source. Either way the
build works with no action.

Dependencies that are fetched (serd, sord, cpp-httplib, ONNX Runtime and, when
asked for, the CLAP extensions) are fetched once and kept in `deps/` in the
repository, which git ignores. Every build directory, Debug or Release, then
uses that copy and needs no network. To fetch one again, for instance after
changing its pinned version in `cmake/`, delete its directory in `deps/`. Set
`VALIS_DEPS_DIR` to keep the copies somewhere else, such as a directory shared
between checkouts.

## Building

```sh
./build.sh              # Debug with tests, CTest, then a Release plugin
VALIS_WITH_CLAP=1 ./build.sh   # and the CLAP format
```

Options: `VALIS_BUILD_TESTS` (ON), `VALIS_BUILD_PLUGIN` (ON), `VALIS_WITH_MCP`
(ON), `VALIS_WITH_CLAP` (OFF), `VALIS_JUCE_DIR`.

## Running

```sh
./valis                                   # the standalone (see standalone.md)
ctest --test-dir build --output-on-failure
./build/valis_render_artefacts/Debug/valis_render circuits/skream.ttl \
    -o out.wav --tone 100
```

`valis-render` needs no host, no GUI and no audio device, which makes it the
fastest way to hear a change.

## Verifying the plugin formats

```sh
LV2_PATH="$PWD/build/valis_plugin_artefacts/Debug/LV2" lv2ls
LV2_PATH="$PWD/build/valis_plugin_artefacts/Debug/LV2" lv2info urn:valis:valis
```

`lv2info` reports ports but not parameters: JUCE 9 exposes plugin parameters
through the LV2 patch extension rather than as control ports. To see the slots,
read the generated manifest:

```sh
grep -A5 'plug:p00' build/valis_plugin_artefacts/Debug/LV2/Valis.lv2/dsp.ttl
```

### VST3 conformance check

`build.sh` runs the Steinberg [VST3 SDK](https://github.com/steinberg/vst3sdk) validator on the
Release plugin when it finds one. If it cannot, it prints the path it tried and skips the check.
To build the validator once:

```sh
cmake -S ~/VST_SDK/vst3sdk -B ~/VST_SDK/build -DCMAKE_BUILD_TYPE=Release \
      -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
      -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF -DSMTG_RUN_VST_VALIDATOR=OFF
cmake --build ~/VST_SDK/build --target validator
```

`build.sh` looks for `~/VST_SDK/build/bin/Release/validator`; set `VALIDATOR=/path/to/validator`
to use another copy. To run it by hand:

```sh
~/VST_SDK/build/bin/Release/validator build-release/valis_plugin_artefacts/Release/VST3/Valis.vst3
```

The plugin passes all 47 tests. The "64bit Audio Processing not supported" lines are
informational: Valis processes 32-bit audio only.

## Regenerating the docs

`docs/manual/elements.md` is generated from `vocabs/valis.ttl`, so it cannot
drift from what the plugin offers:

```sh
./scripts/generate-docs.sh
```
