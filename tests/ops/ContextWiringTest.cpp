// tests/ops/ContextWiringTest.cpp
//
// The plugin builds the context the ops run in (ValisProcessor::ops), and the tests build their
// own by hand. An op that needs a field the plugin forgot to fill fails only in the plugin: render
// refused to run there for a long time because the registry was never set, and no test could see
// it, since each test's context had everything.
//
// So the plugin's source is read as text and held to the context struct: every field OpContext
// declares must be assigned there. Add a field and this fails until the plugin sets it.

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

std::string slurp(const std::string& path)
{
    std::ifstream file(path);
    assert(file.good());
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

int main()
{
    // The fields: the identifier before each semicolon in the struct, comments set aside.
    const auto header = slurp(VALIS_ROOT_DIR "/include/valis/Ops.h");
    const auto start = header.find("struct OpContext");
    assert(start != std::string::npos);
    const auto end = header.find("\n};", start);
    std::string body = header.substr(start, end - start);

    std::istringstream lines(body);
    std::string code;
    for (std::string line; std::getline(lines, line);)
    {
        const auto first = line.find_first_not_of(' ');
        if (first != std::string::npos && line.compare(first, 2, "//") != 0)
            code += line + "\n";
    }

    std::set<std::string> fields;
    static const std::regex field("(\\w+)(?:\\s*=\\s*[^;]*)?;");
    for (std::sregex_iterator it(code.begin(), code.end(), field), stop; it != stop; ++it)
        fields.insert((*it)[1]);

    assert(fields.size() >= 8);
    for (const char* known : {"ontology", "engine", "registry", "history", "readTurtle", "writeTurtle", "readModel", "readSample", "writeSample"})
        assert(fields.count(known) != 0 && "OpContext lost a field this test knows about");

    // What the plugin assigns.
    const auto plugin = slurp(VALIS_ROOT_DIR "/src/plugin/ValisProcessor.cpp");
    const auto at = plugin.find("OpDispatcher ValisProcessor::ops()");
    assert(at != std::string::npos);
    const auto function = plugin.substr(at, plugin.find("\n}\n", at) - at);

    int missing = 0;
    for (const auto& name : fields)
        if (function.find("ctx." + name + " ") == std::string::npos && function.find("ctx." + name + "=") == std::string::npos)
        {
            std::printf("  ValisProcessor::ops() never sets ctx.%s\n", name.c_str());
            ++missing;
        }
    assert(missing == 0);

    std::printf("  %zu context fields, all set by the plugin\n", fields.size());
    std::puts("ContextWiringTest PASSED");
    return 0;
}
