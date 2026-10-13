#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::prop;
    using params::schema;

    // Tracktion's sampler holds this many sounds.
    constexpr int maxSamples = 64;

    // Kits start on C1 (36), where General MIDI puts the kick.
    constexpr int firstKitNote = 36;

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
    {
        auto obj = new juce::DynamicObject();

        for (auto& [name, value] : properties)
            obj->setProperty (name, value);

        return juce::var (obj);
    }

    bool isNumber (const juce::var& v)
    {
        return v.isDouble() || v.isInt() || v.isInt64();
    }

    juce::Array<juce::ValueTree> getSounds (te::SamplerPlugin& sampler)
    {
        juce::Array<juce::ValueTree> sounds;

        for (auto v : sampler.state)
            if (v.hasType (te::IDs::SOUND))
                sounds.add (v);

        return sounds;
    }

    struct SamplerTarget
    {
        te::AudioTrack* channel = nullptr;
        te::SamplerPlugin* sampler = nullptr;
        juce::String error;
    };

    SamplerTarget findSampler (Session& session, const juce::String& command, const juce::var& p)
    {
        SamplerTarget t;
        auto ref = params::getString (p, "channel");
        t.channel = session.findTrack (ref, Session::Role::channel);

        if (t.channel == nullptr)
            t.error = command + ": no channel called '" + ref + "'";
        else if ((t.sampler = Session::getSampler (*t.channel)) == nullptr)
            t.error = command + ": '" + t.channel->getName() + "' doesn't play the sampler (set_instrument to \"sampler\" first)";

        return t;
    }

    /** A sample by its position (from 0) or its name. Returns -1 and sets the error if there's none. */
    int findSample (te::SamplerPlugin& sampler, const juce::String& command, const juce::var& ref, juce::String& error)
    {
        auto sounds = getSounds (sampler);

        if (isNumber (ref))
        {
            auto index = static_cast<int> (ref);

            if (juce::isPositiveAndBelow (index, sounds.size()))
                return index;

            error = command + ": there's no sample " + ref.toString() + " (the sampler has " + juce::String (sounds.size()) + ")";
            return -1;
        }

        juce::Array<int> matches;

        for (int i = 0; i < sounds.size(); ++i)
            if (sounds[i][te::IDs::name].toString().equalsIgnoreCase (ref.toString()))
                matches.add (i);

        if (matches.size() == 1)
            return matches.getFirst();

        // An index sent as text ("2"), when no sample has that name.
        if (matches.isEmpty() && ref.toString().containsOnly ("0123456789") && ref.toString().isNotEmpty())
            return findSample (sampler, command, ref.toString().getIntValue(), error);

        error = matches.isEmpty() ? command + ": no sample called '" + ref.toString() + "'"
                                  : command + ": there are " + juce::String (matches.size()) + " samples called '"
                                      + ref.toString() + "'; use its index";
        return -1;
    }

    bool isKeyUsed (te::SamplerPlugin& sampler, int note)
    {
        for (auto& v : getSounds (sampler))
            if (static_cast<int> (v[te::IDs::minNote]) <= note && note <= static_cast<int> (v[te::IDs::maxNote]))
                return true;

        return false;
    }

    /** The lowest key from C1 up that no sample plays on, then below C1; -1 when all are taken. */
    int findFreeKey (te::SamplerPlugin& sampler)
    {
        for (int note = firstKitNote; note < 128; ++note)
            if (! isKeyUsed (sampler, note))
                return note;

        for (int note = firstKitNote; --note >= 0;)
            if (! isKeyUsed (sampler, note))
                return note;

        return -1;
    }

    juce::String checkNote (const juce::var& v, const char* name)
    {
        if (v.isVoid())
            return {};

        if (! isNumber (v) || static_cast<double> (v) != std::floor (static_cast<double> (v)))
            return juce::String (name) + " must be a MIDI note number";

        if (! juce::isPositiveAndBelow (static_cast<int> (v), 128))
            return juce::String (name) + " must be between 0 and 127";

        return {};
    }

    /** Checks note, min_note, max_note, gain_db, pan, one_shot and name, as add_sample and
        set_sample take them. */
    juce::String checkSampleParams (const juce::var& p)
    {
        for (auto name : { "note", "min_note", "max_note" })
            if (auto error = checkNote (p[name], name); error.isNotEmpty())
                return error;

        auto gain = p["gain_db"], pan = p["pan"], oneShot = p["one_shot"];

        if (! gain.isVoid() && (! isNumber (gain) || std::abs (static_cast<double> (gain)) > 48.0))
            return "gain_db must be a number from -48 to 48";

        if (! pan.isVoid() && (! isNumber (pan) || std::abs (static_cast<double> (pan)) > 1.0))
            return "pan must be a number from -1 (left) to 1 (right)";

        if (! oneShot.isVoid() && ! oneShot.isBool())
            return "one_shot is true or false";

        if (p.hasProperty ("name") && p["name"].toString().trim().isEmpty())
            return "name can't be empty";

        return {};
    }

    /** Applies the params set_sample (and add_sample) take to a sound. A sample played on one
        key (the usual case) moves its whole range with its note. */
    void applySampleParams (te::SamplerPlugin& sampler, int index, const juce::var& p)
    {
        auto v = getSounds (sampler)[index];
        int note = v[te::IDs::keyNote], minNote = v[te::IDs::minNote], maxNote = v[te::IDs::maxNote];

        if (! p["note"].isVoid())
        {
            auto newNote = static_cast<int> (p["note"]);

            if (minNote == maxNote && minNote == note)
                minNote = maxNote = newNote;

            note = newNote;
        }

        if (! p["min_note"].isVoid())   minNote = static_cast<int> (p["min_note"]);
        if (! p["max_note"].isVoid())   maxNote = static_cast<int> (p["max_note"]);

        sampler.setSoundParams (index, note, minNote, maxNote);

        if (p.hasProperty ("gain_db") || p.hasProperty ("pan"))
            sampler.setSoundGains (index,
                                   static_cast<float> (params::getDouble (p, "gain_db", v[te::IDs::gainDb])),
                                   static_cast<float> (params::getDouble (p, "pan", v[te::IDs::pan])));

        if (! p["one_shot"].isVoid())
            sampler.setSoundOpenEnded (index, static_cast<bool> (p["one_shot"]));

        if (p.hasProperty ("name"))
            sampler.setSoundName (index, p["name"].toString().trim());
    }

    /** An error if the file isn't an audio file the sampler can read. */
    juce::String checkAudioFile (Session& session, const juce::File& file)
    {
        if (! file.existsAsFile())
            return "no file at '" + file.getFullPathName() + "'";

        auto& formats = session.getEngine().getAudioFileFormatManager().readFormatManager;
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return "'" + file.getFileName() + "' isn't an audio file the sampler can read (" + session.getAudioFileWildcard() + ")";

        return {};
    }

    /** Adds a sound for the file on a key, as a one-shot unless the params say otherwise.
        Returns its index, or -1 and sets the error. */
    int addSample (Session& session, te::SamplerPlugin& sampler, const juce::File& file, int note,
                   const juce::var& p, juce::String& error)
    {
        if (getSounds (sampler).size() >= maxSamples)
        {
            error = "a sampler holds up to " + juce::String (maxSamples) + " samples";
            return -1;
        }

        if (error = checkAudioFile (session, file); error.isNotEmpty())
            return -1;

        error = sampler.addSound (file.getFullPathName(), file.getFileNameWithoutExtension(), 0.0, 0.0, 0.0f);

        if (error.isNotEmpty())
            return -1;

        auto index = getSounds (sampler).size() - 1;

        // Every setting is written out, so undoing a later change always has a value to go back to.
        sampler.setSoundParams (index, note, note, note);
        sampler.setSoundGains (index, 0.0f, 0.0f);
        sampler.setSoundOpenEnded (index, true);
        applySampleParams (sampler, index, p);
        return index;
    }

    juce::var describeSample (te::SamplerPlugin& sampler, const te::AudioTrack& channel, int index)
    {
        auto sample = Session::describeSamples (sampler)[index];

        if (auto obj = sample.getDynamicObject())
            obj->setProperty ("channel", channel.itemID.toString());

        return sample;
    }

    //==============================================================================
    CommandResult addSampleCommand (Session& session, const juce::var& p)
    {
        auto t = findSampler (session, "add_sample", p);

        if (t.error.isNotEmpty())
            return CommandResult::failure (t.error);

        if (auto error = checkSampleParams (p); error.isNotEmpty())
            return CommandResult::failure ("add_sample: " + error);

        auto path = params::getString (p, "file");

        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            return CommandResult::failure ("add_sample: file is the full path to an audio file");

        auto note = p["note"].isVoid() ? findFreeKey (*t.sampler) : static_cast<int> (p["note"]);

        if (note < 0)
            return CommandResult::failure ("add_sample: every key has a sample; give a note to share one");

        juce::String error;
        auto index = addSample (session, *t.sampler, juce::File (path), note, p, error);

        if (index < 0)
            return CommandResult::failure ("add_sample: " + error);

        return CommandResult::success (describeSample (*t.sampler, *t.channel, index));
    }

    CommandResult setSampleCommand (Session& session, const juce::var& p)
    {
        auto t = findSampler (session, "set_sample", p);

        if (t.error.isNotEmpty())
            return CommandResult::failure (t.error);

        juce::String error;
        auto index = findSample (*t.sampler, "set_sample", p["sample"], error);

        if (index < 0)
            return CommandResult::failure (error);

        if (error = checkSampleParams (p); error.isNotEmpty())
            return CommandResult::failure ("set_sample: " + error);

        if (p.hasProperty ("file"))
        {
            auto path = params::getString (p, "file");

            if (! juce::File::isAbsolutePath (path))
                return CommandResult::failure ("set_sample: file is the full path to an audio file");

            if (error = checkAudioFile (session, juce::File (path)); error.isNotEmpty())
                return CommandResult::failure ("set_sample: " + error);

            t.sampler->setSoundMedia (index, path);
        }

        applySampleParams (*t.sampler, index, p);
        return CommandResult::success (describeSample (*t.sampler, *t.channel, index));
    }

    CommandResult removeSampleCommand (Session& session, const juce::var& p)
    {
        auto t = findSampler (session, "remove_sample", p);

        if (t.error.isNotEmpty())
            return CommandResult::failure (t.error);

        juce::String error;
        auto index = findSample (*t.sampler, "remove_sample", p["sample"], error);

        if (index < 0)
            return CommandResult::failure (error);

        auto name = getSounds (*t.sampler)[index][te::IDs::name].toString();
        t.sampler->state.removeChild (getSounds (*t.sampler)[index], &session.getUndoManager());

        return CommandResult::success (object ({ { "channel", t.channel->itemID.toString() }, { "removed", name },
                                                 { "samples", getSounds (*t.sampler).size() } }));
    }

    //==============================================================================
    /** The General MIDI drum key a file name suggests (e.g. "Kick 01" plays on 36), or -1. */
    int guessDrumKey (const juce::String& fileName)
    {
        juce::StringArray words;

        // Split on anything but letters, and where lower case turns to upper ("OpenHat").
        juce::String word;

        for (auto c : fileName)
        {
            auto isLetter = juce::CharacterFunctions::isLetter (c);

            if (! isLetter || (juce::CharacterFunctions::isUpperCase (c) && word.isNotEmpty()
                                 && juce::CharacterFunctions::isLowerCase (word.getLastCharacter())))
            {
                if (word.isNotEmpty())
                    words.add (word.toLowerCase());

                word.clear();
            }

            if (isLetter)
                word += c;
        }

        if (word.isNotEmpty())
            words.add (word.toLowerCase());

        auto has = [&] (std::initializer_list<const char*> options)
        {
            for (auto& w : words)
                for (auto o : options)
                    if (w == o || (std::strlen (o) > 3 && w.startsWith (o)))
                        return true;

            return false;
        };

        auto isHat = has ({ "hat", "hihat", "hh", "hats" });

        if ((isHat && has ({ "open", "op", "o" })) || has ({ "oh", "ohh", "ohat", "openhat" }))    return 46;
        if (isHat || has ({ "ch", "chh", "chat", "closedhat" }))                            return 42;
        if (has ({ "kick", "bd", "bassdrum", "kik" }))                                     return 36;
        if (has ({ "rim", "rimshot", "sidestick", "stick" }))                              return 37;
        if (has ({ "snare", "sd", "snr" }))                                                 return 38;
        if (has ({ "clap", "cp", "clp" }))                                                  return 39;

        if (has ({ "tom", "toms" }))
        {
            if (has ({ "floor", "low", "lo", "lt" }))   return 45;
            if (has ({ "high", "hi", "ht" }))           return 50;
            return 47;
        }

        if (has ({ "crash", "cym", "cymbal" }))         return 49;
        if (has ({ "ride" }))                           return 51;
        if (has ({ "tamb", "tambourine" }))             return 54;
        if (has ({ "cowbell", "bell", "cb" }))          return 56;
        if (has ({ "conga" }))                          return 63;
        if (has ({ "shaker", "shk", "maraca", "maracas" }))  return 70;

        return -1;
    }

    CommandResult loadDrumKit (Session& session, const juce::var& p)
    {
        auto t = findSampler (session, "load_drum_kit", p);

        if (t.error.isNotEmpty())
            return CommandResult::failure (t.error);

        auto path = params::getString (p, "folder");

        if (path.isEmpty() || ! juce::File::isAbsolutePath (path) || ! juce::File (path).isDirectory())
            return CommandResult::failure ("load_drum_kit: folder is the full path to a folder of samples");

        if (! p["replace"].isVoid() && ! p["replace"].isBool())
            return CommandResult::failure ("load_drum_kit: replace is true or false");

        // The samples in the folder itself or, for a kit kept in folders like "Kicks" and
        // "Snares", in the folders inside it.
        juce::File folder (path);
        auto wildcard = session.getAudioFileWildcard();
        auto files = folder.findChildFiles (juce::File::findFiles, false, wildcard);

        if (files.isEmpty())
            files = folder.findChildFiles (juce::File::findFiles, true, wildcard);

        if (files.isEmpty())
            return CommandResult::failure ("load_drum_kit: no samples in '" + folder.getFullPathName() + "' (looked for " + wildcard + ")");

        struct NaturalOrder
        {
            static int compareElements (const juce::File& a, const juce::File& b)
            {
                return a.getFullPathName().compareNatural (b.getFullPathName());
            }
        } order;

        files.sort (order);

        auto replace = p["replace"].isVoid() || static_cast<bool> (p["replace"]);
        auto& um = session.getUndoManager();

        if (replace)
            for (auto& v : getSounds (*t.sampler))
                t.sampler->state.removeChild (v, &um);

        if (getSounds (*t.sampler).size() + files.size() > maxSamples)
            return CommandResult::failure ("load_drum_kit: '" + folder.getFileName() + "' has " + juce::String (files.size())
                                             + " samples and a sampler holds up to " + juce::String (maxSamples)
                                             + "; pick the folder of one kit");

        // Samples whose names say what they are go on their General MIDI keys (kick on C1,
        // snare on D1, ...) when free; the rest fill the free keys from C1 up, in name order.
        juce::Array<int> keys;

        for (auto& file : files)
        {
            auto key = guessDrumKey (file.getFileNameWithoutExtension());
            keys.add (key >= 0 && ! keys.contains (key) && ! isKeyUsed (*t.sampler, key) ? key : -1);
        }

        juce::Array<juce::var> loaded;

        for (int i = 0; i < files.size(); ++i)
        {
            auto key = keys[i];

            if (key < 0)
            {
                // The lowest key nothing has and no named sample is waiting for.
                for (int note = firstKitNote; note < 128 && key < 0; ++note)
                    if (! isKeyUsed (*t.sampler, note) && ! keys.contains (note))
                        key = note;

                if (key < 0)
                    return CommandResult::failure ("load_drum_kit: ran out of keys for '" + files[i].getFileName() + "'");

                keys.set (i, key);
            }

            juce::String error;
            auto index = addSample (session, *t.sampler, files[i], key, {}, error);

            if (index < 0)
                return CommandResult::failure ("load_drum_kit: " + error);

            loaded.add (describeSample (*t.sampler, *t.channel, index));
        }

        return CommandResult::success (object ({ { "channel", t.channel->itemID.toString() }, { "loaded", loaded } }));
    }
}

