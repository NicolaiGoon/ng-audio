#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::isNumber;
    using params::object;
    using params::roundBeats;
    using params::prop;
    using params::schema;

    CommandResult addMidiClip (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();
        auto channelRef = params::getString (p, "channel");
        auto channel = session.findTrack (channelRef, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("add_midi_clip: no channel called '" + channelRef + "'");

        if (Session::isAudioChannel (*channel))
            return CommandResult::failure ("add_midi_clip: '" + channel->getName() + "' is an audio channel; add audio to it with import_audio");

        auto startBeat = te::BeatPosition::fromBeats (params::getDouble (p, "start_beat", 0.0));
        auto lengthBeats = te::BeatDuration::fromBeats (params::getDouble (p, "length_beats", 4.0));

        if (lengthBeats <= te::BeatDuration())
            return CommandResult::failure ("add_midi_clip: length_beats must be positive");

        auto& tempo = edit.tempoSequence;
        te::TimeRange range (tempo.toTime (startBeat), tempo.toTime (startBeat + lengthBeats));

        auto clip = channel->insertMIDIClip (range, nullptr);

        if (clip == nullptr)
            return CommandResult::failure ("add_midi_clip: couldn't create the clip");

        if (auto notes = p["notes"].getArray())
        {
            for (auto& n : *notes)
            {
                auto pitch = static_cast<int> (n.getProperty ("pitch", 60));
                auto start = static_cast<double> (n.getProperty ("start", 0.0));
                auto length = static_cast<double> (n.getProperty ("length", 1.0));
                auto velocity = static_cast<int> (n.getProperty ("velocity", 100));

                if (! juce::isPositiveAndBelow (pitch, 128) || length <= 0.0)
                    return CommandResult::failure ("add_midi_clip: invalid note " + juce::JSON::toString (n, true));

                clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (start),
                                             te::BeatDuration::fromBeats (length),
                                             juce::jlimit (1, 127, velocity), 0, &session.getUndoManager());
            }
        }

        return CommandResult::success (object ({ { "clip", clip->itemID.toString() }, { "notes", clip->getSequence().getNotes().size() } }));
    }


    //==============================================================================
    // Step patterns, as in FL Studio: a pattern holds a step row (16th notes, one pitch) for
    // each channel. Placing it on the timeline puts a pattern clip on every channel with steps
    // in it, all starting together: one block. The clips are marked with their pattern and
    // rebuilt from the rows whenever the steps change, and moving, copying, resizing or
    // deleting any clip of a block does the same to the whole block.
    const juce::Identifier patternClipProp ("ngPattern"), patternBeatsProp ("ngPatternBeats");
    constexpr int maxStepCount = 64;
    constexpr double beatsPerStep = 0.25;

    double startBeatOf (Session& session, const te::Clip& clip)
    {
        auto beat = session.getEdit().tempoSequence.toBeats (clip.getPosition().getStart()).inBeats();
        return std::round (beat * 1.0e6) / 1.0e6;
    }

    juce::Array<te::MidiClip*> patternClipsOn (te::AudioTrack& channel, int pattern)
    {
        juce::Array<te::MidiClip*> clips;

        for (auto clip : channel.getClips())
            if (Session::getPatternOf (*clip) == pattern)
                if (auto midi = dynamic_cast<te::MidiClip*> (clip))
                    clips.add (midi);

        return clips;
    }

    /** The pattern's clips on every channel. */
    juce::Array<te::MidiClip*> patternClips (Session& session, int pattern)
    {
        juce::Array<te::MidiClip*> clips;

        for (auto channel : session.getChannels())
            clips.addArray (patternClipsOn (*channel, pattern));

        return clips;
    }

    /** The block a pattern clip belongs to: its pattern's clips that start where it does. */
    juce::Array<te::MidiClip*> blockOf (Session& session, const te::Clip& clip)
    {
        juce::Array<te::MidiClip*> block;
        auto start = startBeatOf (session, clip);

        for (auto c : patternClips (session, Session::getPatternOf (clip)))
            if (startBeatOf (session, *c) == start)
                block.add (c);

        return block;
    }

    /** Where the pattern is placed: each block's start, and its own length if it was resized. */
    std::map<double, juce::var> blocksOf (Session& session, int pattern)
    {
        std::map<double, juce::var> blocks;

        for (auto clip : patternClips (session, pattern))
        {
            auto& length = blocks[startBeatOf (session, *clip)];

            if (length.isVoid() && clip->state.hasProperty (patternBeatsProp))
                length = clip->state[patternBeatsProp];
        }

        return blocks;
    }

    /** Fills a pattern clip from its channel's row: the steps play once from its start, cut off
        if it was resized shorter; one that wasn't resized follows the row's length. */
    void fillPatternClip (Session& session, te::MidiClip& clip, te::AudioTrack& channel)
    {
        auto& tempo = session.getEdit().tempoSequence;
        auto* um = &session.getUndoManager();
        auto pattern = Session::getPatternOf (clip);
        auto steps = Session::getSteps (channel, pattern);
        auto pitch = Session::getStepPitch (channel, pattern);
        auto clipBeats = clip.state.hasProperty (patternBeatsProp) ? static_cast<double> (clip.state[patternBeatsProp])
                                                                    : steps.length() * beatsPerStep;
        auto startBeat = tempo.toBeats (clip.getPosition().getStart());
        clip.setPosition ({ { clip.getPosition().getStart(), tempo.toTime (startBeat + te::BeatDuration::fromBeats (clipBeats)) },
                            te::TimeDuration() });
        clip.getSequence().clear (um);

        for (int i = 0; i < steps.length(); ++i)
            if (auto beat = i * beatsPerStep; steps[i] == 'x' && beat < clipBeats - 1.0e-6)
                clip.getSequence().addNote (pitch, te::BeatPosition::fromBeats (beat),
                                            te::BeatDuration::fromBeats (beatsPerStep), 100, 0, um);
    }

    /** The channel's clip of the pattern's block at a start, made if it isn't there yet. */
    te::MidiClip* placeOn (Session& session, te::AudioTrack& channel, int pattern, double startBeat, const juce::var& ownLength)
    {
        for (auto clip : patternClipsOn (channel, pattern))
            if (startBeatOf (session, *clip) == std::round (startBeat * 1.0e6) / 1.0e6)
                return clip;

        auto& tempo = session.getEdit().tempoSequence;
        auto* um = &session.getUndoManager();
        auto info = session.findPattern (pattern).value_or (Session::PatternInfo());
        auto start = te::BeatPosition::fromBeats (startBeat);
        auto clip = channel.insertMIDIClip (info.name, { tempo.toTime (start), tempo.toTime (start + te::BeatDuration::fromBeats (4.0)) }, nullptr);

        if (clip == nullptr)
            return nullptr;

        clip->state.setProperty (patternClipProp, pattern, um);

        if (! ownLength.isVoid())
            clip->state.setProperty (patternBeatsProp, ownLength, um);

        if (! info.colour.isTransparent())
            session.setClipColour (*clip, info.colour);

        return clip.get();
    }

    /** Brings a channel's clips of a pattern in line with its row in it: a clip in each of the
        pattern's blocks while the row has steps, none when it's empty. Pattern 1, with no blocks
        anywhere, goes back at the start of the project, as the single pattern always has. */
    void rebuildPatternClip (Session& session, te::AudioTrack& channel, int pattern)
    {
        if (! Session::getSteps (channel, pattern).containsChar ('x'))
        {
            for (auto clip : patternClipsOn (channel, pattern))
                clip->removeFromParent();

            return;
        }

        auto blocks = blocksOf (session, pattern);

        if (blocks.empty() && pattern == 1)
            blocks[0.0] = juce::var();

        for (auto& [start, length] : blocks)
            placeOn (session, channel, pattern, start, length);

        for (auto clip : patternClipsOn (channel, pattern))
            fillPatternClip (session, *clip, channel);
    }

    CommandResult setSteps (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("set_steps: no channel called '" + ref + "'");

        if (Session::isAudioChannel (*channel))
            return CommandResult::failure ("set_steps: '" + channel->getName() + "' is an audio channel, which has no step pattern");

        auto pattern = p.hasProperty ("pattern") ? static_cast<int> (p["pattern"]) : session.getCurrentPattern();

        if (! session.findPattern (pattern))
            return CommandResult::failure ("set_steps: no pattern " + p["pattern"].toString() + " (get_state lists them)");

        auto steps = Session::getSteps (*channel, pattern);
        auto pitch = Session::getStepPitch (*channel, pattern);

        // The whole row, as a string ("x..." or "1000...") or an array of booleans.
        if (p.hasProperty ("steps"))
        {
            auto value = p["steps"];
            juce::String newSteps;

            if (auto arr = value.getArray())
            {
                for (auto& v : *arr)
                    newSteps << (static_cast<bool> (v) ? "x" : ".");
            }
            else
            {
                auto text = value.toString().removeCharacters (" |");

                if (! text.containsOnly ("x.10X-"))
                    return CommandResult::failure ("set_steps: steps must be a string of 'x' and '.' (or 1 and 0), or an array of booleans");

                for (auto c : text)
                    newSteps << ((c == 'x' || c == 'X' || c == '1') ? "x" : ".");
            }

            if (newSteps.isEmpty() || newSteps.length() > maxStepCount)
                return CommandResult::failure ("set_steps: a pattern has 1 to " + juce::String (maxStepCount) + " steps");

            steps = newSteps;
        }

        // Or change the length, keeping the steps that still fit.
        if (p.hasProperty ("length"))
        {
            auto length = static_cast<int> (p["length"]);

            if (length < 1 || length > maxStepCount)
                return CommandResult::failure ("set_steps: length must be 1 to " + juce::String (maxStepCount));

            steps = steps.substring (0, length).paddedRight ('.', length);
        }

        // Or switch single steps: {"step": 4, "on": true}.
        if (p.hasProperty ("step"))
        {
            auto index = static_cast<int> (p["step"]);

            if (! juce::isPositiveAndBelow (index, steps.length()))
                return CommandResult::failure ("set_steps: step must be 0 to " + juce::String (steps.length() - 1));

            auto on = p.hasProperty ("on") ? static_cast<bool> (p["on"]) : steps[index] != 'x';
            steps = steps.substring (0, index) + (on ? "x" : ".") + steps.substring (index + 1);
        }

        if (p.hasProperty ("pitch"))
        {
            pitch = static_cast<int> (p["pitch"]);

            if (! juce::isPositiveAndBelow (pitch, 128))
                return CommandResult::failure ("set_steps: pitch must be a MIDI note from 0 to 127");
        }

        session.setSteps (*channel, pattern, steps, pitch);
        rebuildPatternClip (session, *channel, pattern);

        auto obj = new juce::DynamicObject();
        obj->setProperty ("channel", channel->itemID.toString());
        obj->setProperty ("pattern", pattern);
        obj->setProperty ("steps", steps);
        obj->setProperty ("pitch", pitch);
        return CommandResult::success (juce::var (obj));
    }


    //==============================================================================
    /** A clip on any channel, by its id. */
    struct FoundClip
    {
        te::Clip* clip = nullptr;
        te::AudioTrack* channel = nullptr;
    };

    FoundClip findClip (Session& session, const juce::String& id)
    {
        for (auto channel : session.getChannels())
            for (auto clip : channel->getClips())
                if (clip->itemID.toString() == id)
                    return { clip, channel };

        return {};
    }

    juce::var describeClip (Session& session, te::Clip& clip)
    {
        auto& tempo = session.getEdit().tempoSequence;
        auto start = tempo.toBeats (clip.getPosition().getStart()).inBeats();
        auto end = tempo.toBeats (clip.getPosition().getEnd()).inBeats();

        auto obj = new juce::DynamicObject();
        obj->setProperty ("clip", clip.itemID.toString());

        if (auto track = clip.getTrack())
            obj->setProperty ("channel", track->itemID.toString());

        obj->setProperty ("start_beat", roundBeats (start));
        obj->setProperty ("length_beats", roundBeats (end - start));
        obj->setProperty ("muted", clip.isMuted());
        return juce::var (obj);
    }

    /** Looks up the "clip" param, or explains what's wrong. */
    std::optional<CommandResult> requireClip (Session& session, const juce::String& command,
                                              const juce::var& p, FoundClip& found)
    {
        auto id = params::getString (p, "clip");

        if (id.isEmpty())
            return CommandResult::failure (command + ": give the clip's id (from get_state)");

        found = findClip (session, id);

        if (found.clip == nullptr)
            return CommandResult::failure (command + ": no clip with id '" + id + "'");

        return std::nullopt;
    }

    constexpr double minClipBeats = 1.0 / 16.0;

    CommandResult moveClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "move_clip", p, found))
            return *error;

        auto& tempo = session.getEdit().tempoSequence;
        auto startBeat = params::getDouble (p, "start_beat", tempo.toBeats (found.clip->getPosition().getStart()).inBeats());

        if (startBeat < 0.0)
            return CommandResult::failure ("move_clip: start_beat can't be before the start of the project");

        if (p.hasProperty ("channel"))
        {
            auto channelRef = params::getString (p, "channel");
            auto target = session.findTrack (channelRef, Session::Role::channel);

            if (target == nullptr)
                return CommandResult::failure ("move_clip: no channel called '" + channelRef + "'");

            if (target != found.channel)
            {
                if (Session::isPatternClip (*found.clip))
                    return CommandResult::failure ("move_clip: a step pattern belongs to its channel; it can only move in time");

                if (Session::isAudioChannel (*target) != Session::isAudioChannel (*found.channel))
                    return CommandResult::failure (Session::isAudioChannel (*target)
                                                     ? "move_clip: '" + target->getName() + "' is an audio channel; MIDI clips go on instrument channels"
                                                     : "move_clip: '" + target->getName() + "' is an instrument channel; audio clips go on audio channels");

                if (! found.clip->moveTo (*target))
                    return CommandResult::failure ("move_clip: couldn't move the clip to '" + channelRef + "'");
            }
        }

        // A pattern clip takes its whole block along.
        auto moving = Session::isPatternClip (*found.clip) ? blockOf (session, *found.clip) : juce::Array<te::MidiClip*>();

        if (moving.isEmpty())
            found.clip->setStart (tempo.toTime (te::BeatPosition::fromBeats (startBeat)), false, true);

        for (auto clip : moving)
            clip->setStart (tempo.toTime (te::BeatPosition::fromBeats (startBeat)), false, true);

        return CommandResult::success (describeClip (session, *found.clip));
    }

    CommandResult resizeClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "resize_clip", p, found))
            return *error;

        if (! p.hasProperty ("start_beat") && ! p.hasProperty ("end_beat"))
            return CommandResult::failure ("resize_clip: give start_beat, end_beat or both");

        auto& tempo = session.getEdit().tempoSequence;
        auto& clip = *found.clip;
        auto start = tempo.toBeats (clip.getPosition().getStart()).inBeats();
        auto end = tempo.toBeats (clip.getPosition().getEnd()).inBeats();

        // A pattern block gets its own length; its steps play once from its start.
        if (Session::isPatternClip (clip))
        {
            auto newStart = params::getDouble (p, "start_beat", start);
            auto newEnd = params::getDouble (p, "end_beat", end);

            if (newStart < 0.0 || newEnd - newStart < beatsPerStep - 1.0e-6)
                return CommandResult::failure ("resize_clip: a step pattern can't be shorter than one step or start before the project");

            auto* um = &session.getUndoManager();
            auto pattern = Session::getPatternOf (clip);
            juce::Array<te::AudioTrack*> channels;

            for (auto c : blockOf (session, clip))
            {
                auto* channel = dynamic_cast<te::AudioTrack*> (c->getTrack());

                if (channel == nullptr)
                    continue;

                // Longer than a row adds empty steps to it (up to the most it can have), as FL
                // Studio's pattern grows; past that, the rest is silence.
                auto steps = Session::getSteps (*channel, pattern);
                auto wantedSteps = juce::jmin (maxStepCount, (int) std::ceil ((newEnd - newStart) / beatsPerStep - 1.0e-6));

                if (wantedSteps > steps.length())
                    session.setSteps (*channel, pattern, steps.paddedRight ('.', wantedSteps), Session::getStepPitch (*channel, pattern));

                auto stepsBeats = Session::getSteps (*channel, pattern).length() * beatsPerStep;

                if (std::abs ((newEnd - newStart) - stepsBeats) < 1.0e-6)
                    c->state.removeProperty (patternBeatsProp, um);
                else
                    c->state.setProperty (patternBeatsProp, newEnd - newStart, um);

                c->setStart (tempo.toTime (te::BeatPosition::fromBeats (newStart)), false, true);
                channels.addIfNotAlreadyThere (channel);
            }

            // A longer row lengthens the pattern's other blocks too.
            for (auto channel : channels)
                rebuildPatternClip (session, *channel, pattern);

            return CommandResult::success (describeClip (session, clip));
        }

        // The start can come back as far as the clip's content goes, but no further.
        auto contentStart = start - clip.getOffsetInBeats().inBeats();
        auto newStart = std::max (contentStart, params::getDouble (p, "start_beat", start));
        auto newEnd = params::getDouble (p, "end_beat", end);

        // An audio clip can't run past the end of its file.
        if (auto wave = dynamic_cast<te::WaveAudioClip*> (&clip); wave != nullptr && ! wave->isLooping())
            newEnd = std::min (newEnd, session.getSourceEndBeat (*wave));

        if (newStart < 0.0 || newEnd - newStart < minClipBeats)
            return CommandResult::failure ("resize_clip: the clip would be shorter than a 64th note or start before the project");

        // Edges move; the notes stay where they are in the song.
        clip.setEnd (tempo.toTime (te::BeatPosition::fromBeats (newEnd)), true);
        clip.setStart (tempo.toTime (te::BeatPosition::fromBeats (newStart)), true, false);
        return CommandResult::success (describeClip (session, clip));
    }

    CommandResult splitClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "split_clip", p, found))
            return *error;

        if (Session::isPatternClip (*found.clip))
            return CommandResult::failure ("split_clip: a step pattern can't be split; change its steps instead");

        if (! p.hasProperty ("beat"))
            return CommandResult::failure ("split_clip: give the beat to split at");

        auto& tempo = session.getEdit().tempoSequence;
        auto beat = params::getDouble (p, "beat", 0.0);
        auto start = tempo.toBeats (found.clip->getPosition().getStart()).inBeats();
        auto end = tempo.toBeats (found.clip->getPosition().getEnd()).inBeats();

        if (beat - start < minClipBeats || end - beat < minClipBeats)
            return CommandResult::failure ("split_clip: beat must be inside the clip (" + juce::String (roundBeats (start)) + " to "
                                             + juce::String (roundBeats (end)) + ")");

        auto second = found.channel->splitClip (*found.clip, tempo.toTime (te::BeatPosition::fromBeats (beat)));

        if (second == nullptr)
            return CommandResult::failure ("split_clip: couldn't split the clip");

        return CommandResult::success (juce::Array<juce::var> { describeClip (session, *found.clip), describeClip (session, *second) });
    }

    /** Mutes or unmutes a clip (toggles it when muted isn't given): it stays in place but is silent. */
    CommandResult muteClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "mute_clip", p, found))
            return *error;

        if (p.hasProperty ("muted") && ! p["muted"].isBool())
            return CommandResult::failure ("mute_clip: muted is true or false");

        found.clip->setMuted (p.hasProperty ("muted") ? static_cast<bool> (p["muted"]) : ! found.clip->isMuted());
        return CommandResult::success (describeClip (session, *found.clip));
    }

    CommandResult setClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "set_clip", p, found))
            return *error;

        if (! p.hasProperty ("name") && ! p.hasProperty ("colour"))
            return CommandResult::failure ("set_clip: give a name, a colour, or both");

        std::optional<juce::Colour> colour;
        auto colourText = params::getString (p, "colour").trim();

        if (colourText.isNotEmpty())
        {
            colour = parseColour (colourText);

            if (! colour)
                return CommandResult::failure ("set_clip: colour is a hex colour like \"#3c8ce0\", or \"\" for the channel's");
        }

        if (p.hasProperty ("name"))
        {
            auto name = params::getString (p, "name").trim();

            if (name.isEmpty())
                return CommandResult::failure ("set_clip: the name can't be empty");

            found.clip->setName (name);
        }

        if (p.hasProperty ("colour"))
            session.setClipColour (*found.clip, colour);

        auto result = describeClip (session, *found.clip);
        result.getDynamicObject()->setProperty ("name", found.clip->getName());

        if (colour)
            result.getDynamicObject()->setProperty ("colour", "#" + colour->toDisplayString (false));

        return CommandResult::success (result);
    }

    CommandResult duplicateClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "duplicate_clip", p, found))
            return *error;

        auto& tempo = session.getEdit().tempoSequence;
        auto startBeat = params::getDouble (p, "start_beat", tempo.toBeats (found.clip->getPosition().getEnd()).inBeats());

        if (startBeat < 0.0)
            return CommandResult::failure ("duplicate_clip: start_beat can't be before the start of the project");

        // A pattern clip copies its whole block (and a block already there stays as it is).
        if (Session::isPatternClip (*found.clip))
        {
            te::Clip* copyOfThis = nullptr;
            auto pattern = Session::getPatternOf (*found.clip);

            for (auto c : blockOf (session, *found.clip))
            {
                if (auto* channel = dynamic_cast<te::AudioTrack*> (c->getTrack()))
                {
                    auto placed = placeOn (session, *channel, pattern, startBeat, c->state[patternBeatsProp]);

                    if (placed != nullptr)
                        fillPatternClip (session, *placed, *channel);

                    if (c == found.clip)
                        copyOfThis = placed;
                }
            }

            if (copyOfThis == nullptr)
                return CommandResult::failure ("duplicate_clip: couldn't copy the pattern");

            return CommandResult::success (describeClip (session, *copyOfThis));
        }

        auto copy = te::duplicateClip (*found.clip);

        if (copy == nullptr)
            return CommandResult::failure ("duplicate_clip: couldn't copy the clip");

        copy->setStart (tempo.toTime (te::BeatPosition::fromBeats (startBeat)), false, true);
        return CommandResult::success (describeClip (session, *copy));
    }

    CommandResult deleteClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "delete_clip", p, found))
            return *error;

        auto result = describeClip (session, *found.clip);

        // A pattern clip deletes its whole block.
        if (Session::isPatternClip (*found.clip))
            for (auto clip : blockOf (session, *found.clip))
                clip->removeFromParent();
        else
            found.clip->removeFromParent();

        return CommandResult::success (result);
    }

    //==============================================================================
    // Managing patterns.

    /** The "pattern" param as a pattern that exists, or explains what's wrong. */
    std::optional<CommandResult> requirePattern (Session& session, const juce::String& command, const juce::var& p, int& pattern)
    {
        if (! isNumber (p["pattern"]))
            return CommandResult::failure (command + ": give the pattern's id (get_state lists them)");

        pattern = static_cast<int> (p["pattern"]);

        if (! session.findPattern (pattern))
            return CommandResult::failure (command + ": no pattern " + juce::String (pattern) + " (get_state lists them)");

        return std::nullopt;
    }

    juce::var describePattern (Session& session, int pattern)
    {
        auto info = session.findPattern (pattern).value_or (Session::PatternInfo());
        auto obj = new juce::DynamicObject();
        obj->setProperty ("pattern", info.id);
        obj->setProperty ("name", info.name);

        if (! info.colour.isTransparent())
            obj->setProperty ("colour", "#" + info.colour.toDisplayString (false));

        return juce::var (obj);
    }

    /** A new pattern with the rows of another, e.g. to make a variation of it. */
    int copyPattern (Session& session, int from, const juce::String& name, juce::Colour colour)
    {
        auto pattern = session.addPattern (name, colour);

        for (auto channel : session.getChannels())
            if (Session::getSteps (*channel, from).containsChar ('x'))
                session.setSteps (*channel, pattern, Session::getSteps (*channel, from), Session::getStepPitch (*channel, from));

        return pattern;
    }

    CommandResult createPattern (Session& session, const juce::var& p)
    {
        auto from = 0;

        if (p.hasProperty ("copy_of"))
        {
            from = static_cast<int> (p["copy_of"]);

            if (! isNumber (p["copy_of"]) || ! session.findPattern (from))
                return CommandResult::failure ("create_pattern: no pattern " + p["copy_of"].toString() + " to copy (get_state lists them)");
        }

        std::optional<juce::Colour> colour;

        if (auto text = params::getString (p, "colour").trim(); text.isNotEmpty())
            if (! (colour = parseColour (text)))
                return CommandResult::failure ("create_pattern: colour is a hex colour like \"#3c8ce0\"");

        auto name = params::getString (p, "name").trim();
        auto pattern = from > 0 ? copyPattern (session, from, name, colour.value_or (juce::Colour()))
                                : session.addPattern (name, colour.value_or (juce::Colour()));

        if (name.isEmpty())
            session.setPatternInfo (pattern, "Pattern " + juce::String (pattern), colour.value_or (juce::Colour()));

        // As in FL Studio, a new pattern is the one the rack shows.
        session.setCurrentPattern (pattern);
        return CommandResult::success (describePattern (session, pattern));
    }

    CommandResult setPattern (Session& session, const juce::var& p)
    {
        int pattern = 0;

        if (auto error = requirePattern (session, "set_pattern", p, pattern))
            return *error;

        if (! p.hasProperty ("name") && ! p.hasProperty ("colour"))
            return CommandResult::failure ("set_pattern: give a name, a colour, or both");

        auto info = *session.findPattern (pattern);

        if (p.hasProperty ("name"))
        {
            info.name = params::getString (p, "name").trim();

            if (info.name.isEmpty())
                return CommandResult::failure ("set_pattern: the name can't be empty");
        }

        if (p.hasProperty ("colour"))
        {
            auto text = params::getString (p, "colour").trim();
            auto colour = text.isEmpty() ? std::optional<juce::Colour> (juce::Colour()) : parseColour (text);

            if (! colour)
                return CommandResult::failure ("set_pattern: colour is a hex colour like \"#3c8ce0\", or \"\" for the channels' colours");

            info.colour = *colour;
        }

        session.setPatternInfo (pattern, info.name, info.colour);

        // Its clips take the new name and colour.
        for (auto clip : patternClips (session, pattern))
        {
            clip->setName (info.name);
            session.setClipColour (*clip, info.colour.isTransparent() ? std::optional<juce::Colour>() : info.colour);
        }

        return CommandResult::success (describePattern (session, pattern));
    }

    CommandResult deletePattern (Session& session, const juce::var& p)
    {
        int pattern = 0;

        if (auto error = requirePattern (session, "delete_pattern", p, pattern))
            return *error;

        if (pattern == 1)
            return CommandResult::failure ("delete_pattern: pattern 1 can't be deleted; clear its steps instead");

        auto result = describePattern (session, pattern);

        for (auto clip : patternClips (session, pattern))
            clip->removeFromParent();

        session.removePattern (pattern);

        if (session.getCurrentPattern() == pattern)
            session.setCurrentPattern (1);

        return CommandResult::success (result);
    }

    CommandResult selectPattern (Session& session, const juce::var& p)
    {
        int pattern = 0;

        if (auto error = requirePattern (session, "select_pattern", p, pattern))
            return *error;

        session.setCurrentPattern (pattern);
        return CommandResult::success (describePattern (session, pattern));
    }

    CommandResult placePattern (Session& session, const juce::var& p)
    {
        int pattern = 0;

        if (auto error = requirePattern (session, "place_pattern", p, pattern))
            return *error;

        auto startBeat = params::getDouble (p, "start_beat", 0.0);

        if (startBeat < 0.0)
            return CommandResult::failure ("place_pattern: start_beat can't be before the start of the project");

        juce::Array<juce::var> clips;

        for (auto channel : session.getChannels())
        {
            if (Session::isAudioChannel (*channel) || ! Session::getSteps (*channel, pattern).containsChar ('x'))
                continue;

            if (auto clip = placeOn (session, *channel, pattern, startBeat, {}))
            {
                fillPatternClip (session, *clip, *channel);
                clips.add (describeClip (session, *clip));
            }
        }

        if (clips.isEmpty())
            return CommandResult::failure ("place_pattern: pattern " + juce::String (pattern) + " has no steps yet; set some with set_steps");

        auto result = describePattern (session, pattern);
        result.getDynamicObject()->setProperty ("start_beat", roundBeats (startBeat));
        result.getDynamicObject()->setProperty ("clips", clips);
        return CommandResult::success (result);
    }

    CommandResult makeUnique (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "make_unique", p, found))
            return *error;

        auto from = Session::getPatternOf (*found.clip);

        if (from == 0)
            return CommandResult::failure ("make_unique: only a step pattern's clips can be made unique");

        // The block gets a pattern of its own, starting as a copy, so changing its steps
        // leaves the pattern's other blocks as they are.
        auto info = *session.findPattern (from);
        auto block = blockOf (session, *found.clip);
        auto pattern = copyPattern (session, from, info.name + " (copy)", info.colour);

        for (auto clip : block)
        {
            clip->state.setProperty (patternClipProp, pattern, &session.getUndoManager());
            clip->setName (info.name + " (copy)");
        }

        session.setCurrentPattern (pattern);
        auto result = describePattern (session, pattern);
        result.getDynamicObject()->setProperty ("clip", found.clip->itemID.toString());
        return CommandResult::success (result);
    }

    //==============================================================================

    /** Looks up the "clip" param as a MIDI clip whose notes can be edited, or explains what's wrong. */
    std::optional<CommandResult> requireNoteClip (Session& session, const juce::String& command,
                                                  const juce::var& p, te::MidiClip*& midi)
    {
        FoundClip found;

        if (auto error = requireClip (session, command, p, found))
            return error;

        midi = dynamic_cast<te::MidiClip*> (found.clip);

        if (midi == nullptr)
            return CommandResult::failure (command + ": that clip has no notes to edit");

        return std::nullopt;
    }

    CommandResult getNotes (Session& session, const juce::var& p)
    {
        te::MidiClip* midi = nullptr;

        if (auto error = requireNoteClip (session, "get_notes", p, midi))
            return *error;

        auto result = describeClip (session, *midi);
        result.getDynamicObject()->setProperty ("pattern", Session::isPatternClip (*midi));
        result.getDynamicObject()->setProperty ("notes", notesToVar (Session::getNotes (*midi)));
        return CommandResult::success (result);
    }

    CommandResult setNotes (Session& session, const juce::var& p)
    {
        te::MidiClip* midi = nullptr;

        if (auto error = requireNoteClip (session, "set_notes", p, midi))
            return *error;

        if (Session::isPatternClip (*midi))
            return CommandResult::failure ("set_notes: a step pattern's notes follow its steps; change them with set_steps");

        auto list = p["notes"];

        if (! list.isArray())
            return CommandResult::failure ("set_notes: give notes as an array of {pitch, start, length, velocity} (it can be empty)");

        // Notes are timed from the clip's start; Tracktion times them from where its content starts.
        auto offset = midi->getOffsetInBeats().inBeats();
        juce::Array<ClipNote> notes;

        for (auto& n : *list.getArray())
        {
            ClipNote note;
            note.pitch = static_cast<int> (n.getProperty ("pitch", -1));
            note.start = static_cast<double> (n.getProperty ("start", 0.0));
            note.length = static_cast<double> (n.getProperty ("length", 1.0));
            note.velocity = static_cast<int> (n.getProperty ("velocity", 100));

            if (! n.isObject() || ! juce::isPositiveAndBelow (note.pitch, 128) || ! (note.length >= minClipBeats / 4.0)
                  || ! (note.start + offset >= -1.0e-9) || ! juce::isPositiveAndBelow (note.velocity - 1, 127))
                return CommandResult::failure ("set_notes: invalid note " + juce::JSON::toString (n, true)
                                                 + " (pitch 0-127, length above 0, velocity 1-127, not before the clip's content)");

            notes.add (note);
        }

        auto* um = &session.getUndoManager();
        auto& sequence = midi->getSequence();
        sequence.clear (um);

        for (auto& n : notes)
            sequence.addNote (n.pitch, te::BeatPosition::fromBeats (std::max (0.0, n.start + offset)),
                              te::BeatDuration::fromBeats (n.length), n.velocity, 0, um);

        auto result = describeClip (session, *midi);
        result.getDynamicObject()->setProperty ("notes", notesToVar (Session::getNotes (*midi)));
        return CommandResult::success (result);
    }

    CommandResult quantizeNotes (Session& session, const juce::var& p)
    {
        te::MidiClip* midi = nullptr;

        if (auto error = requireNoteClip (session, "quantize_notes", p, midi))
            return *error;

        if (Session::isPatternClip (*midi))
            return CommandResult::failure ("quantize_notes: a step pattern is always on its steps");

        auto grid = params::getDouble (p, "grid", 0.25);

        if (! (grid >= 1.0 / 64.0 && grid <= 16.0))
            return CommandResult::failure ("quantize_notes: grid must be from 1/64 to 16 beats (0.25 is a 16th note)");

        auto strength = params::getDouble (p, "strength", 1.0);

        if (! (strength > 0.0 && strength <= 1.0))
            return CommandResult::failure ("quantize_notes: strength must be above 0 and at most 1");

        auto notes = Session::getNotes (*midi);
        juce::Array<bool> chosen;
        chosen.insertMultiple (0, ! p.hasProperty ("notes"), notes.size());

        if (p.hasProperty ("notes"))
        {
            if (! p["notes"].isArray())
                return CommandResult::failure ("quantize_notes: notes must be an array of indexes, as listed by get_notes");

            for (auto& i : *p["notes"].getArray())
            {
                if (! isNumber (i) || ! juce::isPositiveAndBelow (static_cast<int> (i), notes.size()))
                    return CommandResult::failure ("quantize_notes: no note " + i.toString() + " (the clip has "
                                                     + juce::String (notes.size()) + ")");

                chosen.set (static_cast<int> (i), true);
            }
        }

        auto ends = static_cast<bool> (p.getProperty ("ends", false));
        auto offset = midi->getOffsetInBeats().inBeats();
        auto towards = [grid, strength] (double beat) { return beat + (std::round (beat / grid) * grid - beat) * strength; };
        int changed = 0;

        for (int i = 0; i < notes.size(); ++i)
        {
            if (! chosen[i])
                continue;

            auto& n = notes.getReference (i);
            auto end = n.start + n.length;
            auto newStart = std::max (-offset, towards (n.start));
            auto newEnd = ends ? std::max (newStart + grid * strength, towards (end)) : newStart + n.length;

            if (std::abs (newStart - n.start) > 1.0e-9 || std::abs (newEnd - end) > 1.0e-9)
                ++changed;

            n.start = newStart;
            n.length = newEnd - newStart;
        }

        auto* um = &session.getUndoManager();
        auto& sequence = midi->getSequence();
        sequence.clear (um);

        for (auto& n : notes)
            sequence.addNote (n.pitch, te::BeatPosition::fromBeats (n.start + offset),
                              te::BeatDuration::fromBeats (n.length), n.velocity, 0, um);

        // Where the quantized notes ended up in the new order, e.g. to keep them selected.
        auto after = Session::getNotes (*midi);
        juce::Array<bool> taken;
        taken.insertMultiple (0, false, after.size());
        juce::Array<juce::var> quantized;

        for (int i = 0; i < notes.size(); ++i)
        {
            if (! chosen[i])
                continue;

            for (int j = 0; j < after.size(); ++j)
            {
                auto& a = after.getReference (j);
                auto& n = notes.getReference (i);

                if (! taken[j] && a.pitch == n.pitch && std::abs (a.start - n.start) < 1.0e-6 && std::abs (a.length - n.length) < 1.0e-6)
                {
                    taken.set (j, true);
                    quantized.add (j);
                    break;
                }
            }
        }

        auto result = describeClip (session, *midi);
        result.getDynamicObject()->setProperty ("moved", changed);
        result.getDynamicObject()->setProperty ("quantized", quantized);
        result.getDynamicObject()->setProperty ("notes", notesToVar (after));
        return CommandResult::success (result);
    }

}

