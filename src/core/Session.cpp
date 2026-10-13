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

    forEachEffect ([] (te::Plugin& effect) { storeEffectSettings (effect); });

    armedChannel = {};
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

te::Plugin* Session::getInstrument (const te::AudioTrack& channel)
{
    // A plugin that failed to load can't say it's a synth, but a channel only holds its
    // instrument (and its volume and meter), so any external plugin is the instrument.
    for (auto p : channel.pluginList)
        if (p->isSynth() || dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
            return p;

    return nullptr;
}

juce::String Session::getInstrumentId (const te::Plugin* instrument)
{
    return instrument != nullptr ? getPluginId (*instrument) : juce::String ("none");
}

const juce::Array<Session::BuiltInEffect>& Session::getBuiltInEffects()
{
    static const juce::Array<BuiltInEffect> effects {
        BuiltInEffect { "eq",         "EQ",         te::EqualiserPlugin::xmlTypeName },
        BuiltInEffect { "compressor", "Compressor", te::CompressorPlugin::xmlTypeName },
        BuiltInEffect { "reverb",     "Reverb",     te::ReverbPlugin::xmlTypeName },
        BuiltInEffect { "delay",      "Delay",      te::DelayPlugin::xmlTypeName }
    };

    return effects;
}

juce::String Session::getPluginId (const te::Plugin& plugin)
{
    // The id list_plugins gave it. The description saved with the plugin can differ in
    // details (such as how its file is written), which would change the id, so match it
    // against the scanned list the way Tracktion does when loading it.
    if (auto external = dynamic_cast<const te::ExternalPlugin*> (&plugin))
    {
        auto& saved = external->desc;

        for (auto& d : external->edit.engine.getPluginManager().knownPluginList.getTypes())
            if (d.pluginFormatName == saved.pluginFormatName && d.uniqueId == saved.uniqueId && d.name == saved.name)
                return d.createIdentifierString();

        return saved.createIdentifierString();
    }

    auto type = const_cast<te::Plugin&> (plugin).getPluginType();

    for (auto& effect : getBuiltInEffects())
        if (effect.pluginType == type)
            return effect.id;

    return type;
}

te::PluginList* Session::findEffectList (const juce::String& stripIdOrName) const
{
    if (stripIdOrName == "master")
        return &edit->getMasterPluginList();

    if (auto strip = findTrack (stripIdOrName, Role::strip))
        return &strip->pluginList;

    return nullptr;
}

juce::Array<te::Plugin*> Session::getEffects (const te::PluginList& list)
{
    juce::Array<te::Plugin*> effects;

    for (auto p : list)
        if (! (dynamic_cast<te::VolumeAndPanPlugin*> (p) || dynamic_cast<te::LevelMeterPlugin*> (p)))
            effects.add (p);

    return effects;
}

juce::var Session::describeEffect (te::Plugin& effect)
{
    auto obj = new juce::DynamicObject();
    obj->setProperty ("id", effect.itemID.toString());
    obj->setProperty ("type", getPluginId (effect));
    obj->setProperty ("name", effect.getName());
    obj->setProperty ("enabled", effect.isEnabled());

    if (effect.isMissing())
        obj->setProperty ("missing", true);

    return juce::var (obj);
}

int Session::getEffectInsertIndex (const te::PluginList& list, int effectIndex)
{
    auto effects = getEffects (list);

    if (juce::isPositiveAndBelow (effectIndex, effects.size()))
        return list.indexOf (effects[effectIndex]);

    // At the end of the effects: before the first fader or meter (which come last).
    for (int i = 0; i < list.size(); ++i)
        if (dynamic_cast<te::VolumeAndPanPlugin*> (list[i]) || dynamic_cast<te::LevelMeterPlugin*> (list[i]))
            return i;

    return list.size();
}

bool Session::isPatternClip (const te::Clip& clip)
{
    return clip.state["ngPattern"];
}

//==============================================================================
te::AudioTrack* Session::getArmedChannel() const
{
    for (auto c : getChannels())
        if (c->itemID == armedChannel)
            return c;

    return nullptr;
}

void Session::setArmedChannel (te::AudioTrack* channel)
{
    armedChannel = channel != nullptr ? channel->itemID : te::EditItemID();
    routeMidiInputs();
}

namespace
{
    /** The MIDI inputs to route: Tracktion's "All MIDI Ins", which merges every keyboard,
        or each keyboard on its own if that isn't there. */
    std::vector<std::shared_ptr<te::MidiInputDevice>> getMidiInputsToRoute (te::Engine& engine)
    {
        std::vector<std::shared_ptr<te::MidiInputDevice>> all, physical;

        for (auto& d : engine.getDeviceManager().getMidiInDevices())
        {
            if (auto v = dynamic_cast<te::VirtualMidiInputDevice*> (d.get()); v != nullptr && v->useAllInputs)
                all.push_back (d);
            else if (dynamic_cast<te::PhysicalMidiInputDevice*> (d.get()) != nullptr)
                physical.push_back (d);
        }

        return all.empty() ? physical : all;
    }
}

juce::StringArray Session::getMidiInputNames() const
{
    juce::StringArray names;

    for (auto& d : engine.getDeviceManager().getMidiInDevices())
        if (dynamic_cast<te::PhysicalMidiInputDevice*> (d.get()) != nullptr)
            names.add (d->getName());

    return names;
}

void Session::routeMidiInputs()
{
    auto& deviceManager = engine.getDeviceManager();

    // Keyboards are switched on, so "All MIDI Ins" hears them.
    for (auto& d : deviceManager.getMidiInDevices())
        if (! d->isEnabled())
            d->setEnabled (true);

    auto armed = getArmedChannel();
    auto inputs = getMidiInputsToRoute (engine);

    if (armed == nullptr && inputs.empty())
        return;

    edit->getTransport().ensureContextAllocated();

    for (auto instance : edit->getAllInputDevices())
    {
        if (! instance->getInputDevice().isMidi())
            continue;

        for (auto target : instance->getTargets())
            if (target != armedChannel || armed == nullptr)
                (void) instance->removeTarget (target, nullptr);

        auto wanted = std::any_of (inputs.begin(), inputs.end(),
                                   [instance] (auto& d) { return d.get() == &instance->getInputDevice(); });

        if (armed != nullptr && wanted)
        {
            if (! instance->getTargets().contains (armedChannel))
                (void) instance->setTarget (armedChannel, false, nullptr, 0);

            instance->setRecordingEnabled (armedChannel, true);
        }
    }
}

void Session::sendLiveMidi (const juce::MidiMessage& message)
{
    for (auto& d : getMidiInputsToRoute (engine))
    {
        d->handleIncomingMidiMessage (nullptr, message);
        return;
    }
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

    // An undone delete brings an armed channel back; point the MIDI inputs at it again.
    if (getArmedChannel() != nullptr)
        routeMidiInputs();

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

    // Effect settings are written out in full when an effect is added (storeEffectSettings),
    // so bringing a parameter back in line writes nothing new.
    forEachEffect ([] (te::Plugin& effect)
    {
        if (dynamic_cast<te::ExternalPlugin*> (&effect) == nullptr)
            for (auto param : effect.getAutomatableParameters())
                param->updateFromAttachedValue();
    });

    edit->updateMuteSoloStatuses();
}

void Session::forEachEffect (const std::function<void (te::Plugin&)>& fn) const
{
    for (auto s : getStrips())
        for (auto effect : getEffects (s->pluginList))
            fn (*effect);

    for (auto effect : getEffects (edit->getMasterPluginList()))
        fn (*effect);
}

void Session::storeEffectSettings (te::Plugin& effect)
{
    // A setting still at its default isn't stored, and undoing a change to it would remove
    // it rather than put the old value back; Tracktion then writes the default back, which
    // clears the redo history. Storing every setting avoids that.
    if (dynamic_cast<te::ExternalPlugin*> (&effect) != nullptr)
        return;

    for (auto param : effect.getAutomatableParameters())
        param->setParameter (param->getCurrentValue(), juce::sendNotificationSync);

    if (auto delay = dynamic_cast<te::DelayPlugin*> (&effect))
        if (delay->lengthMs.isUsingDefault())
            delay->lengthMs = delay->lengthMs.get();
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
namespace
{
    juce::var describeEffects (const te::PluginList& list)
    {
        juce::Array<juce::var> effects;

        for (auto effect : Session::getEffects (list))
            effects.add (Session::describeEffect (*effect));

        return effects;
    }
}

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

        auto instrument = getInstrument (*c);
        obj->setProperty ("instrument", getInstrumentId (instrument));
        obj->setProperty ("instrument_name", instrument != nullptr ? instrument->getName() : juce::String ("No instrument"));

        // A plugin the project uses that isn't installed (or failed to load) on this computer.
        if (instrument != nullptr && instrument->isMissing())
            obj->setProperty ("instrument_missing", true);
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
        obj->setProperty ("armed", c->itemID == armedChannel);
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
        obj->setProperty ("effects", describeEffects (s->pluginList));

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
    transportObj->setProperty ("recording", transport.isRecording());

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

    master->setProperty ("effects", describeEffects (edit->getMasterPluginList()));

    auto root = new juce::DynamicObject();
    auto& timeSig = edit->tempoSequence.getTimeSigAt (te::TimePosition());
    auto timeSigObj = new juce::DynamicObject();
    timeSigObj->setProperty ("numerator", timeSig.numerator.get());
    timeSigObj->setProperty ("denominator", timeSig.denominator.get());

    root->setProperty ("tempo_bpm", edit->tempoSequence.getBpmAt (te::TimePosition()));
    root->setProperty ("midi_inputs", getMidiInputNames());
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
