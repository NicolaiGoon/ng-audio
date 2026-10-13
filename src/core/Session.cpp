#include "Session.h"

namespace ng
{
namespace
{
    const juce::Identifier loopStartProp ("ngLoopStartBeat"), loopEndProp ("ngLoopEndBeat");
    const juce::Identifier roleProp ("ngRole");
    const juce::Identifier ownStripProp ("ngOwnStrip");

    juce::String colourToString (juce::Colour c)
    {
        return "#" + c.toDisplayString (false);
    }
}

juce::String toString (Session::Role role)
{
    switch (role)
    {
        case Session::Role::channel:  return "channel";
        case Session::Role::strip:    return "strip";
        case Session::Role::none:     break;
    }

    return {};
}

std::optional<juce::Colour> parseColour (const juce::String& text)
{
    auto hex = text.trim().removeCharacters ("#");

    if ((hex.length() != 6 && hex.length() != 8) || ! hex.containsOnly ("0123456789abcdefABCDEF"))
        return std::nullopt;

    return juce::Colour::fromString (hex.paddedLeft ('f', 8));
}

//==============================================================================
Session::Session (te::Engine& e)
    : engine (e)
{
    adoptEdit (createNewEdit(), {});
}

std::unique_ptr<te::Edit> Session::createNewEdit()
{
    te::Edit::Options options { engine };
    options.editState = te::createEmptyEdit (engine);
    options.editProjectItemID = te::ProjectItemID::createNewID (0);
    options.numAudioTracks = 0;
    auto newEdit = te::Edit::createEdit (std::move (options));

    // New projects loop by default, so Play repeats what's there.
    newEdit->getTransport().looping = true;
    return newEdit;
}

void Session::adoptEdit (std::unique_ptr<te::Edit> newEdit, const juce::File& newFile)
{
    if (edit != nullptr)
    {
        if (onEditAboutToChange != nullptr)
            onEditAboutToChange();

        edit->getTransport().stop (false, true);
    }

    edit = std::move (newEdit);
    file = newFile;

    // Tracktion creates the scene list lazily, inside the first track insertion. That
    // nested change lands in the undo history out of order, so the first undo would
    // fail. Create it now, before the history starts.
    edit->getSceneList();

    // Tracktion gives every track a level meter but not the master output, so add one there.
    if (getMasterMeter() == nullptr)
        if (auto meter = edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}))
            edit->getMasterPluginList().insertPlugin (meter, -1, nullptr);

    updateLoopRange();
    edit->getUndoManager().clearUndoHistory();
    markAsSaved();
}

//==============================================================================
void Session::newProject()
{
    adoptEdit (createNewEdit(), {});
}

juce::String Session::openProject (const juce::File& projectFile)
{
    if (! projectFile.existsAsFile())
        return "File not found: " + projectFile.getFullPathName();

    // Check it's a project before replacing the open one. Tracktion would quietly give an
    // empty project for a file it can't read.
    auto xml = juce::parseXML (projectFile);

    if (xml == nullptr || ! xml->hasTagName (te::IDs::EDIT.toString()))
        return "Not an Ng DAW project: " + projectFile.getFullPathName();

    auto loaded = te::loadEditFromFile (engine, projectFile);

    if (loaded == nullptr)
        return "Couldn't open " + projectFile.getFullPathName();

    adoptEdit (std::move (loaded), projectFile);
    return {};
}

juce::String Session::saveProject (const juce::File& projectFile)
{
    auto target = projectFile == juce::File() ? file : projectFile;

    if (target == juce::File())
        return "This project hasn't been saved yet, so give a path to save it to";

    if (target.isDirectory())
        return "That's a folder, not a file: " + target.getFullPathName();

    if (! target.getParentDirectory().createDirectory())
        return "Couldn't create the folder for " + target.getFullPathName();

    // Write next to the target first, so a failed save never leaves a half-written project.
    juce::TemporaryFile temp (target);

    if (! te::EditFileOperations (*edit).writeToFile (temp.getFile(), false) || ! temp.overwriteTargetFileWithTemporary())
        return "Couldn't save to " + target.getFullPathName();

    file = target;
    markAsSaved();
    return {};
}