//==============================================================================
juce::var notesToVar (const juce::Array<ClipNote>& notes)
{
    juce::Array<juce::var> list;

    for (auto& n : notes)
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty ("pitch", n.pitch);
        obj->setProperty ("start", roundBeats (n.start));
        obj->setProperty ("length", roundBeats (n.length));
        obj->setProperty ("velocity", n.velocity);
        list.add (juce::var (obj));
    }

    return list;
}

void registerClipCommands (CommandRegistry& registry)
{
    registry.add ({ "add_midi_clip",
                    "Adds a MIDI clip with notes to a channel. Note start and length are in beats from the clip start.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "start_beat", prop ("number", "Where the clip starts, in beats (default 0)") },
                              { "length_beats", prop ("number", "Clip length in beats (default 4)") },
                              { "notes", prop ("array", "Notes as {pitch, start, length, velocity}") } },
                            { "channel" }),
                    true, addMidiClip });

    registry.add ({ "set_steps",
                    "Sets a channel's row in a step pattern: 16th-note steps that play one pitch wherever the pattern "
                    "is placed (pattern 1 starts at the beginning of the project). Give the whole row, a new length, or one step.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "pattern", prop ("integer", "Pattern id, from get_state (default: the current pattern)") },
                              { "steps", prop ("string", "The row as 'x' (on) and '.' (off), e.g. \"x...x...x...x...\"; "
                                                         "an array of booleans also works") },
                              { "length", prop ("integer", "Number of steps, 1 to 64 (default 16)") },
                              { "step", prop ("integer", "One step to change, from 0") },
                              { "on", prop ("boolean", "With step: turn it on or off (toggles if omitted)") },
                              { "pitch", prop ("integer", "MIDI note the steps play (default 60, middle C)") } },
                            { "channel" }),
                    true, setSteps });

    registry.add ({ "move_clip",
                    "Moves a clip to a new start beat, and optionally to another channel. A step pattern's clip only moves in time, and takes its whole block along.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "start_beat", prop ("number", "New start, in beats (default: where it is)") },
                              { "channel", prop ("string", "Channel id or name to move it to") } },
                            { "clip" }),
                    true, moveClip });

    registry.add ({ "resize_clip",
                    "Moves a clip's start and/or end; its notes stay where they are in the song. "
                    "A step pattern's block can be resized too: longer adds empty steps to its rows (up to 64); "
                    "shorter cuts off the steps past its end.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "start_beat", prop ("number", "New start, in beats") },
                              { "end_beat", prop ("number", "New end, in beats") } },
                            { "clip" }),
                    true, resizeClip });

    registry.add ({ "split_clip",
                    "Splits a clip in two at a beat inside it. Returns both halves.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "beat", prop ("number", "Where to split, in beats from the start of the project") } },
                            { "clip", "beat" }),
                    true, splitClip });

    registry.add ({ "mute_clip",
                    "Mutes or unmutes a clip: a muted clip stays where it is but plays nothing (as FL Studio's mute tool does). "
                    "Leave out muted to toggle it.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "muted", prop ("boolean", "True to mute, false to unmute; leave out to toggle") } },
                            { "clip" }),
                    true, muteClip });

    registry.add ({ "set_clip",
                    "Renames a clip or gives it a colour of its own. A clip shows in its channel's colour unless it has "
                    "one; give colour \"\" to go back to the channel's.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "name", prop ("string", "The clip's new name") },
                              { "colour", prop ("string", "A hex colour like \"#3c8ce0\", or \"\" for the channel's colour") } },
                            { "clip" }),
                    true, setClip });

    registry.add ({ "duplicate_clip",
                    "Copies a clip, by default to just after itself. A step pattern's clip copies its whole block, which keeps playing the same pattern.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "start_beat", prop ("number", "Where the copy starts, in beats (default: the clip's end)") } },
                            { "clip" }),
                    true, duplicateClip });

    registry.add ({ "delete_clip",
                    "Deletes a clip. A step pattern's clip deletes its whole block (the pattern's clips on every channel that start there).",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") } }, { "clip" }),
                    true, deleteClip });

    registry.add ({ "create_pattern",
                    "Adds a step pattern, as in FL Studio: a step row for each channel, placed on the timeline as one block. "
                    "It becomes the current pattern, which set_steps changes by default.",
                    schema ({ { "name", prop ("string", "Its name (default \"Pattern <id>\")") },
                              { "colour", prop ("string", "Hex colour such as #3c8ce0 for its clips (default: the channels' colours)") },
                              { "copy_of", prop ("integer", "A pattern whose rows it starts with, to make a variation") } }),
                    true, createPattern });

    registry.add ({ "set_pattern",
                    "Renames a step pattern or changes its colour (its clips follow).",
                    schema ({ { "pattern", prop ("integer", "Pattern id, from get_state") },
                              { "name", prop ("string", "New name") },
                              { "colour", prop ("string", "Hex colour such as #3c8ce0, or \"\" for the channels' colours") } },
                            { "pattern" }),
                    true, setPattern });

    registry.add ({ "delete_pattern",
                    "Deletes a step pattern, its rows and every block of it on the timeline. Pattern 1 can't be deleted.",
                    schema ({ { "pattern", prop ("integer", "Pattern id, from get_state") } }, { "pattern" }),
                    true, deletePattern });

    registry.add ({ "select_pattern",
                    "Makes a pattern the current one: the one the channel rack shows and set_steps changes by default.",
                    schema ({ { "pattern", prop ("integer", "Pattern id, from get_state") } }, { "pattern" }),
                    true, selectPattern });

    registry.add ({ "place_pattern",
                    "Places a step pattern on the timeline as a block: a clip on every channel with steps in it, all starting at start_beat. "
                    "Moving, copying, resizing or deleting any clip of the block does the same to all of it.",
                    schema ({ { "pattern", prop ("integer", "Pattern id, from get_state") },
                              { "start_beat", prop ("number", "Where the block starts, in beats (default 0)") } },
                            { "pattern" }),
                    true, placePattern });

    registry.add ({ "make_unique",
                    "Gives a step pattern's block a pattern of its own, a copy of the one it played, so its steps can change "
                    "without changing the pattern's other blocks. The copy becomes the current pattern.",
                    schema ({ { "clip", prop ("string", "Id of any clip of the block, from get_state") } }, { "clip" }),
                    true, makeUnique });

    registry.add ({ "get_notes",
                    "Lists a MIDI clip's notes. Start and length are in beats from the clip's start.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") } }, { "clip" }),
                    false, getNotes });

    registry.add ({ "set_notes",
                    "Replaces all of a MIDI clip's notes, e.g. after drawing in the piano roll. Start and length are "
                    "in beats from the clip's start; notes past the clip's end are kept but don't play.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "notes", prop ("array", "Notes as {pitch, start, length, velocity}; velocity defaults to 100") } },
                            { "clip", "notes" }),
                    true, setNotes });

    registry.add ({ "quantize_notes",
                    "Moves a MIDI clip's notes onto a grid. Note lengths stay the same unless ends is true.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "grid", prop ("number", "Grid in beats: 1 is a quarter note, 0.25 a 16th (default)") },
                              { "notes", prop ("array", "Indexes of the notes to quantize, as listed by get_notes (default: all)") },
                              { "strength", prop ("number", "How far towards the grid, above 0 up to 1 (default 1)") },
                              { "ends", prop ("boolean", "Also snap where notes end (default false)") } },
                            { "clip" }),
                    true, quantizeNotes });
}

} // namespace ng
