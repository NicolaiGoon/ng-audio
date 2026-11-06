#include "Autosave.h"

namespace ng
{
namespace
{
    juce::String lockName (const juce::String& id)
    {
        return "NgDawAutosave_" + id;
    }

    /** The autosaves this process is writing, which its own locks can't tell apart from
        abandoned ones on every platform. */
    juce::StringArray& liveIds()
    {
        static juce::StringArray ids;
        return ids;
    }
}

Autosave::Autosave (Session& s, const juce::File& folder)
    : session (s)
{
    juce::Random random;
    random.setSeedRandomly();
    id = juce::String::toHexString (random.nextInt64()).paddedLeft ('0', 16);
    projectFile = folder.getChildFile (id + projectFileExtension);
    infoFile = folder.getChildFile (id + ".json");

    lock = std::make_unique<juce::InterProcessLock> (lockName (id));
    lock->enter (0);
    liveIds().add (id);
}

Autosave::~Autosave()
{
    discard();
    liveIds().removeString (id);
    lock->exit();
}

juce::File Autosave::getDefaultFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    folder = folder.getChildFile ("Application Support");
   #endif

    return folder.getChildFile ("Ng DAW").getChildFile ("Autosave");
}

bool Autosave::saveIfNeeded()
{
    if (! session.hasUnsavedChanges())
    {
        discard();
        return false;
    }

    auto state = session.getComparableState();

    if (state == lastWritten && projectFile.existsAsFile())
        return false;

    if (session.writeCopy (projectFile).isNotEmpty())
        return false;

    auto info = new juce::DynamicObject();
    info->setProperty ("id", id);
    info->setProperty ("project", session.getFile().getFullPathName());
    info->setProperty ("name", session.getProjectName());
    info->setProperty ("time", juce::Time::getCurrentTime().toISO8601 (true));
    infoFile.replaceWithText (juce::JSON::toString (juce::var (info)));

    // Writing brings plugins' own settings into the project, so remember it as written.
    lastWritten = session.getComparableState();
    return true;
}

void Autosave::discard()
{
    projectFile.deleteFile();
    infoFile.deleteFile();
    lastWritten = {};
}

std::vector<Autosave::Recovery> Autosave::findRecoverable (const juce::File& folder)
{
    std::vector<Recovery> found;

    for (auto& info : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        auto json = juce::JSON::parse (info);
        auto autosaveId = json["id"].toString();
        auto autosave = folder.getChildFile (autosaveId + projectFileExtension);

        if (autosaveId.isEmpty() || ! autosave.existsAsFile() || liveIds().contains (autosaveId))
            continue;

        // A copy of the app that's still running holds its lock.
        juce::InterProcessLock other (lockName (autosaveId));

        if (! other.enter (0))
            continue;

        other.exit();

        auto project = json["project"].toString();
        found.push_back ({ autosave, info, project.isNotEmpty() ? juce::File (project) : juce::File(),
                           json["name"].toString(), juce::Time::fromISO8601 (json["time"].toString()) });
    }

    std::sort (found.begin(), found.end(), [] (auto& a, auto& b) { return a.time > b.time; });
    return found;
}

juce::String Autosave::recover (Session& session, const Recovery& recovery)
{
    auto error = session.recoverProject (recovery.autosave, recovery.originalProject);

    if (error.isEmpty())
        remove (recovery);

    return error;
}

void Autosave::remove (const Recovery& recovery)
{
    recovery.autosave.deleteFile();
    recovery.info.deleteFile();
}

} // namespace ng
