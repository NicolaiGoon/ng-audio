#include "SampleIndex.h"

namespace ng
{
namespace
{
    juce::String keyFor (const juce::File& folder, const juce::String& wildcard)
    {
        return folder.getFullPathName() + "|" + wildcard;
    }
}

SampleIndex::SampleIndex() = default;

SampleIndex::~SampleIndex()
{
    pool.removeAllJobs (true, 5000);
}

std::shared_ptr<const SampleIndex::Files> SampleIndex::list (const juce::File& folder, const juce::String& wildcard)
{
    auto files = std::make_shared<Files>();

    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, wildcard, juce::File::findFiles))
    {
        auto file = entry.getFile();
        files->push_back ({ file, file.getRelativePathFrom (folder).toLowerCase() });
    }

    return files;
}

std::shared_ptr<const SampleIndex::Files> SampleIndex::getFiles (const juce::File& folder, const juce::String& wildcard)
{
    auto key = keyFor (folder, wildcard);
    auto now = juce::Time::getMillisecondCounter();

    {
        const juce::ScopedLock sl (lock);

        if (auto it = listings.find (key); it != listings.end() && it->second.files != nullptr)
        {
            if (! it->second.refreshing && now - it->second.listedAt > (juce::uint32) refreshAfterMs)
                listInBackground (key, folder, wildcard);

            return it->second.files;
        }
    }

    // Not listed yet: list it now, so the answer is complete.
    auto files = list (folder, wildcard);
    store (key, files, now);
    return files;
}

void SampleIndex::warmUp (const juce::StringArray& folders, const juce::String& wildcard)
{
    const juce::ScopedLock sl (lock);

    for (auto& path : folders)
    {
        if (path.isEmpty())
            continue;

        juce::File folder (path);
        auto key = keyFor (folder, wildcard);

        if (auto it = listings.find (key); it == listings.end() || (it->second.files == nullptr && ! it->second.refreshing))
            listInBackground (key, folder, wildcard);
    }
}

void SampleIndex::clear()
{
    const juce::ScopedLock sl (lock);
    listings.clear();
}

void SampleIndex::listInBackground (const juce::String& key, const juce::File& folder, const juce::String& wildcard)
{
    // Called with the lock held.
    listings[key].refreshing = true;

    pool.addJob ([this, key, folder, wildcard]
    {
        auto startedAt = juce::Time::getMillisecondCounter();
        store (key, list (folder, wildcard), startedAt);
    });
}

void SampleIndex::store (const juce::String& key, std::shared_ptr<const Files> files, juce::uint32 listedAt)
{
    const juce::ScopedLock sl (lock);
    auto& listing = listings[key];
    listing.refreshing = false;

    // A listing started before the one already kept is older; keep the newer one.
    if (listing.files == nullptr || (juce::int32) (listedAt - listing.listedAt) >= 0)
    {
        listing.files = std::move (files);
        listing.listedAt = listedAt;
    }
}

} // namespace ng
