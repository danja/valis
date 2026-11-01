// include/valis/Freesound.h
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace valis {

/// One sound from a Freesound search (https://freesound.org/docs/api/resources_apiv2.html#sound-instance).
struct FreesoundSound
{
    int id = 0;
    std::string name;
    std::string username;
    std::string license;     ///< the licence page URL Freesound reports
    std::string tags;        ///< space separated
    std::string pageUrl;     ///< https://freesound.org/s/<id>/
    double seconds = 0.0;
};

struct FreesoundQuery
{
    std::string text;
    std::string license = "cc0";   ///< "cc0", "cc-by", "any", or a raw Freesound licence name
    double minSeconds = 0.0;       ///< 0 means no bound
    double maxSeconds = 0.0;
    int pageSize = 15;             ///< 1 to 50
    std::string sort = "rating_desc";
};

/// Search and download over the Freesound API with an API key (token authentication).
/// A token cannot fetch the original upload, which needs OAuth2, so a download is the 192 kbps Ogg preview.
/// The fetch function is injected so the request and response handling are testable without a network.
class Freesound
{
public:
    /// GET `url`. `bearer` is sent as the API token and only to Freesound's own host. Fills `body` or `error`.
    using Fetch = std::function<bool(const std::string& url, const std::string& token, std::string& body, std::string& error)>;

    Freesound(std::string apiKey, Fetch fetch);

    /// The API key from the lines of a .env file (FREESOUND_API_KEY=...), empty if absent.
    static std::string keyFromEnv(const std::string& envText);

    static std::string searchUrl(const FreesoundQuery& query);
    static bool parseSearch(const std::string& json, std::vector<FreesoundSound>& sounds, std::string& error);

    /// The preview URL and sound details for one id.
    static bool parseSound(const std::string& json, FreesoundSound& sound, std::string& previewUrl, std::string& error);

    /// A file name safe on every system: lower case words joined by '-'.
    static std::string slug(const std::string& name);

    bool search(const FreesoundQuery& query, std::vector<FreesoundSound>& sounds, std::string& error) const;

    /// Fetches the preview of sound `id` into `directory` as <slug>-<id>.ogg with a <slug>-<id>.json beside it that
    /// records the author, licence and page, which the licence requires a user to be able to find again.
    bool download(int id, const std::string& directory, const std::string& name, std::string& pathWritten,
                  FreesoundSound& sound, std::string& error) const;

private:
    std::string apiKey;
    Fetch fetch;
};

/// Fetch over HTTPS (cpp-httplib with OpenSSL). Follows redirects. Sends the token only to freesound.org.
bool httpsFetch(const std::string& url, const std::string& token, std::string& body, std::string& error);

} // namespace valis
