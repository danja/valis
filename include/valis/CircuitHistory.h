// include/valis/CircuitHistory.h
//
// The versions a circuit has had, so that an edit can be taken back and two states compared.
// A list of the circuit's source text with a cursor on the one that is current: an edit adds a
// version after the cursor and drops anything the cursor had been moved back past, as an editor's
// undo does.
//
// Owned by whoever owns the circuit (the plugin) and lent to the ops, which are made afresh for
// each call and so cannot keep state of their own. Message thread only.

#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace valis {

struct CircuitVersion
{
    int id = 0;            ///< counts up from 1 and is never reused, so a number names one version
    std::string label;     ///< what produced it, such as "connect osc.out to vca.in"
    std::string turtle;
};

class CircuitHistory
{
public:
    /// The most versions kept. The oldest go first.
    static constexpr std::size_t kMaxVersions = 100;

    bool empty() const noexcept { return versions_.empty(); }
    const std::vector<CircuitVersion>& versions() const noexcept { return versions_; }

    const CircuitVersion* current() const noexcept
    {
        return versions_.empty() ? nullptr : &versions_[cursor_];
    }

    const CircuitVersion* find(int id) const noexcept
    {
        const auto it = std::find_if(versions_.begin(), versions_.end(), [id](const CircuitVersion& v) { return v.id == id; });
        return it != versions_.end() ? &*it : nullptr;
    }

    /// The version before the current one, or null.
    const CircuitVersion* previous() const noexcept
    {
        return cursor_ > 0 ? &versions_[cursor_ - 1] : nullptr;
    }

    bool canUndo() const noexcept { return cursor_ > 0; }
    bool canRedo() const noexcept { return ! versions_.empty() && cursor_ + 1 < versions_.size(); }

    /// Makes the current version match `turtle`. The first call starts the history; a later one that
    /// finds different text means the circuit was changed somewhere that does not record, such as
    /// the editor, and that state becomes a version of its own so it can be returned to.
    void sync(const std::string& turtle)
    {
        if (versions_.empty())
            push("starting point", turtle);
        else if (versions_[cursor_].turtle != turtle)
            record("changed outside these tools", turtle);
    }

    /// Adds a version after the current one. The same text as the current version adds nothing.
    /// Returns whether a version was added.
    bool record(const std::string& label, const std::string& turtle)
    {
        if (versions_.empty())
        {
            push(label, turtle);
            return true;
        }
        if (versions_[cursor_].turtle == turtle)
            return false;

        versions_.erase(versions_.begin() + static_cast<std::ptrdiff_t>(cursor_) + 1, versions_.end());
        push(label, turtle);
        return true;
    }

    /// Moves the cursor back one version and returns it, or null at the start.
    const CircuitVersion* undo() noexcept
    {
        if (! canUndo())
            return nullptr;
        return &versions_[--cursor_];
    }

    /// Moves the cursor forward one version and returns it, or null at the end.
    const CircuitVersion* redo() noexcept
    {
        if (! canRedo())
            return nullptr;
        return &versions_[++cursor_];
    }

private:
    void push(const std::string& label, const std::string& turtle)
    {
        versions_.push_back({nextId_++, label, turtle});
        cursor_ = versions_.size() - 1;

        if (versions_.size() > kMaxVersions)
        {
            versions_.erase(versions_.begin());
            cursor_ = versions_.size() - 1;
        }
    }

    std::vector<CircuitVersion> versions_;
    std::size_t cursor_ = 0;
    int nextId_ = 1;
};

}  // namespace valis
