#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::isNumber;
    using params::prop;
    using params::schema;

    CommandResult play (Session& session, const juce::var& p)
    {
        auto& transport = session.getEdit().getTransport();

        if (p.hasProperty ("loop"))
            transport.looping = static_cast<bool> (p["loop"]);

        session.updateLoopRange();
        transport.play (false);
        return CommandResult::success();
    }

    CommandResult setTempo (Session& session, const juce::var& p)
    {
        auto bpm = p["bpm"];

        if (! isNumber (bpm) || static_cast<double> (bpm) < 20.0 || static_cast<double> (bpm) > 300.0)
            return CommandResult::failure ("set_tempo: bpm must be a number from 20 to 300");

        // Clips are placed in beats, so they keep their place in the music at the new tempo.
        session.getEdit().tempoSequence.getTempo (0)->setBpm (static_cast<double> (bpm));
        session.updateLoopRange();
        auto obj = new juce::DynamicObject();
        obj->setProperty ("bpm", session.getEdit().tempoSequence.getBpmAt (te::TimePosition()));
        return CommandResult::success (juce::var (obj));
    }

    CommandResult setTimeSignature (Session& session, const juce::var& p)
    {
        auto numerator = p["numerator"];
        auto denominator = p["denominator"];

        if (! isNumber (numerator) || static_cast<int> (numerator) < 1 || static_cast<int> (numerator) > 32
              || static_cast<double> (numerator) != static_cast<int> (numerator))
            return CommandResult::failure ("set_time_signature: numerator must be a whole number from 1 to 32");

        if (! isNumber (denominator) || ! juce::Array<double> { 1, 2, 4, 8, 16, 32 }.contains (static_cast<double> (denominator)))
            return CommandResult::failure ("set_time_signature: denominator must be 1, 2, 4, 8, 16 or 32");

        auto& timeSig = *session.getEdit().tempoSequence.getTimeSig (0);
        timeSig.numerator = static_cast<int> (numerator);
        timeSig.denominator = static_cast<int> (denominator);
        session.updateLoopRange();

        auto obj = new juce::DynamicObject();
        obj->setProperty ("numerator", timeSig.numerator.get());
        obj->setProperty ("denominator", timeSig.denominator.get());
        return CommandResult::success (juce::var (obj));
    }

    CommandResult setLoop (Session& session, const juce::var& p)
    {
        auto hasStart = p.hasProperty ("start_beat"), hasEnd = p.hasProperty ("end_beat");

        if (hasStart != hasEnd)
            return CommandResult::failure ("set_loop: give both start_beat and end_beat, or neither");

        if (hasStart)
        {
            if (! isNumber (p["start_beat"]) || ! isNumber (p["end_beat"]))
                return CommandResult::failure ("set_loop: start_beat and end_beat must be numbers");

            auto start = static_cast<double> (p["start_beat"]), end = static_cast<double> (p["end_beat"]);

            if (start < 0.0 || end <= start)
                return CommandResult::failure ("set_loop: the loop must start at or after beat 0 and end after it starts");
        }

        if (p.hasProperty ("enabled"))
            session.getEdit().getTransport().looping = static_cast<bool> (p["enabled"]);

        if (hasStart)
            session.setCustomLoop (juce::Range<double> (p["start_beat"], p["end_beat"]));
        else if (static_cast<bool> (p.getProperty ("whole_project", false)))
            session.setCustomLoop (std::nullopt);

        auto beats = session.getLoopBeats();
        auto obj = new juce::DynamicObject();
        obj->setProperty ("enabled", session.getEdit().getTransport().looping.get());
        obj->setProperty ("start_beat", beats.getStart());
        obj->setProperty ("end_beat", beats.getEnd());
        obj->setProperty ("whole_project", ! session.hasCustomLoop());
        return CommandResult::success (juce::var (obj));
    }

    CommandResult setPosition (Session& session, const juce::var& p)
    {
        if (! p.hasProperty ("beat"))
            return CommandResult::failure ("set_position: give the beat to move the playhead to");

        auto beat = static_cast<double> (p["beat"]);

        if (! (beat >= 0.0 && beat < 100000.0))
            return CommandResult::failure ("set_position: beat must be 0 or more");

        auto& edit = session.getEdit();
        edit.getTransport().setPosition (edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat)));
        auto result = new juce::DynamicObject();
        result->setProperty ("beat", beat);
        return CommandResult::success (juce::var (result));
    }

    CommandResult setMetronome (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();

        if (p.hasProperty ("enabled"))
            edit.clickTrackEnabled = static_cast<bool> (p["enabled"]);
        else
            edit.clickTrackEnabled = ! edit.clickTrackEnabled.get();

        // Tracktion clicks only while recording by default; ours clicks whenever it's on.
        edit.clickTrackRecordingOnly = false;
        edit.clickTrackEmphasiseBars = true;

        auto obj = new juce::DynamicObject();
        obj->setProperty ("enabled", edit.clickTrackEnabled.get());
        return CommandResult::success (juce::var (obj));
    }

    CommandResult stop (Session& session, const juce::var&)
    {
        auto& transport = session.getEdit().getTransport();

        if (! transport.isRecording())
        {
            transport.stop (false, false);
            return CommandResult::success();
        }

        // What was recorded lands as clips when recording stops: one undo step, tidied
        // up like any other command so the first Undo removes it.
        auto& um = session.getUndoManager();
        um.beginNewTransaction ("Record");
        transport.stop (false, false);
        session.sortTrackContents();
        um.beginNewTransaction();
        return CommandResult::success();
    }

    CommandResult armChannel (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto armed = static_cast<bool> (p.getProperty ("armed", true));
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (armed && channel == nullptr)
            return CommandResult::failure ("arm_channel: no channel called '" + ref + "'");

        // Audio channels arm for their audio input, any number at once.
        if (channel != nullptr && Session::isAudioChannel (*channel))
        {
            if (armed && Session::getAudioInput (*channel).isEmpty())
                return CommandResult::failure ("arm_channel: '" + channel->getName() + "' has no audio input; "
                                               "pick one with set_audio_input first");

            session.setAudioArmed (*channel, armed);
        }
        else if (armed)
        {
            session.setArmedChannel (channel);
        }
        else if (channel == nullptr || session.getArmedChannel() == channel)
        {
            session.setArmedChannel (nullptr);

            if (channel == nullptr)
                for (auto c : session.getArmedAudioChannels())
                    session.setAudioArmed (*c, false);
        }

        auto obj = new juce::DynamicObject();
        auto now = session.getArmedChannel();
        juce::Array<juce::var> audio;

        for (auto c : session.getArmedAudioChannels())
            audio.add (c->itemID.toString());

        obj->setProperty ("armed_channel", now != nullptr ? juce::var (now->itemID.toString()) : juce::var());
        obj->setProperty ("armed_audio", audio);
        obj->setProperty ("midi_inputs", session.getMidiInputNames());
        return CommandResult::success (juce::var (obj));
    }

    CommandResult record (Session& session, const juce::var&)
    {
        auto armedAudio = session.getArmedAudioChannels();

        if (session.getArmedChannel() == nullptr && armedAudio.isEmpty())
            return CommandResult::failure ("record: arm a channel first (arm_channel), so the recording has somewhere to go");

        // Every armed audio channel needs its input to be there.
        auto inputs = session.getAudioInputNames();

        for (auto c : armedAudio)
            if (! inputs.contains (Session::getAudioInput (*c)))
                return CommandResult::failure ("record: '" + c->getName() + "' records from '" + Session::getAudioInput (*c)
                                                 + "', which the audio device doesn't have"
                                                 + (inputs.isEmpty() ? juce::String ("; pick an audio device with inputs in Audio settings")
                                                                     : "; pick one of " + inputs.joinIntoString (", ")));

        auto& transport = session.getEdit().getTransport();

        if (transport.isRecording())
            return CommandResult::failure ("record: already recording");

        session.routeMidiInputs();
        session.routeAudioInputs();
        session.updateLoopRange();
        transport.record (false, true);
        return CommandResult::success();
    }

}

