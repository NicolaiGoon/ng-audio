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
    // Step patterns: each channel has a row of steps (16th notes) that plays one pitch.
    // The steps are stored on the channel and turned into a MIDI clip at the start of the
    // project, marked as the channel's pattern clip and rebuilt whenever the steps change.
    const juce::Identifier stepsProp ("ngSteps"), stepPitchProp ("ngStepPitch"), patternClipProp ("ngPattern"),
                          patternBeatsProp ("ngPatternBeats");
    constexpr int maxStepCount = 64;
    constexpr double beatsPerStep = 0.25;

    /** Brings a channel's pattern clips in line with its steps. A pattern can be placed on the
        timeline several times (duplicate_clip); every copy plays the same steps, so each one is
        refilled where it stands. A copy that was resized (resize_clip) keeps its own length, as in FL
        Studio: the steps play once from its start, cut off if it's shorter (a longer one
        first grows the step row, see resize_clip). The rest follow the steps' length.
        With no copies left, the pattern goes back at the start. */
    void rebuildPatternClip (Session& session, te::AudioTrack& channel)
    {
        auto& edit = session.getEdit();
        auto* um = &session.getUndoManager();
        auto steps = Session::getSteps (channel);

        juce::Array<te::MidiClip*> instances;

        for (auto clip : channel.getClips())
            if (Session::isPatternClip (*clip))
                if (auto midi = dynamic_cast<te::MidiClip*> (clip))
                    instances.add (midi);

        if (! steps.containsChar ('x'))
        {
            for (auto clip : instances)
                clip->removeFromParent();

            return;
        }

        auto& tempo = edit.tempoSequence;
        auto patternBeats = steps.length() * beatsPerStep;
        auto lengthBeats = te::BeatDuration::fromBeats (patternBeats);

        if (instances.isEmpty())
        {
            auto clip = channel.insertMIDIClip ("Pattern", { tempo.toTime (te::BeatPosition()), tempo.toTime (te::BeatPosition() + lengthBeats) }, nullptr);

            if (clip == nullptr)
                return;

            clip->state.setProperty (patternClipProp, true, um);
            instances.add (clip.get());
        }

        auto pitch = Session::getStepPitch (channel);

        for (auto clip : instances)
        {
            auto startBeat = tempo.toBeats (clip->getPosition().getStart());
            auto clipBeats = clip->state.hasProperty (patternBeatsProp) ? static_cast<double> (clip->state[patternBeatsProp])
                                                                         : patternBeats;
            clip->setPosition ({ { clip->getPosition().getStart(), tempo.toTime (startBeat + te::BeatDuration::fromBeats (clipBeats)) },
                                 te::TimeDuration() });
            clip->getSequence().clear (um);

            for (int i = 0; i < steps.length(); ++i)
                if (auto beat = i * beatsPerStep; steps[i] == 'x' && beat < clipBeats - 1.0e-6)
                    clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (beat),
                                                 te::BeatDuration::fromBeats (beatsPerStep), 100, 0, um);
        }
    }

    CommandResult setSteps (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("set_steps: no channel called '" + ref + "'");

        if (Session::isAudioChannel (*channel))
            return CommandResult::failure ("set_steps: '" + channel->getName() + "' is an audio channel, which has no step pattern");

        auto steps = Session::getSteps (*channel);
        auto pitch = Session::getStepPitch (*channel);

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

        channel->state.setProperty (stepsProp, steps, &session.getUndoManager());
        channel->state.setProperty (stepPitchProp, pitch, &session.getUndoManager());
        rebuildPatternClip (session, *channel);

        auto obj = new juce::DynamicObject();
        obj->setProperty ("channel", channel->itemID.toString());
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

        found.clip->setStart (tempo.toTime (te::BeatPosition::fromBeats (startBeat)), false, true);
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

        // A step pattern copy gets its own length; its steps play once from its start.
        if (Session::isPatternClip (clip))
        {
            auto newStart = params::getDouble (p, "start_beat", start);
            auto newEnd = params::getDouble (p, "end_beat", end);

            if (newStart < 0.0 || newEnd - newStart < beatsPerStep - 1.0e-6)
                return CommandResult::failure ("resize_clip: a step pattern can't be shorter than one step or start before the project");

            auto* channel = dynamic_cast<te::AudioTrack*> (clip.getTrack());

            if (channel == nullptr)
                return CommandResult::failure ("resize_clip: the pattern isn't on a channel");

            auto* um = &session.getUndoManager();

            // Longer than the steps adds empty steps to the channel's row (up to the most it
            // can have), as FL Studio's pattern grows; past that, the rest is silence.
            auto steps = Session::getSteps (*channel);
            auto wantedSteps = juce::jmin (maxStepCount, (int) std::ceil ((newEnd - newStart) / beatsPerStep - 1.0e-6));

            if (wantedSteps > steps.length())
                channel->state.setProperty (stepsProp, steps.paddedRight ('.', wantedSteps), um);

            auto stepsBeats = Session::getSteps (*channel).length() * beatsPerStep;

            if (std::abs ((newEnd - newStart) - stepsBeats) < 1.0e-6)
                clip.state.removeProperty (patternBeatsProp, um);
            else
                clip.state.setProperty (patternBeatsProp, newEnd - newStart, um);

            clip.setStart (tempo.toTime (te::BeatPosition::fromBeats (newStart)), false, true);
            rebuildPatternClip (session, *channel);
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

    CommandResult duplicateClip (Session& session, const juce::var& p)
    {
        FoundClip found;

        if (auto error = requireClip (session, "duplicate_clip", p, found))
            return *error;

        auto& tempo = session.getEdit().tempoSequence;
        auto startBeat = params::getDouble (p, "start_beat", tempo.toBeats (found.clip->getPosition().getEnd()).inBeats());

        if (startBeat < 0.0)
            return CommandResult::failure ("duplicate_clip: start_beat can't be before the start of the project");

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
        found.clip->removeFromParent();
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
                    "Sets a channel's step pattern: a row of 16th-note steps from the start of the project that "
                    "plays one pitch and loops with the project. Give the whole row, a new length, or one step.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "steps", prop ("string", "The row as 'x' (on) and '.' (off), e.g. \"x...x...x...x...\"; "
                                                         "an array of booleans also works") },
                              { "length", prop ("integer", "Number of steps, 1 to 64 (default 16)") },
                              { "step", prop ("integer", "One step to change, from 0") },
                              { "on", prop ("boolean", "With step: turn it on or off (toggles if omitted)") },
                              { "pitch", prop ("integer", "MIDI note the steps play (default 60, middle C)") } },
                            { "channel" }),
                    true, setSteps });

    registry.add ({ "move_clip",
                    "Moves a clip to a new start beat, and optionally to another channel. A step pattern can only move in time.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "start_beat", prop ("number", "New start, in beats (default: where it is)") },
                              { "channel", prop ("string", "Channel id or name to move it to") } },
                            { "clip" }),
                    true, moveClip });

    registry.add ({ "resize_clip",
                    "Moves a clip's start and/or end; its notes stay where they are in the song. "
                    "A step pattern copy can be resized too: longer adds empty steps to the channel's row (up to 64); "
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

    registry.add ({ "duplicate_clip",
                    "Copies a clip, by default to just after itself. A copied step pattern keeps following the channel's steps.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") },
                              { "start_beat", prop ("number", "Where the copy starts, in beats (default: the clip's end)") } },
                            { "clip" }),
                    true, duplicateClip });

    registry.add ({ "delete_clip",
                    "Deletes a clip.",
                    schema ({ { "clip", prop ("string", "Clip id, from get_state") } }, { "clip" }),
                    true, deleteClip });

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
