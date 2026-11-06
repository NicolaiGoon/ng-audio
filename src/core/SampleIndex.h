#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <vector>

namespace ng
{

/**
    The audio files in the sample folders, listed once and kept, so find_samples searches a list
    in memory instead of walking the disk on every search (a big library can hold tens of
    thousands of files).

    The first search of a folder lists it there and then, unless warmUp() has already listed it
    in the background. After that a search uses the list it has, and if the list is more than a
    few seconds old it is listed again in the background for next time, so files added to a
    folder show up shortly after.
*/
class SampleIndex
{
public:
    SampleIndex();
    ~SampleIndex();

    struct Entry
    {
        juce::File file;
        juce::String searchText;   // the path below the folder, lower case
    };

    using Files = std::vector<Entry>;

    /** The files under the folder that match the wildcard (e.g. "*.wav;*.aif"). */
    std::shared_ptr<const Files> getFiles (const juce::File& folder, const juce::String& wildcard);

    /** Lists the folders in the background, if they aren't listed yet, so the first search is quick. */
    void warmUp (const juce::StringArray& folders, const juce::String& wildcard);

    /** Forgets every list, e.g. when the sample folders change. */
    void clear();

    /** How old a list can be before a search lists the folder again in the background. */
    static constexpr int refreshAfterMs = 10000;

private:
    struct Listing
    {
        std::shared_ptr<const Files> files;
        juce::uint32 listedAt = 0;         // when listing started (Time::getMillisecondCounter)
        bool refreshing = false;
    };

    static std::shared_ptr<const Files> list (const juce::File& folder, const juce::String& wildcard);
    void listInBackground (const juce::String& key, const juce::File& folder, const juce::String& wildcard);
    void store (const juce::String& key, std::shared_ptr<const Files>, juce::uint32 listedAt);

    juce::CriticalSection lock;
    std::map<juce::String, Listing> listings;   // by folder and wildcard
    juce::ThreadPool pool { juce::ThreadPoolOptions().withThreadName ("Sample index").withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE (SampleIndex)
};

} // namespace ng
