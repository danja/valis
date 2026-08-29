# /new-element — Scaffold a new DSP element

Creates a new Valis DSP element end-to-end. Provide a name and element type as arguments, e.g.:

```
/new-element Chorus filter
```

## Arguments

`$ARGUMENTS` — `<ElementName> <category>` where category is one of:
`source`, `filter`, `transfer`, `dynamics`, `utility`

## What to do

Given `ElementName` and `category`:

### 1. Ontology (`vocabs/valis.ttl`)

Add a new class block near other elements of the same category. Follow this pattern exactly (copy the closest similar element, adjust ports):

```turtle
val:ElementName a rdfs:Class ; rdfs:subClassOf val:Element ;
    rdfs:label "ElementName" ;
    rdfs:comment "One-sentence description of what it does." ;
    val:implementation "ElementName" ;
    val:linear false ;
    lv2:port
      [ a lv2:InputPort, lv2:AudioPort ; lv2:symbol "in" ; lv2:name "In" ] ,
      [ a lv2:OutputPort, lv2:AudioPort ; lv2:symbol "out" ; lv2:name "Out" ] ,
      [ a lv2:InputPort, lv2:ControlPort ; lv2:symbol "param" ;
        lv2:name "Param" ; lv2:default 0.5 ;
        lv2:minimum 0.0 ; lv2:maximum 1.0 ] .
```

Rules:
- Use `val:linear true` only if processing is strictly linear (no saturation, no state-dependent nonlinearity).
- Match port symbols to what the C++ class calls `controlIndex(type, "symbol")`.
- Use `units:unit units:ms` / `units:db` / `units:hz` where applicable.
- Audio ports: `lv2:AudioPort`. Control ports: `lv2:ControlPort`.

### 2. C++ implementation

Choose the right source file for the category:
- `source` → `src/dsp/elements/Sources.cpp`
- `filter` → `src/dsp/elements/Filters.cpp`
- `transfer` → `src/dsp/elements/Transfers.cpp`
- `dynamics` → `src/dsp/elements/Dynamics.cpp`
- `utility` → `src/dsp/elements/Utility.cpp`

Add the class inside `namespace valis::elements { ... }` before the closing brace. Template:

```cpp
class ElementName final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        paramIndex = controlIndex(type, "param");
    }

    void reset() override { /* clear state */ }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float param = controlAt(args, paramIndex, 0.5f);
        const float* in   = args.audioIn[0];
        float*       out  = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
            out[i] = in[i]; // TODO: implement
    }

private:
    double sampleRate = 44100.0;
    int paramIndex = -1;
};
```

Use `MonoElement` (extends `DspElement`, provides `processMono`) for simple stereo-agnostic elements. Inherit directly from `DspElement` when you need full control.

**Real-time rules (non-negotiable):**
- No allocation in `process()` or `reset()` — allocate in `prepare()`.
- No std::vector growth, no new/delete, no I/O, no locks.
- `noexcept` on `process()` and `reset()`.

### 3. Registry

In the same `.cpp` file, add to the `register*` function:

```cpp
registry.add("ElementName", &make<elements::ElementName>);
```

### 4. Test

Create `tests/dsp/ElementNameTest.cpp`:

```cpp
// tests/dsp/ElementNameTest.cpp
#include <cassert>
#include "../../src/dsp/elements/Common.h"

int main()
{
    // Minimal smoke test: prepare → process → check output is finite.
    // Add specific golden-output checks for deterministic behaviour.
    return 0;
}
```

Register in `tests/CMakeLists.txt` following the same pattern as existing dsp tests.

### 5. Verify

After making changes:
1. Run `./build.sh` — all 9 tests must pass.
2. The `dsp_ElementRegistryTest` asserts ontology ↔ registry symmetry; it will catch any mismatch.
3. Update `TODO.md` if this element was listed there.
