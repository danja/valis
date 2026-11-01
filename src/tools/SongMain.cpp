// src/tools/SongMain.cpp
//
// valis-song - renders and mixes a song file. The same operations as the MCP tools read_song, render_song and mix_song,
// for a script that has no server to call: no GUI, no host, no audio device. The reply is the operation's JSON.

#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Ops.h"

#include <juce_core/juce_core.h>

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

void usage()
{
    std::puts(
        "valis-song - render and mix a song file (a Turtle file with one val:Song; see docs/songs.md)\n"
        "\n"
        "  valis-song check  <song.ttl>\n"
        "  valis-song score  <song.ttl> [--only a,b]\n"
        "  valis-song render <song.ttl> [--only a,b] [--workers N]\n"
        "  valis-song stems  <song.ttl>\n"
        "  valis-song reaper <song.ttl> [--output file.rpp] [--overwrite]\n"
        "  valis-song mix    <song.ttl> [--version vN] [--arc] [--lufs N] [--overwrite]\n"
        "\n"
        "check prints the layout and any file the song names that is missing. stems compares each rendered wav with its\n"
        "MIDI file, bar by bar, and reports silence where there are notes, clipping and non-finite samples. render writes one wav per stem to wavs/\n"
        "beside the song file. mix writes mixes/<title>-<version>.wav and will not replace an existing one unless\n"
        "--overwrite. Both take --wavs <dir>; mix takes --output <file>. Relative paths are read against the working\n"
        "directory. The reply is JSON on standard output; a failure is a message on standard error and exit status 1.");
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 2 || args[0] == "-h" || args[0] == "--help")
    {
        usage();
        return args.empty() ? 1 : 0;
    }

    const std::string command = args[0], song = args[1];
    std::vector<std::string> only;
    std::string version, wavs, output;
    std::optional<double> lufs;
    int workers = 3;
    bool arc = false, overwrite = false;
    for (std::size_t i = 2; i < args.size(); ++i)
    {
        const auto& a = args[i];
        const auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--only")
        {
            for (const auto& name : juce::StringArray::fromTokens(juce::String(next()), ",", ""))
                only.push_back(name.toStdString());
        }
        else if (a == "--workers")   workers = std::stoi(next());
        else if (a == "--version")   version = next();
        else if (a == "--lufs")      lufs = std::stod(next());
        else if (a == "--wavs")      wavs = next();
        else if (a == "--output")    output = next();
        else if (a == "--arc")       arc = true;
        else if (a == "--overwrite") overwrite = true;
        else
        {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 1;
        }
    }

    valis::Ontology ontology;
    std::vector<std::string> ontologyErrors;
    ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", ontologyErrors);
    if (! ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", ontologyErrors))
    {
        for (const auto& e : ontologyErrors)
            std::fprintf(stderr, "ontology: %s\n", e.c_str());
        return 1;
    }
    const auto registry = valis::makeDefaultRegistry();

    valis::OpContext context;
    context.ontology = &ontology;
    context.registry = &registry;
    const valis::OpDispatcher ops(context);

    valis::OpResult result;
    if (command == "check")        result = ops.readSong(song);
    else if (command == "render")  result = ops.renderSong(song, only, workers, wavs);
    else if (command == "score")   result = ops.scoreSong(song, only);
    else if (command == "reaper")  result = ops.reaperProject(song, wavs, output, overwrite);
    else if (command == "stems")   result = ops.checkSongStems(song, wavs);
    else if (command == "mix")     result = ops.mixSong(song, version, arc, lufs, overwrite, wavs, output);
    else
    {
        usage();
        return 1;
    }

    if (! result.ok)
    {
        for (const auto& d : result.diagnostics)
            std::fprintf(stderr, "%s\n", d.message.c_str());
        return 1;
    }
    std::puts(result.value.c_str());
    return 0;
}