juce::String Session::getProjectName() const
{
    return file == juce::File() ? juce::String ("Untitled") : file.getFileNameWithoutExtension();
}

juce::String Session::getComparableState() const
{
    // The playhead position and the audio and MIDI inputs Tracktion finds when playback
    // starts are saved with the project, but neither is a change the user made.
    auto copy = edit->state.createCopy();
    copy.removeChild (copy.getChildWithName (te::IDs::INPUTDEVICES), nullptr);
    auto transport = copy.getChildWithName (te::IDs::TRANSPORT);
    transport.removeProperty (te::IDs::position, nullptr);
    transport.removeProperty (te::IDs::start, nullptr);
    copy.removeProperty (te::IDs::lastSignificantChange, nullptr);
    return copy.toXmlString();
}

void Session::markAsSaved()
{
    savedState = getComparableState();
}

bool Session::hasUnsavedChanges() const
{
    return getComparableState() != savedState;
}

Session::~Session()
{
    edit->getTransport().stop (false, true);
}

//==============================================================================
Session::Role Session::getRole (const te::Track& track)
{
    auto value = track.state[roleProp].toString();

    if (value == "channel")  return Role::channel;
    if (value == "strip")    return Role::strip;

    return Role::none;
}

void Session::setRole (te::Track& track, Role role)
{
    track.state.setProperty (roleProp, toString (role), &getUndoManager());
}

juce::Array<te::AudioTrack*> Session::getChannels() const
{
    juce::Array<te::AudioTrack*> result;

    for (auto t : te::getAudioTracks (*edit))
        if (getRole (*t) == Role::channel)
            result.add (t);

    return result;
}

juce::Array<te::AudioTrack*> Session::getStrips() const
{
    juce::Array<te::AudioTrack*> result;

    for (auto t : te::getAudioTracks (*edit))
        if (getRole (*t) == Role::strip)
            result.add (t);

    return result;
}

te::AudioTrack* Session::findTrack (const juce::String& idOrName, Role role) const
{
    auto tracks = role == Role::channel ? getChannels() : getStrips();

    for (auto t : tracks)
        if (t->itemID.toString() == idOrName)
            return t;

    for (auto t : tracks)
        if (t->getName() == idOrName)
            return t;

    return nullptr;
}

te::AudioTrack* Session::getOwnStrip (const te::AudioTrack& channel) const
{
    auto id = te::EditItemID::fromProperty (channel.state, ownStripProp);

    if (! id.isValid())
        return nullptr;

    return dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, id));
}

void Session::setOwnStrip (te::AudioTrack& channel, te::AudioTrack* strip)
{
    if (strip != nullptr)
        channel.state.setProperty (ownStripProp, strip->itemID.toString(), &getUndoManager());
    else
        channel.state.removeProperty (ownStripProp, &getUndoManager());
}

te::AudioTrack* Session::getDestinationStrip (const te::AudioTrack& channel) const
{
    return channel.getOutput().getDestinationTrack();
}

bool Session::isLinked (const te::AudioTrack& channel) const
{
    auto own = getOwnStrip (channel);
    return own != nullptr && getDestinationStrip (channel) == own;
}

juce::String Session::getSteps (const te::AudioTrack& channel)
{
    auto steps = channel.state["ngSteps"].toString();
    return steps.isEmpty() ? juce::String::repeatedString (".", 16) : steps;
}

bool Session::isPatternClip (const te::Clip& clip)
{
    return clip.state["ngPattern"];
}

juce::String Session::getPositionText() const
{
    auto& tempo = edit->tempoSequence;
    auto position = tempo.toBarsAndBeats (edit->getTransport().getPosition());

    // A small nudge so 2.9999 reads as the next sixteenth, as it will sound.
    auto beatsInBar = position.beats.inBeats() + 1.0e-6;
    auto beat = (int) std::floor (beatsInBar);
    auto sixteenth = (int) std::floor ((beatsInBar - beat) * 4.0);

    return juce::String (position.bars + 1) + "." + juce::String (beat + 1) + "." + juce::String (sixteenth + 1);
}