//==============================================================================
void registerTransportCommands (CommandRegistry& registry)
{
    registry.add ({ "set_tempo",
                    "Sets the project tempo. Clips keep their positions in beats.",
                    schema ({ { "bpm", prop ("number", "Beats per minute, 20 to 300") } }, { "bpm" }),
                    true, setTempo });

    registry.add ({ "set_time_signature",
                    "Sets the project time signature, e.g. 3/4 or 6/8.",
                    schema ({ { "numerator", prop ("integer", "Beats per bar, 1 to 32") },
                              { "denominator", prop ("integer", "Note value of a beat: 1, 2, 4, 8, 16 or 32") } },
                            { "numerator", "denominator" }),
                    true, setTimeSignature });

    registry.add ({ "set_loop",
                    "Turns looping on or off and sets the loop region in beats. Without a region the loop covers "
                    "the whole project, rounded up to whole bars. Like the play position, this isn't an undo step.",
                    schema ({ { "enabled", prop ("boolean", "Loop playback") },
                              { "start_beat", prop ("number", "Loop start in beats (give with end_beat)") },
                              { "end_beat", prop ("number", "Loop end in beats (give with start_beat)") },
                              { "whole_project", prop ("boolean", "true to go back to looping the whole project") } }),
                    false, setLoop });

    registry.add ({ "play", "Starts playback from the current position. Loops when looping is on (see set_loop).",
                    schema ({ { "loop", prop ("boolean", "Turn looping on or off before playing") } }),
                    false, play });

    registry.add ({ "stop", "Stops playback. When recording, the recorded notes become clips on the armed "
                            "channel, as one undo step.", schema ({}), false, stop });

    registry.add ({ "arm_channel",
                    "Arms a channel for recording. An instrument channel arms for MIDI input: MIDI keyboards (and the "
                    "on-screen keyboard) play it, and arming one disarms the other instrument channels. An audio channel "
                    "arms for its audio input (set_audio_input), and any number can be armed together. "
                    "Leave out channel with armed false to disarm everything. Not an undo step.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "armed", prop ("boolean", "false to disarm (default true)") } }),
                    false, armChannel });

    registry.add ({ "record",
                    "Starts recording from the playhead: MIDI into the armed instrument channel and audio into each armed "
                    "audio channel. stop finishes the recording; what was recorded becomes clips, as one undo step.",
                    schema ({}), false, record });

    registry.add ({ "set_metronome",
                    "Turns the metronome (a click on every beat, louder on the bar) on or off. Like the loop, "
                    "this isn't an undo step.",
                    schema ({ { "enabled", prop ("boolean", "On or off (toggles if omitted)") } }),
                    false, setMetronome });

    registry.add ({ "set_position", "Moves the playhead. Like play and stop, this isn't an undo step.",
                    schema ({ { "beat", prop ("number", "Where to move the playhead, in beats from the start") } }, { "beat" }),
                    false, setPosition });
}

} // namespace ng
