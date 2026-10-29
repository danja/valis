// tests/ops/CircuitHistoryTest.cpp
//
// valis::CircuitHistory: a list of versions with a cursor.

#include "valis/CircuitHistory.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace valis;

namespace {

void testRecordingAndUndoAndRedo()
{
    CircuitHistory h;
    assert(h.empty() && h.current() == nullptr && ! h.canUndo() && ! h.canRedo());
    assert(h.undo() == nullptr && h.redo() == nullptr);

    h.sync("one");
    assert(h.current()->turtle == "one" && h.current()->label == "starting point" && h.current()->id == 1);

    assert(h.record("add b", "two"));
    assert(h.record("add c", "three"));
    assert(h.current()->turtle == "three" && h.versions().size() == 3);
    assert(h.canUndo() && ! h.canRedo());

    assert(h.undo()->turtle == "two");
    assert(h.undo()->turtle == "one");
    assert(h.undo() == nullptr && h.current()->turtle == "one");
    assert(h.canRedo());

    assert(h.redo()->turtle == "two");
    assert(h.redo()->turtle == "three");
    assert(h.redo() == nullptr);
}

void testANewEditEndsTheRedo()
{
    CircuitHistory h;
    h.sync("a");
    h.record("b", "b");
    h.record("c", "c");
    h.undo();                       // at b
    assert(h.canRedo());

    h.record("d", "d");             // branches: c is gone
    assert(! h.canRedo());
    assert(h.versions().size() == 3 && h.current()->turtle == "d");
    assert(h.previous()->turtle == "b");
    for (const auto& v : h.versions())
        assert(v.turtle != "c");
}

void testTheSameTextAddsNothing()
{
    CircuitHistory h;
    h.sync("a");
    assert(! h.record("again", "a"));
    assert(h.versions().size() == 1);

    h.sync("a");                    // already current
    assert(h.versions().size() == 1);
}

void testSyncKeepsAnOutsideChange()
{
    CircuitHistory h;
    h.sync("a");
    h.record("b", "b");

    h.sync("typed in the editor");
    assert(h.versions().size() == 3);
    assert(h.current()->label == "changed outside these tools" && h.current()->turtle == "typed in the editor");
    assert(h.undo()->turtle == "b");   // and it can be undone
}

void testNumbersAreNeverReusedAndTheListIsBounded()
{
    CircuitHistory h;
    h.sync("0");
    for (int i = 1; i <= 250; ++i)
        h.record("edit", std::to_string(i));

    assert(h.versions().size() == CircuitHistory::kMaxVersions);
    assert(h.current()->turtle == "250");
    assert(h.current()->id == 251);                       // 1 for the start, then 250 edits
    assert(h.versions().front().id == 251 - 99);          // the oldest ones went
    assert(h.find(1) == nullptr && h.find(251) != nullptr);

    // Undo still walks the whole of what is kept.
    int steps = 0;
    while (h.undo()) ++steps;
    assert(steps == 99);
}

}  // namespace

int main()
{
    testRecordingAndUndoAndRedo();
    testANewEditEndsTheRedo();
    testTheSameTextAddsNothing();
    testSyncKeepsAnOutsideChange();
    testNumbersAreNeverReusedAndTheListIsBounded();

    std::puts("CircuitHistoryTest PASSED");
    return 0;
}