te::Clip* Session::findClip (const juce::String& id) const
{
    for (auto channel : getChannels())
        for (auto clip : channel->getClips())
            if (clip->itemID.toString() == id)
                return clip;

    return nullptr;
}

juce::Array<ClipNote> Session::getNotes (const te::MidiClip& clip)
{
    // Tracktion times notes from where the clip's content starts, which is earlier than
    // the clip's start when its front has been trimmed.
    auto offset = clip.getOffsetInBeats().inBeats();
    juce::Array<ClipNote> notes;

    for (auto n : clip.getSequence().getNotes())
        notes.add ({ n->getNoteNumber(), n->getStartBeat().inBeats() - offset, n->getLengthBeats().inBeats(), n->getVelocity() });

    std::stable_sort (notes.begin(), notes.end(), [] (const ClipNote& a, const ClipNote& b)
    {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });

    return notes;
}

int Session::getStepPitch (const te::AudioTrack& channel)
{
    return channel.state.getProperty ("ngStepPitch", 60);
}

te::AudioTrack* Session::getLinkedPartner (const te::AudioTrack& track) const
{
    if (getRole (track) == Role::channel)
        return isLinked (track) ? getOwnStrip (track) : nullptr;

    if (getRole (track) == Role::strip)
        for (auto c : getChannels())
            if (getOwnStrip (*c) == &track && isLinked (*c))
                return c;

    return nullptr;
}

void Session::sortTrackContents()
{
    // Each track: the same order as Tracktion's ClipList::sortClips (tracktion_ClipOwner.cpp).
    struct Sorter
    {
        static int getPriority (const juce::Identifier& type)
        {
            if (type == te::IDs::AUTOMATIONTRACK)  return 0;
            if (te::Clip::isClipState (type))      return 1;
            if (type == te::IDs::PLUGIN)           return 2;
            if (type == te::IDs::OUTPUTDEVICES)    return 3;
            if (type == te::IDs::LFOS)             return 4;

            return -1;
        }

        int compareElements (const juce::ValueTree& first, const juce::ValueTree& second) const noexcept
        {
            auto priority = getPriority (first.getType()) - getPriority (second.getType());

            if (priority != 0)
                return priority;

            if (te::Clip::isClipState (first) && te::Clip::isClipState (second))
            {
                double t1 = first[te::IDs::start];
                double t2 = second[te::IDs::start];
                return t1 < t2 ? -1 : (t2 < t1 ? 1 : 0);
            }

            return 0;
        }
    };

    Sorter sorter;

    // Sorting moves only children that are out of order, so this adds nothing when it's tidy.
    for (auto t : te::getAudioTracks (*edit))
        t->state.sort (sorter, &getUndoManager(), true);

    // Tracktion also keeps the Edit's own children in order, tracks after everything else.
    te::TrackList::sortTracksByType (edit->state, &getUndoManager());
}

void Session::writeTrackOutputs()
{
    for (auto t : te::getAudioTracks (*edit))
        t->getOutput().flushStateToValueTree();
}

void Session::refreshTrackOutputs()
{
    for (auto t : te::getAudioTracks (*edit))
        t->getOutput().updateOutput();
}

void Session::refreshAfterHistoryChange()
{
    refreshTrackOutputs();

    // Only touch parameters that are out of step: updating one writes its value back to the
    // edit, and an edit change after an undo would clear the redo history.
    auto refresh = [] (te::VolumeAndPanPlugin* vp)
    {
        if (vp == nullptr)
            return;

        if (vp->volParam->getCurrentBaseValue() != vp->volume.get())
            vp->volParam->updateFromAttachedValue();

        if (vp->panParam->getCurrentBaseValue() != vp->pan.get())
            vp->panParam->updateFromAttachedValue();
    };

    for (auto t : te::getAudioTracks (*edit))
        refresh (t->getVolumePlugin());

    refresh (edit->getMasterVolumePlugin().get());
    edit->updateMuteSoloStatuses();
}

