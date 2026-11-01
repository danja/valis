// src/net/HttpsFetch.cpp
#include "valis/Freesound.h"

#include <httplib.h>

namespace valis {

bool httpsFetch(const std::string& url, const std::string& token, std::string& body, std::string& error)
{
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos || url.compare(0, schemeEnd, "https") != 0)
    {
        error = "only https URLs are fetched: " + url;
        return false;
    }
    const auto pathStart = url.find('/', schemeEnd + 3);
    const auto host = url.substr(schemeEnd + 3, pathStart == std::string::npos ? std::string::npos : pathStart - schemeEnd - 3);
    const auto path = pathStart == std::string::npos ? std::string("/") : url.substr(pathStart);

    httplib::Client client("https://" + host);
    client.set_follow_location(true);
    client.set_connection_timeout(10);
    client.set_read_timeout(30);

    httplib::Headers headers;
    // The key goes to Freesound's own host and nowhere else, such as the CDN a preview redirects to.
    if (! token.empty() && (host == "freesound.org" || host == "www.freesound.org"))
        headers.emplace("Authorization", "Token " + token);

    const auto response = client.Get(path, headers);
    if (! response)
    {
        error = "cannot reach " + host + ": " + httplib::to_string(response.error());
        return false;
    }
    if (response->status != 200)
    {
        error = host + " answered HTTP " + std::to_string(response->status);
        return false;
    }
    body = response->body;
    return true;
}

} // namespace valis
