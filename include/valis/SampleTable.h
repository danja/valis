// include/valis/SampleTable.h
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace valis {

/// One instrument of a val:Sampler table, as a panel shows it: the zones that answer the same keys (velocity layers
/// and round-robin takes of one drum) taken together.
struct SamplePad
{
    std::string keys;        ///< "36" or "40-64", as written
    std::string name;        ///< name= of the first zone, else the first file's name without its extension
    std::string file;        ///< the first zone's file
    float levelDb = 0.0f;    ///< the first zone's gain
    float pan = 0.0f;        ///< the first zone's pan
    int   zones = 0;
    int   lowKey = 0;        ///< the lowest key it answers, for ordering and display
};

/// The text of a val:samples table as an editable list of zones. It does no I/O and opens no file, so the
/// control panel can change one drum's level and send the whole table back to the element, which reads the files.
/// Comments are kept where they are. A word this class does not know is kept as written, so a table that uses
/// features the panel has no control for (loops, start and end) survives an edit.
class SampleTable
{
public:
    /// Never fails: a line that is not key=value words is kept as a comment, so editing cannot destroy it.
    static SampleTable parse(const std::string& text);

    /// One zone per line, comments in place.
    std::string toString() const;

    static bool words(const std::string& line, std::vector<std::pair<std::string, std::string>>& out, std::string& error);

    std::vector<SamplePad> pads() const;
    int padCount() const { return static_cast<int>(pads().size()); }

    /// Change one instrument. A level change moves every zone of the pad by the same number of decibels, so the
    /// balance between its layers stays; a pan, name or key change sets every zone to the same value.
    /// All return false for a pad that does not exist.
    bool setLevel(int pad, float levelDb);
    bool setPan(int pad, float pan);
    bool setName(int pad, const std::string& name);
    bool setKeys(int pad, const std::string& keys);

    /// Every zone of the pad that used the pad's first file takes `file`.
    bool setFile(int pad, const std::string& file);

    /// Appends a pad on the first free key from `fromKey` up (or on `keys`, if given). Returns its index.
    int addPad(const std::string& file, const std::string& name, const std::string& keys = {}, int fromKey = 36);

    bool removePad(int pad);

private:
    struct Row
    {
        bool comment = false;
        std::string text;                                        ///< a comment's whole line
        std::vector<std::pair<std::string, std::string>> words;  ///< a zone's words, in order
    };

    std::vector<Row> rows;

    static std::string keysOf(const Row&);
    std::vector<std::vector<int>> groups() const;   ///< row indices of each pad, in order of first appearance
    static const std::string* find(const Row&, const std::string& key);
    static void set(Row&, const std::string& key, const std::string& value);
    static bool keysCover(const std::string& keys, int key);
};

}  // namespace valis