te::AudioTrack* Session::findAnyTrack (const juce::String& idOrName) const
{
    if (auto t = findTrack (idOrName, Role::channel))
        return t;

    return findTrack (idOrName, Role::strip);
}

//==============================================================================
// The custom loop lives on the transport state, which Tracktion keeps out of the undo
// history (like the play position), and is saved with the project.
bool Session::hasCustomLoop() const
{
    return edit->getTransport().state.hasProperty (loopEndProp);
}

juce::Range<double> Session::getLoopBeats() const
{
    auto& transportState = edit->getTransport().state;

    if (hasCustomLoop())
        return { static_cast<double> (transportState[loopStartProp]), static_cast<double> (transportState[loopEndProp]) };

    auto& tempo = edit->tempoSequence;
    auto bars = tempo.toBarsAndBeats (te::toPosition (edit->getLength()));
    auto wholeBars = std::max (1, bars.bars + (bars.beats > te::BeatDuration::fromBeats (1.0e-6) ? 1 : 0));
    auto endBeat = tempo.toBeats (te::tempo::BarsAndBeats { wholeBars, {} }).inBeats();
    return { 0.0, std::round (endBeat * 1.0e6) / 1.0e6 };   // tidy float noise like 6.000000000000001
}

void Session::setCustomLoop (std::optional<juce::Range<double>> beats)
{
    auto transportState = edit->getTransport().state;

    if (beats)
    {
        transportState.setProperty (loopStartProp, beats->getStart(), nullptr);
        transportState.setProperty (loopEndProp, beats->getEnd(), nullptr);
    }
    else
    {
        transportState.removeProperty (loopStartProp, nullptr);
        transportState.removeProperty (loopEndProp, nullptr);
    }

    updateLoopRange();
}

void Session::updateLoopRange()
{
    auto beats = getLoopBeats();
    auto& tempo = edit->tempoSequence;
    te::TimeRange range (tempo.toTime (te::BeatPosition::fromBeats (beats.getStart())),
                         tempo.toTime (te::BeatPosition::fromBeats (beats.getEnd())));

    auto& transport = edit->getTransport();

    if (transport.getLoopRange() != range)
        transport.setLoopRange (range);
}

te::LevelMeterPlugin* Session::getMasterMeter() const
{
    return edit->getMasterPluginList().getPluginsOfType<te::LevelMeterPlugin>().getLast();
}

juce::Array<juce::Colour> Session::getPalette()
{
    return { juce::Colour (0xffe0603c), juce::Colour (0xffe8b23a), juce::Colour (0xff8cc84b), juce::Colour (0xff3cb4a0),
             juce::Colour (0xff3c8ce0), juce::Colour (0xff8c64dc), juce::Colour (0xffd85aa8), juce::Colour (0xff9aa0a6) };
}

