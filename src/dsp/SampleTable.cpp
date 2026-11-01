// src/dsp/SampleTable.cpp
#include "valis/SampleTable.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace valis {

namespace {

std::string trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

std::string number(double v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.2f", v);
    std::string s = buffer;
    while (! s.empty() && s.back() == '0') s.pop_back();
    if (! s.empty() && s.back() == '.') s.pop_back();
    return s.empty() || s == "-0" ? std::string("0") : s;
}

double numberOf(const std::string* text, double fallback)
{
    if (text == nullptr) return fallback;
    char* end = nullptr;
    const double v = std::strtod(text->c_str(), &end);
    return end != text->c_str() ? v : fallback;
}

std::string stem(const std::string& path)
{
    auto name = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
    if (const auto dot = name.find_last_of('.'); dot != std::string::npos && dot > 0)
        name.resize(dot);
    return name;
}

} // namespace

bool SampleTable::words(const std::string& line, std::vector<std::pair<std::string, std::string>>& out, std::string& error)
{
    std::size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        const auto eq = line.find('=', i);
        if (eq == std::string::npos)
        {
            error = "expected key=value, found '" + line.substr(i) + "'";
            return false;
        }
        const auto key = line.substr(i, eq - i);
        i = eq + 1;
        std::string value;
        if (i < line.size() && line[i] == '"')
        {
            const auto close = line.find('"', i + 1);
            if (close == std::string::npos)
            {
                error = "unterminated quote after " + key + "=";
                return false;
            }
            value = line.substr(i + 1, close - i - 1);
            i = close + 1;
        }
        else
        {
            const auto end = line.find_first_of(" \t", i);
            value = line.substr(i, end == std::string::npos ? std::string::npos : end - i);
            i = end == std::string::npos ? line.size() : end;
        }
        out.emplace_back(key, value);
    }
    return true;
}

SampleTable SampleTable::parse(const std::string& text)
{
    SampleTable table;
    std::size_t from = 0;
    while (from <= text.size())
    {
        auto to = text.find('\n', from);
        if (to == std::string::npos) to = text.size();
        const auto physical = trim(text.substr(from, to - from));
        from = to + 1;
        if (physical.empty())
            continue;
        if (physical[0] == '#')
        {
            table.rows.push_back(Row{true, physical, {}});
            continue;
        }
        std::size_t start = 0;
        while (start <= physical.size())
        {
            auto end = physical.find(';', start);
            if (end == std::string::npos) end = physical.size();
            const auto line = trim(physical.substr(start, end - start));
            start = end + 1;
            if (line.empty())
                continue;
            Row row;
            std::string error;
            if (words(line, row.words, error))
                table.rows.push_back(std::move(row));
            else
                table.rows.push_back(Row{true, "# " + line, {}});   // kept, not lost
        }
    }
    return table;
}

std::string SampleTable::toString() const
{
    std::string out;
    for (const auto& row : rows)
    {
        if (row.comment)
        {
            out += row.text;
        }
        else
        {
            for (const auto& [key, value] : row.words)
            {
                if (! out.empty() && out.back() != '\n') out += ' ';
                const bool quote = value.empty() || value.find_first_of(" \t;") != std::string::npos;
                out += key + "=" + (quote ? "\"" + value + "\"" : value);
            }
        }
        out += '\n';
    }
    return out;
}

const std::string* SampleTable::find(const Row& row, const std::string& key)
{
    for (const auto& [k, v] : row.words)
        if (k == key) return &v;
    return nullptr;
}

void SampleTable::set(Row& row, const std::string& key, const std::string& value)
{
    for (auto& [k, v] : row.words)
        if (k == key) { v = value; return; }
    row.words.emplace_back(key, value);
}

std::string SampleTable::keysOf(const Row& row)
{
    if (const auto* k = find(row, "keys")) return *k;
    if (const auto* k = find(row, "key")) return *k;
    return "0-127";
}

std::vector<std::vector<int>> SampleTable::groups() const
{
    std::vector<std::vector<int>> result;
    std::vector<std::string> seen;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].comment) continue;
        const auto keys = keysOf(rows[i]);
        const auto at = std::find(seen.begin(), seen.end(), keys);
        if (at == seen.end())
        {
            seen.push_back(keys);
            result.push_back({static_cast<int>(i)});
        }
        else
            result[static_cast<std::size_t>(at - seen.begin())].push_back(static_cast<int>(i));
    }
    return result;
}

std::vector<SamplePad> SampleTable::pads() const
{
    std::vector<SamplePad> result;
    for (const auto& group : groups())
    {
        const auto& first = rows[static_cast<std::size_t>(group.front())];
        SamplePad pad;
        pad.keys = keysOf(first);
        if (const auto* f = find(first, "file")) pad.file = *f;
        pad.name = find(first, "name") != nullptr ? *find(first, "name") : stem(pad.file);
        pad.levelDb = static_cast<float>(numberOf(find(first, "gain"), 0.0));
        pad.pan = static_cast<float>(numberOf(find(first, "pan"), 0.0));
        pad.zones = static_cast<int>(group.size());
        pad.lowKey = static_cast<int>(numberOf(&pad.keys, 0.0));
        result.push_back(std::move(pad));
    }
    return result;
}

