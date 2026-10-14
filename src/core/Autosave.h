#pragma once

#include "Session.h"

namespace ng
{

/**
    Keeps a copy of the open project's unsaved changes, so a crash loses at most a minute or so
    of work. Each running copy of the app writes its own autosave (the project, plus a small
    file saying which project it is) and holds a lock while it runs. An autosave whose lock
    nobody holds was left by a copy that didn't close properly, and can be recovered.

    The autosave goes when the project is saved, replaced, or the app closes normally.
*/
class Autosave
{
public:
    explicit Autosave (Session&, const juce::File& folder = getDefaultFolder());
    ~Autosave();

    /** "Autosave" in the user's Ng DAW settings folder. */
    static juce::File getDefaultFolder();

    /** Writes the project if it has unsaved changes it hasn't written yet. Returns true if it
        wrote it. Call it every so often, e.g. once a minute. */
    bool saveIfNeeded();

    /** Removes this copy's autosave, e.g. after the project is saved, closed or replaced. */
    void discard();

    /** An autosave left behind by a copy of the app that didn't close properly. */
    struct Recovery
    {
        juce::File autosave, info;
        juce::File originalProject;     // empty for a project that was never saved
        juce::String projectName;
        juce::Time time;
    };

    static std::vector<Recovery> findRecoverable (const juce::File& folder = getDefaultFolder());

    /** Opens a recovered autosave in the session (see Session::recoverProject) and removes it. */
    static juce::String recover (Session&, const Recovery&);

    /** Deletes an autosave nobody wants. */
    static void remove (const Recovery&);

    juce::File getAutosaveFile() const      { return projectFile; }

private:
    Session& session;
    juce::String id;
    juce::File projectFile, infoFile;
    std::unique_ptr<juce::InterProcessLock> lock;
    juce::String lastWritten;

    JUCE_DECLARE_NON_COPYABLE (Autosave)
};

} // namespace ng