//==============================================================================
juce::var Session::toVar() const
{
    juce::Array<juce::var> channels, strips;

    for (auto c : getChannels())
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty ("id", c->itemID.toString());
        obj->setProperty ("name", c->getName());
        obj->setProperty ("colour", colourToString (c->getColour()));

        if (auto dest = getDestinationStrip (*c))
            obj->setProperty ("output", dest->itemID.toString());
        else
            obj->setProperty ("output", "master");

        obj->setProperty ("linked", isLinked (*c));

        juce::Array<juce::var> plugins;

        for (auto p : c->pluginList)
            if (! (dynamic_cast<te::VolumeAndPanPlugin*> (p) || dynamic_cast<te::LevelMeterPlugin*> (p)))
                plugins.add (p->getPluginType());

        obj->setProperty ("plugins", plugins);

        juce::String instrument ("none");

        for (auto p : c->pluginList)
            if (p->isSynth())
                instrument = p->getPluginType();

        obj->setProperty ("instrument", instrument);
        auto pattern = new juce::DynamicObject();
        pattern->setProperty ("steps", getSteps (*c));
        pattern->setProperty ("pitch", getStepPitch (*c));
        obj->setProperty ("pattern", juce::var (pattern));

        juce::Array<juce::var> clips;
        auto& tempo = edit->tempoSequence;

        for (auto clip : c->getClips())
        {
            auto pos = clip->getPosition();
            auto startBeat = tempo.toBeats (pos.getStart()).inBeats();
            auto endBeat = tempo.toBeats (pos.getEnd()).inBeats();

            auto clipObj = new juce::DynamicObject();
            clipObj->setProperty ("id", clip->itemID.toString());
            clipObj->setProperty ("name", clip->getName());
            clipObj->setProperty ("start_beat", std::round (startBeat * 1.0e6) / 1.0e6);
            clipObj->setProperty ("length_beats", std::round ((endBeat - startBeat) * 1.0e6) / 1.0e6);
            clipObj->setProperty ("pattern", isPatternClip (*clip));

            if (auto midi = dynamic_cast<te::MidiClip*> (clip))
                clipObj->setProperty ("notes", midi->getSequence().getNumNotes());

            clips.add (juce::var (clipObj));
        }

        obj->setProperty ("clips", clips);
        channels.add (juce::var (obj));
    }

    for (auto s : getStrips())
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty ("id", s->itemID.toString());
        obj->setProperty ("name", s->getName());
        obj->setProperty ("colour", colourToString (s->getColour()));

        juce::Array<juce::var> inputs;

        for (auto c : getChannels())
            if (getDestinationStrip (*c) == s)
                inputs.add (c->itemID.toString());

        obj->setProperty ("inputs", inputs);

        if (auto dest = s->getOutput().getDestinationTrack())
            obj->setProperty ("output", dest->itemID.toString());
        else
            obj->setProperty ("output", "master");

        if (auto vp = s->getVolumePlugin())
        {
            obj->setProperty ("volume_db", vp->getVolumeDb());
            obj->setProperty ("pan", vp->getPan());
        }

        obj->setProperty ("mute", s->isMuted (false));
        obj->setProperty ("solo", s->isSolo (false));

        strips.add (juce::var (obj));
    }

    auto& transport = edit->getTransport();

    auto transportObj = new juce::DynamicObject();
    transportObj->setProperty ("playing", transport.isPlaying());
    transportObj->setProperty ("position_seconds", transport.getPosition().inSeconds());
    transportObj->setProperty ("position_beats", std::round (edit->tempoSequence.toBeats (transport.getPosition()).inBeats() * 1.0e6) / 1.0e6);
    transportObj->setProperty ("position", getPositionText());
    transportObj->setProperty ("looping", transport.looping.get());
    transportObj->setProperty ("metronome", edit->clickTrackEnabled.get());

    auto loopBeats = getLoopBeats();
    transportObj->setProperty ("loop_start_beat", loopBeats.getStart());
    transportObj->setProperty ("loop_end_beat", loopBeats.getEnd());
    transportObj->setProperty ("loop_is_whole_project", ! hasCustomLoop());

    auto master = new juce::DynamicObject();

    if (auto vp = edit->getMasterVolumePlugin())
    {
        master->setProperty ("volume_db", vp->getVolumeDb());
        master->setProperty ("pan", vp->getPan());
    }

    auto root = new juce::DynamicObject();
    auto& timeSig = edit->tempoSequence.getTimeSigAt (te::TimePosition());
    auto timeSigObj = new juce::DynamicObject();
    timeSigObj->setProperty ("numerator", timeSig.numerator.get());
    timeSigObj->setProperty ("denominator", timeSig.denominator.get());

    root->setProperty ("tempo_bpm", edit->tempoSequence.getBpmAt (te::TimePosition()));
    root->setProperty ("time_signature", juce::var (timeSigObj));
    root->setProperty ("channels", channels);
    root->setProperty ("strips", strips);
    root->setProperty ("master", juce::var (master));
    root->setProperty ("transport", juce::var (transportObj));

    auto project = new juce::DynamicObject();
    project->setProperty ("name", getProjectName());
    project->setProperty ("path", file.getFullPathName());
    project->setProperty ("unsaved_changes", hasUnsavedChanges());
    root->setProperty ("project", juce::var (project));
    return juce::var (root);
}

} // namespace ng