bool SampleTable::setLevel(int pad, float levelDb)
{
    const auto g = groups();
    if (pad < 0 || pad >= static_cast<int>(g.size())) return false;
    const auto& first = rows[static_cast<std::size_t>(g[static_cast<std::size_t>(pad)].front())];
    const double delta = static_cast<double>(levelDb) - numberOf(find(first, "gain"), 0.0);
    for (const int index : g[static_cast<std::size_t>(pad)])
    {
        auto& row = rows[static_cast<std::size_t>(index)];
        set(row, "gain", number(numberOf(find(row, "gain"), 0.0) + delta));
    }
    return true;
}

bool SampleTable::setPan(int pad, float pan)
{
    const auto g = groups();
    if (pad < 0 || pad >= static_cast<int>(g.size())) return false;
    for (const int index : g[static_cast<std::size_t>(pad)])
        set(rows[static_cast<std::size_t>(index)], "pan", number(std::clamp(pan, -1.0f, 1.0f)));
    return true;
}

bool SampleTable::setName(int pad, const std::string& name)
{
    const auto g = groups();
    if (pad < 0 || pad >= static_cast<int>(g.size())) return false;
    for (const int index : g[static_cast<std::size_t>(pad)])
        set(rows[static_cast<std::size_t>(index)], "name", name);
    return true;
}

bool SampleTable::setKeys(int pad, const std::string& keys)
{
    const auto g = groups();
    if (pad < 0 || pad >= static_cast<int>(g.size()) || trim(keys).empty()) return false;
    for (const int index : g[static_cast<std::size_t>(pad)])
    {
        auto& row = rows[static_cast<std::size_t>(index)];
        row.words.erase(std::remove_if(row.words.begin(), row.words.end(),
                                       [](const auto& w) { return w.first == "key" || w.first == "keys"; }),
                        row.words.end());
        row.words.emplace(row.words.begin() + (row.words.empty() ? 0 : 1), keys.find('-', 1) == std::string::npos ? "key" : "keys", trim(keys));
    }
    return true;
}

bool SampleTable::setFile(int pad, const std::string& file)
{
    const auto g = groups();
    if (pad < 0 || pad >= static_cast<int>(g.size())) return false;
    const auto* oldFile = find(rows[static_cast<std::size_t>(g[static_cast<std::size_t>(pad)].front())], "file");
    const std::string old = oldFile != nullptr ? *oldFile : std::string();
    for (const int index : g[static_cast<std::size_t>(pad)])
    {
        auto& row = rows[static_cast<std::size_t>(index)];
        if (const auto* f = find(row, "file"); f == nullptr || *f == old)
            set(row, "file", file);
    }
    return true;
}

bool SampleTable::keysCover(const std::string& keys, int key)
{
    const auto dash = keys.find('-', 1);
    const int low = static_cast<int>(numberOf(&keys, 0.0));
    const int high = dash == std::string::npos ? low : static_cast<int>(std::strtod(keys.c_str() + dash + 1, nullptr));
    return key >= low && key <= high;
}

int SampleTable::addPad(const std::string& file, const std::string& name, const std::string& keys, int fromKey)
{
    std::string chosen = keys;
    if (chosen.empty())
    {
        int key = std::clamp(fromKey, 0, 127);
        const auto taken = [&](int k)
        {
            for (const auto& group : groups())
                if (keysCover(keysOf(rows[static_cast<std::size_t>(group.front())]), k)) return true;
            return false;
        };
        while (key < 127 && taken(key)) ++key;
        chosen = std::to_string(key);
    }

    Row row;
    row.words.emplace_back("file", file);
    row.words.emplace_back(chosen.find('-', 1) == std::string::npos ? "key" : "keys", chosen);
    row.words.emplace_back("name", name.empty() ? stem(file) : name);
    rows.push_back(std::move(row));

    const auto g = groups();
    for (std::size_t i = 0; i < g.size(); ++i)
        if (keysOf(rows[static_cast<std::size_t>(g[i].front())]) == chosen) return static_cast<int>(i);
    return static_cast<int>(g.size()) - 1;
}

bool SampleTable::removePad(int pad)
{
    const auto g = groups();
    if (pad < 0 || pad >= static_cast<int>(g.size())) return false;
    auto doomed = g[static_cast<std::size_t>(pad)];
    std::sort(doomed.rbegin(), doomed.rend());
    for (const int index : doomed)
        rows.erase(rows.begin() + index);
    return true;
}

} // namespace valis