void registerSamplerCommands (CommandRegistry& registry)
{
    auto settings = [] (std::vector<std::pair<juce::String, juce::var>> props)
    {
        props.push_back ({ "note", prop ("integer", "MIDI note it plays at its own pitch on (60 is middle C, 36 a kick's C1). "
                                                    "A sample on one key moves with it") });
        props.push_back ({ "min_note", prop ("integer", "Lowest key it plays on, pitched from note (default: note)") });
        props.push_back ({ "max_note", prop ("integer", "Highest key it plays on (default: note)") });
        props.push_back ({ "gain_db", prop ("number", "Level in dB, -48 to 48 (default 0)") });
        props.push_back ({ "pan", prop ("number", "Pan, -1 (left) to 1 (right) (default 0)") });
        props.push_back ({ "one_shot", prop ("boolean", "True (default) to play to the end whatever the note's length, "
                                                        "as drums do; false to stop when the note ends") });
        props.push_back ({ "name", prop ("string", "Name shown in the sampler (default: the file's name)") });
        return props;
    };

    auto toSchema = [] (const std::vector<std::pair<juce::String, juce::var>>& props, juce::StringArray required)
    {
        auto properties = new juce::DynamicObject();

        for (auto& [name, value] : props)
            properties->setProperty (name, value);

        auto s = schema ({}, required);

        if (auto obj = s.getDynamicObject())
            obj->setProperty ("properties", juce::var (properties));

        return s;
    };

    registry.add ({ "add_sample",
                    "Adds an audio file to a channel's sampler (set_instrument to \"sampler\" first). By default it's a "
                    "one-shot on the lowest free key from C1 (36) up, as for a drum kit; give note, min_note and max_note "
                    "to play it pitched across a range of keys instead.",
                    toSchema (settings ({ { "channel", prop ("string", "Channel id or name") },
                                          { "file", prop ("string", "Full path to the audio file (WAV, AIFF, FLAC, Ogg or MP3)") } }),
                              { "channel", "file" }),
                    true, addSampleCommand });

    registry.add ({ "set_sample",
                    "Changes a sample in a channel's sampler: its keys, level, pan, one-shot, name or file.",
                    toSchema (settings ({ { "channel", prop ("string", "Channel id or name") },
                                          { "sample", prop ("string", "The sample's index (from 0, as get_state lists them) or name") },
                                          { "file", prop ("string", "Full path to a different audio file to play") } }),
                              { "channel", "sample" }),
                    true, setSampleCommand });

    registry.add ({ "remove_sample", "Removes a sample from a channel's sampler.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "sample", prop ("string", "The sample's index (from 0) or name") } },
                            { "channel", "sample" }),
                    true, removeSampleCommand });

    registry.add ({ "load_drum_kit",
                    "Loads a folder of samples into a channel's sampler as a drum kit, one one-shot per key. Samples "
                    "named for what they are (kick, snare, clap, hat, open hat, tom, crash, ride...) go on their General "
                    "MIDI keys (kick 36, snare 38, closed hat 42...); the rest fill the free keys from 36 up, in name order. "
                    "Looks in folders inside the folder when it has no samples itself.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "folder", prop ("string", "Full path to the kit's folder") },
                              { "replace", prop ("boolean", "True (default) to replace the sampler's samples; false to add to them") } },
                            { "channel", "folder" }),
                    true, loadDrumKit });
}

} // namespace ng
