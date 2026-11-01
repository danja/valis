// src/net/Freesound.cpp
#include "valis/Freesound.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>

namespace valis {

namespace {

std::string urlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string licenceFilter(const std::string& license)
{
    if (license == "cc0")   return "license:\"Creative Commons 0\"";
    if (license == "cc-by") return "license:\"Attribution\"";
    if (license == "any" || license.empty()) return {};
    return "license:\"" + license + "\"";
}

std::string number(double v)
{
    return juce::String(v, 3).toStdString();
}

} // namespace

Freesound::Freesound(std::string key, Fetch f) : apiKey(std::move(key)), fetch(std::move(f)) {}

std::string Freesound::keyFromEnv(const std::string& envText)
{
    const juce::StringArray lines = juce::StringArray::fromLines(juce::String(envText));
    for (const auto& raw : lines)
    {
        const auto line = raw.trim();
        if (line.startsWith("#") || ! line.startsWith("FREESOUND_API_KEY"))
            continue;
        auto value = line.fromFirstOccurrenceOf("=", false, false).trim();
        value = value.unquoted();
        return value.toStdString();
    }
    return {};
}

std::string Freesound::searchUrl(const FreesoundQuery& q)
{
    std::vector<std::string> filters;
    if (const auto f = licenceFilter(q.license); ! f.empty())
        filters.push_back(f);
    if (q.minSeconds > 0.0 || q.maxSeconds > 0.0)
        filters.push_back("duration:[" + (q.minSeconds > 0.0 ? number(q.minSeconds) : std::string("*")) + " TO "
                          + (q.maxSeconds > 0.0 ? number(q.maxSeconds) : std::string("*")) + "]");

    std::string filter;
    for (const auto& f : filters)
        filter += (filter.empty() ? "" : " ") + f;

    std::string url = "https://freesound.org/apiv2/search/text/?query=" + urlEncode(q.text)
                    + "&fields=id,name,username,license,tags,duration"
                    + "&page_size=" + std::to_string(std::clamp(q.pageSize, 1, 50));
    if (! filter.empty())
        url += "&filter=" + urlEncode(filter);
    if (! q.sort.empty())
        url += "&sort=" + urlEncode(q.sort);
    return url;
}

namespace {

FreesoundSound soundFrom(const juce::var& v)
{
    FreesoundSound s;
    s.id = static_cast<int>(v["id"]);
    s.name = v["name"].toString().toStdString();
    s.username = v["username"].toString().toStdString();
    s.license = v["license"].toString().toStdString();
    s.seconds = static_cast<double>(v["duration"]);
    s.pageUrl = "https://freesound.org/s/" + std::to_string(s.id) + "/";
    if (const auto* tags = v["tags"].getArray())
        for (const auto& t : *tags)
            s.tags += (s.tags.empty() ? "" : " ") + t.toString().toStdString();
    return s;
}

} // namespace

bool Freesound::parseSearch(const std::string& json, std::vector<FreesoundSound>& sounds, std::string& error)
{
    const auto root = juce::JSON::parse(juce::String(json));
    if (! root.isObject())
    {
        error = "Freesound returned something that is not JSON";
        return false;
    }
    if (root.hasProperty("detail") && ! root.hasProperty("results"))
    {
        error = "Freesound refused the request: " + root["detail"].toString().toStdString();
        return false;
    }
    const auto* results = root["results"].getArray();
    if (results == nullptr)
    {
        error = "Freesound's reply has no results list";
        return false;
    }
    sounds.clear();
    for (const auto& r : *results)
        sounds.push_back(soundFrom(r));
    return true;
}

bool Freesound::parseSound(const std::string& json, FreesoundSound& sound, std::string& previewUrl, std::string& error)
{
    const auto root = juce::JSON::parse(juce::String(json));
    if (! root.isObject())
    {
        error = "Freesound returned something that is not JSON";
        return false;
    }
    if (root.hasProperty("detail"))
    {
        error = "Freesound refused the request: " + root["detail"].toString().toStdString();
        return false;
    }
    sound = soundFrom(root);
    previewUrl = root["previews"]["preview-hq-ogg"].toString().toStdString();
    if (previewUrl.empty())
    {
        error = "the sound has no Ogg preview";
        return false;
    }
    return true;
}

std::string Freesound::slug(const std::string& name)
{
    std::string out;
    bool pendingDash = false;
    for (const unsigned char c : name)
    {
        if (std::isalnum(c))
        {
            if (pendingDash && ! out.empty())
                out += '-';
            pendingDash = false;
            out += static_cast<char>(std::tolower(c));
        }
        else
            pendingDash = true;
    }
    if (out.size() > 40)
        out.resize(40);
    while (! out.empty() && out.back() == '-')
        out.pop_back();
    return out.empty() ? std::string("sound") : out;
}

bool Freesound::search(const FreesoundQuery& query, std::vector<FreesoundSound>& sounds, std::string& error) const
{
    if (apiKey.empty())
    {
        error = "no Freesound API key: put FREESOUND_API_KEY=... in .env";
        return false;
    }
    std::string body;
    if (! fetch(searchUrl(query), apiKey, body, error))
        return false;
    return parseSearch(body, sounds, error);
}

bool Freesound::download(int id, const std::string& directory, const std::string& name, std::string& pathWritten,
                         FreesoundSound& sound, std::string& error) const
{
    if (apiKey.empty())
    {
        error = "no Freesound API key: put FREESOUND_API_KEY=... in .env";
        return false;
    }
    std::string body, previewUrl;
    if (! fetch("https://freesound.org/apiv2/sounds/" + std::to_string(id) + "/?fields=id,name,username,license,tags,duration,previews",
                apiKey, body, error)
        || ! parseSound(body, sound, previewUrl, error))
        return false;

    std::string audio;
    if (! fetch(previewUrl, apiKey, audio, error))
        return false;
    if (audio.size() < 4 || audio.compare(0, 4, "OggS") != 0)
    {
        error = "the preview is not an Ogg file";
        return false;
    }

    const auto stem = (name.empty() ? slug(sound.name) : slug(name)) + "-" + std::to_string(id);
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(directory));
    if (! dir.createDirectory())
    {
        error = "cannot create " + dir.getFullPathName().toStdString();
        return false;
    }
    const auto file = dir.getChildFile(juce::String(stem) + ".ogg");
    if (! file.replaceWithData(audio.data(), audio.size()))
    {
        error = "cannot write " + file.getFullPathName().toStdString();
        return false;
    }

    auto* record = new juce::DynamicObject();
    record->setProperty("id", sound.id);
    record->setProperty("name", juce::String(sound.name));
    record->setProperty("author", juce::String(sound.username));
    record->setProperty("license", juce::String(sound.license));
    record->setProperty("page", juce::String(sound.pageUrl));
    record->setProperty("file", file.getFileName());
    record->setProperty("kind", "192 kbps Ogg preview, not the original upload");
    dir.getChildFile(juce::String(stem) + ".json").replaceWithText(juce::JSON::toString(juce::var(record)));

    pathWritten = file.getFullPathName().toStdString();
    return true;
}

} // namespace valis
