// tests/net/FreesoundTest.cpp
#include "valis/Freesound.h"

#include <juce_core/juce_core.h>

#include <cassert>

using namespace valis;

int main()
{
    // The key is read from .env text and a comment or another variable is not mistaken for it.
    assert(Freesound::keyFromEnv("FREESOUND_CLIENT_ID=abc\nFREESOUND_API_KEY=secret123\n") == "secret123");
    assert(Freesound::keyFromEnv("# FREESOUND_API_KEY=no\nOTHER=1") .empty());
    assert(Freesound::keyFromEnv("FREESOUND_API_KEY=\"quoted\"") == "quoted");

    // The request: encoded words, the licence filter, a duration range, a bounded page.
    FreesoundQuery q;
    q.text = "kick drum";
    q.maxSeconds = 2.0;
    q.pageSize = 500;
    const auto url = Freesound::searchUrl(q);
    assert(url.find("query=kick%20drum") != std::string::npos);
    assert(url.find("filter=license%3A%22Creative%20Commons%200%22%20duration%3A%5B%2A%20TO%202.000%5D") != std::string::npos);
    assert(url.find("page_size=50") != std::string::npos);
    q.license = "any";
    q.maxSeconds = 0.0;
    assert(Freesound::searchUrl(q).find("filter=") == std::string::npos);

    // A search reply is parsed; a refusal is an error naming Freesound's reason.
    std::vector<FreesoundSound> sounds;
    std::string error;
    const std::string reply = R"({"count":1,"results":[{"id":42,"name":"Big Kick","username":"somebody",
        "license":"http://creativecommons.org/publicdomain/zero/1.0/","tags":["kick","drum"],"duration":0.8}]})";
    assert(Freesound::parseSearch(reply, sounds, error));
    assert(sounds.size() == 1 && sounds[0].id == 42 && sounds[0].tags == "kick drum");
    assert(sounds[0].pageUrl == "https://freesound.org/s/42/");
    assert(! Freesound::parseSearch(R"({"detail":"Invalid token."})", sounds, error));
    assert(error.find("Invalid token") != std::string::npos);
    assert(! Freesound::parseSearch("<html>", sounds, error));

    assert(Freesound::slug("Big Kick -- (Dry)!") == "big-kick-dry");
    assert(Freesound::slug("???") == "sound");

    // Download with an injected fetch: the file and its licence record land together, and the key goes with
    // every request (the real fetch withholds it from other hosts).
    int fetches = 0;
    const Freesound fs("k", [&](const std::string& u, const std::string& token, std::string& body, std::string&)
    {
        ++fetches;
        assert(token == "k");
        if (u.find("/apiv2/sounds/42/") != std::string::npos)
            body = R"({"id":42,"name":"Big Kick","username":"somebody","license":"cc0","tags":[],"duration":0.8,
                       "previews":{"preview-hq-ogg":"https://cdn.freesound.org/previews/42.ogg"}})";
        else
            body = std::string("OggS") + "payload";
        return true;
    });
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("valis-freesound-test");
    dir.deleteRecursively();
    std::string path;
    FreesoundSound sound;
    assert(fs.download(42, dir.getFullPathName().toStdString(), "kick", path, sound, error));
    assert(fetches == 2);
    assert(juce::File(path).getFileName() == "kick-42.ogg");
    const auto record = juce::JSON::parse(dir.getChildFile("kick-42.json"));
    assert(record["author"].toString() == "somebody" && record["license"].toString() == "cc0");

    // A reply that is not audio is refused and nothing is written.
    const Freesound bad("k", [&](const std::string& u, const std::string&, std::string& body, std::string&)
    {
        body = u.find("/apiv2/sounds/") != std::string::npos
             ? R"({"id":7,"name":"x","previews":{"preview-hq-ogg":"https://cdn/x.ogg"}})" : "<html>";
        return true;
    });
    assert(! bad.download(7, dir.getFullPathName().toStdString(), "", path, sound, error));
    assert(! dir.getChildFile("x-7.ogg").exists());

    // No key: a located error, no request made.
    const Freesound none("", [&](const std::string&, const std::string&, std::string&, std::string&) { assert(false); return false; });
    assert(! none.search(q, sounds, error) && error.find(".env") != std::string::npos);

    dir.deleteRecursively();
    return 0;
}
