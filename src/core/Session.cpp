#include "Session.h"

namespace ng
{
namespace
{
    const juce::Identifier loopStartProp ("ngLoopStartBeat"), loopEndProp ("ngLoopEndBeat");
    const juce::Identifier roleProp ("ngRole");
    const juce::Identifier ownStripProp ("ngOwnStrip");
    const juce::Identifier audioChannelProp ("ngAudio");
    const juce::Identifier audioInputProp ("ngInput"), monitorProp ("ngMonitor");

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

    // Racks left behind by multi-output instruments that were turned off or deleted.
    auto& racks = edit->getRackList();

    auto plugins = te::getAllPlugins (*edit, false);

    for (int i = racks.size(); --i >= 0;)
    {
        auto rack = racks.getRackType (i);
        auto used = std::any_of (plugins.begin(), plugins.end(), [&] (te::Plugin* p)
        {
            auto ri = dynamic_cast<te::RackInstance*> (p);
            return ri != nullptr && ri->type == rack;
        });

        if (rack != nullptr && ! used)
            racks.removeRackType (rack);
    }

    armedChannel = {};
    armedAudio.clear();
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

bool Session::isAudioChannel (const te::AudioTrack& channel)
{
    return channel.state[audioChannelProp];
}

void Session::setAudioChannel (te::AudioTrack& channel)
{
    channel.state.setProperty (audioChannelProp, true, &getUndoManager());
}

void Session::describeAudioClip (te::WaveAudioClip& clip, juce::DynamicObject& obj) const
{
    auto file = clip.getOriginalFile();
    obj.setProperty ("file", file.getFullPathName());

    if (! file.existsAsFile())
        obj.setProperty ("missing", true);

    auto rounded = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };

    // Unless it loops, a clip ends where its file does.
    if (! clip.isLooping())
        obj.setProperty ("max_end_beat", rounded (getSourceEndBeat (clip)));

    auto& tempo = edit->tempoSequence;
    auto pos = clip.getPosition();
    auto beatsOf = [&] (te::TimePosition from, te::TimeDuration length)
    {
        return rounded (tempo.toBeats (from + length).inBeats() - tempo.toBeats (from).inBeats());
    };

    obj.setProperty ("gain_db", std::round (clip.getGainDB() * 100.0) / 100.0);
    obj.setProperty ("fade_in_beats", beatsOf (pos.getStart(), clip.getFadeIn()));
    obj.setProperty ("fade_out_beats", beatsOf (pos.getEnd() - clip.getFadeOut(), clip.getFadeOut()));
    obj.setProperty ("follow_tempo", clip.getAutoTempo());

    if (clip.getAutoTempo())
        obj.setProperty ("source_bpm", std::round (clip.getLoopInfo().getBpm (clip.getAudioFile().getInfo()) * 100.0) / 100.0);
}

double Session::getSourceStartBeat (te::WaveAudioClip& clip) const
{
    auto& tempo = edit->tempoSequence;
    return tempo.toBeats (clip.getPosition().getStart()).inBeats() - clip.getOffsetInBeats().inBeats();
}

double Session::getSourceEndBeat (te::WaveAudioClip& clip) const
{
    if (clip.getAutoTempo())
        return getSourceStartBeat (clip) + clip.getLoopInfo().getNumBeats();

    auto pos = clip.getPosition();
    return edit->tempoSequence.toBeats (pos.getStartOfSource() + clip.getSourceLength() / clip.getSpeedRatio()).inBeats();
}

juce::String Session::getSteps (const te::AudioTrack& channel)
{
    auto steps = channel.state["ngSteps"].toString();
    return steps.isEmpty() ? juce::String::repeatedString (".", 16) : steps;
}

te::Plugin* Session::getInstrument (const te::AudioTrack& channel)
{
    if (auto rack = getMultiOutputRack (channel))
        return rack->type != nullptr ? rack->type->getPlugins().getFirst() : nullptr;

    // A plugin that failed to load can't say it's a synth, but a channel only holds its
    // instrument (and its volume and meter), so any external plugin is the instrument.
    for (auto p : channel.pluginList)
        if (p->isSynth() || dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
            return p;

    return nullptr;
}

namespace
{
    const juce::Identifier outputOfProp ("ngOutputOf"), outputPairProp ("ngOutputPair");
}

te::RackInstance* Session::getMultiOutputRack (const te::AudioTrack& channel)
{
    return channel.pluginList.findFirstPluginOfType<te::RackInstance>();
}

juce::Array<te::AudioTrack*> Session::getOutputStrips (const te::AudioTrack& channel) const
{
    juce::Array<te::AudioTrack*> strips;

    for (auto s : getStrips())
        if (getOutputOf (*s) == &channel)
            strips.add (s);

    std::sort (strips.begin(), strips.end(), [] (auto a, auto b) { return getOutputPair (*a) < getOutputPair (*b); });
    return strips;
}

te::AudioTrack* Session::getOutputOf (const te::AudioTrack& strip) const
{
    auto id = te::EditItemID::fromProperty (strip.state, outputOfProp);

    if (! id.isValid())
        return nullptr;

    for (auto c : getChannels())
        if (c->itemID == id)
            return c;

    return nullptr;
}

int Session::getOutputPair (const te::AudioTrack& strip)
{
    return strip.state.getProperty (outputPairProp, -1);
}

void Session::setOutputOf (te::AudioTrack& strip, const te::AudioTrack& channel, int pair)
{
    auto* um = &channel.edit.getUndoManager();
    strip.state.setProperty (outputOfProp, channel.itemID.toString(), um);
    strip.state.setProperty (outputPairProp, pair, um);
}

int Session::getNumOutputs (te::Plugin& instrument)
{
    juce::StringArray ins, outs;
    instrument.getChannelNames (&ins, &outs);
    return outs.size();
}

te::SamplerPlugin* Session::getSampler (const te::AudioTrack& channel)
{
    return dynamic_cast<te::SamplerPlugin*> (getInstrument (channel));
}

juce::var Session::describeSamples (te::SamplerPlugin& sampler)
{
    juce::Array<juce::var> samples;
    int index = 0;

    for (auto v : sampler.state)
    {
        if (! v.hasType (te::IDs::SOUND))
            continue;

        auto file = te::SourceFileReference::findFileFromString (sampler.edit, v[te::IDs::source].toString());
        auto obj = new juce::DynamicObject();
        obj->setProperty ("index", index++);
        obj->setProperty ("name", v[te::IDs::name].toString());
        obj->setProperty ("file", file.getFullPathName());
        obj->setProperty ("note", juce::jlimit (0, 127, static_cast<int> (v[te::IDs::keyNote])));
        obj->setProperty ("min_note", juce::jlimit (0, 127, static_cast<int> (v[te::IDs::minNote])));
        obj->setProperty ("max_note", juce::jlimit (0, 127, static_cast<int> (v[te::IDs::maxNote])));
        obj->setProperty ("gain_db", std::round (static_cast<double> (v[te::IDs::gainDb]) * 100.0) / 100.0);
        obj->setProperty ("pan", std::round (static_cast<double> (v[te::IDs::pan]) * 100.0) / 100.0);
        obj->setProperty ("one_shot", static_cast<bool> (v[te::IDs::openEnded]));

        if (! file.existsAsFile())
            obj->setProperty ("missing", true);

        samples.add (juce::var (obj));
    }

    return samples;
}

juce::String Session::getAudioFileWildcard() const
{
    return engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats();
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
        if (! (dynamic_cast<te::VolumeAndPanPlugin*> (p) || dynamic_cast<te::LevelMeterPlugin*> (p)
                 || dynamic_cast<te::AuxSendPlugin*> (p) || dynamic_cast<te::AuxReturnPlugin*> (p)
                 || dynamic_cast<te::RackInstance*> (p)))
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

    // At the end of the effects: before the first fader, meter or send (which come last).
    for (int i = 0; i < list.size(); ++i)
        if (dynamic_cast<te::VolumeAndPanPlugin*> (list[i]) || dynamic_cast<te::LevelMeterPlugin*> (list[i])
              || dynamic_cast<te::AuxSendPlugin*> (list[i]))
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

//==============================================================================
juce::StringArray Session::getAudioInputNames() const
{
    juce::StringArray names;

    for (auto d : engine.getDeviceManager().getWaveInputDevices())
        names.add (d->getName());

    return names;
}

juce::String Session::getAudioInput (const te::AudioTrack& channel)
{
    return channel.state[audioInputProp].toString();
}

juce::String Session::getMonitorMode (const te::AudioTrack& channel)
{
    auto mode = channel.state[monitorProp].toString();
    return mode.isEmpty() ? juce::String ("auto") : mode;
}

void Session::setAudioInput (te::AudioTrack& channel, const juce::String& input)
{
    channel.state.setProperty (audioInputProp, input, &getUndoManager());
}

void Session::setMonitorMode (te::AudioTrack& channel, const juce::String& mode)
{
    channel.state.setProperty (monitorProp, mode, &getUndoManager());
}

bool Session::isAudioArmed (const te::AudioTrack& channel) const
{
    return armedAudio.contains (channel.itemID);
}

void Session::setAudioArmed (te::AudioTrack& channel, bool armed)
{
    if (armed)
        armedAudio.addIfNotAlreadyThere (channel.itemID);
    else
        armedAudio.removeFirstMatchingValue (channel.itemID);

    routeAudioInputs();
}

juce::Array<te::AudioTrack*> Session::getArmedAudioChannels() const
{
    juce::Array<te::AudioTrack*> armed;

    for (auto c : getChannels())
        if (isAudioChannel (*c) && isAudioArmed (*c))
            armed.add (c);

    return armed;
}

juce::File Session::getRecordingFolder() const
{
    if (file != juce::File())
        return file.getParentDirectory().getChildFile ("Recordings");

    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ng DAW").getChildFile ("Recordings");
}

void Session::routeAudioInputs()
{
    auto& deviceManager = engine.getDeviceManager();
    auto devices = deviceManager.getWaveInputDevices();

    // Which audio channels each input feeds.
    std::map<te::WaveInputDevice*, juce::Array<te::AudioTrack*>> users;

    for (auto c : getChannels())
        if (isAudioChannel (*c))
            for (auto d : devices)
                if (d->getName() == getAudioInput (*c))
                    users[d].add (c);

    if (users.empty() && armedAudio.isEmpty())
    {
        bool anyTargets = false;

        for (auto instance : edit->getAllInputDevices())
            if (! instance->getInputDevice().isMidi() && ! instance->getTargets().isEmpty())
                anyTargets = true;

        if (! anyTargets)
            return;
    }

    // Named for the project and channel, e.g. "Beat_Vox_Take_2.wav".
    auto mask = getRecordingFolder().getChildFile (juce::File::createLegalFileName (getProjectName()) + "_%track%_Take_%take%")
                                    .getFullPathName();
    bool enabledAny = false;

    for (auto& [device, channels] : users)
    {
        if (! device->isEnabled())
        {
            device->setEnabled (true);
            enabledAny = true;
        }

        if (device->getFilenameMask() != mask)
            device->setFilenameMask (mask);

        // An input is heard through its channels if any of them asks for it: always, or while armed.
        auto mode = te::InputDevice::MonitorMode::off;

        for (auto c : channels)
        {
            auto wanted = getMonitorMode (*c);

            if (wanted == "on")
                mode = te::InputDevice::MonitorMode::on;
            else if (wanted == "auto" && mode == te::InputDevice::MonitorMode::off)
                mode = te::InputDevice::MonitorMode::automatic;
        }

        if (device->getMonitorMode() != mode)
            device->setMonitorMode (mode);
    }

    if (enabledAny)
        deviceManager.dispatchPendingUpdates();

    edit->getTransport().ensureContextAllocated();

    for (auto instance : edit->getAllInputDevices())
    {
        if (instance->getInputDevice().isMidi())
            continue;

        auto wave = dynamic_cast<te::WaveInputDevice*> (&instance->getInputDevice());
        auto found = wave != nullptr ? users.find (wave) : users.end();
        juce::Array<te::EditItemID> wanted;

        if (found != users.end())
            for (auto c : found->second)
                wanted.add (c->itemID);

        for (auto target : instance->getTargets())
            if (! wanted.contains (target))
                (void) instance->removeTarget (target, nullptr);

        for (auto id : wanted)
        {
            if (! instance->getTargets().contains (id))
                (void) instance->setTarget (id, false, nullptr, 0);

            instance->setRecordingEnabled (id, armedAudio.contains (id));
        }
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

    routeAudioInputs();

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

    // Send levels are always stored (set_send writes them), so the same goes for them.
    for (auto s : getStrips())
        for (auto send : getSends (*s))
            send->gain->updateFromAttachedValue();

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

int Session::getBusNumber (const te::AudioTrack& strip)
{
    if (auto ret = strip.pluginList.findFirstPluginOfType<te::AuxReturnPlugin>())
        return ret->busNumber.get();

    return -1;
}

te::AudioTrack* Session::findBus (int busNumber) const
{
    if (busNumber < 0)
        return nullptr;

    for (auto s : getStrips())
        if (getBusNumber (*s) == busNumber)
            return s;

    return nullptr;
}

juce::Array<te::AuxSendPlugin*> Session::getSends (const te::AudioTrack& strip)
{
    return strip.pluginList.getPluginsOfType<te::AuxSendPlugin>();
}

bool Session::isPreFader (const te::AuxSendPlugin& send)
{
    auto list = send.getOwnerList();

    if (list == nullptr)
        return false;

    for (auto p : *list)
    {
        if (p == &send)
            return true;

        if (dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr)
            return false;
    }

    return false;
}

bool Session::feeds (const te::AudioTrack& from, const te::AudioTrack& to) const
{
    juce::Array<const te::AudioTrack*> visited;
    juce::Array<const te::AudioTrack*> pending { &from };

    while (! pending.isEmpty())
    {
        auto t = pending.removeAndReturn (pending.size() - 1);

        if (t == &to)
            return true;

        if (visited.contains (t))
            continue;

        visited.add (t);

        if (auto dest = dynamic_cast<te::AudioTrack*> (t->getOutput().getDestinationTrack()))
            pending.add (dest);

        for (auto send : getSends (*t))
            if (auto bus = findBus (send->getBusNumber()))
                pending.add (bus);
    }

    return false;
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
        auto audio = isAudioChannel (*c);
        obj->setProperty ("kind", audio ? "audio" : "instrument");
        obj->setProperty ("instrument", audio ? juce::String ("audio") : getInstrumentId (instrument));
        obj->setProperty ("instrument_name", audio ? juce::String ("Audio")
                                                   : instrument != nullptr ? instrument->getName() : juce::String ("No instrument"));

        // A plugin the project uses that isn't installed (or failed to load) on this computer.
        if (instrument != nullptr && instrument->isMissing())
            obj->setProperty ("instrument_missing", true);

        obj->setProperty ("outputs", instrument != nullptr && ! instrument->isMissing() ? getNumOutputs (*instrument) : 2);
        obj->setProperty ("multi_output", getMultiOutputRack (*c) != nullptr);

        if (auto sampler = getSampler (*c))
            obj->setProperty ("samples", describeSamples (*sampler));

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

            clipObj->setProperty ("type", dynamic_cast<te::WaveAudioClip*> (clip) != nullptr ? "audio" : "midi");

            if (auto midi = dynamic_cast<te::MidiClip*> (clip))
                clipObj->setProperty ("notes", midi->getSequence().getNumNotes());

            if (auto wave = dynamic_cast<te::WaveAudioClip*> (clip))
                describeAudioClip (*wave, *clipObj);

            clips.add (juce::var (clipObj));
        }

        obj->setProperty ("clips", clips);
        obj->setProperty ("armed", c->itemID == armedChannel || armedAudio.contains (c->itemID));

        if (audio)
        {
            obj->setProperty ("input", getAudioInput (*c));
            obj->setProperty ("monitor", getMonitorMode (*c));
        }
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

        auto busNumber = getBusNumber (*s);
        obj->setProperty ("bus", busNumber >= 0);

        if (auto channel = getOutputOf (*s))
        {
            auto pair = getOutputPair (*s);
            obj->setProperty ("output_of", channel->itemID.toString());
            obj->setProperty ("output_pair", juce::String (pair * 2 + 1) + "-" + juce::String (pair * 2 + 2));
        }

        juce::Array<juce::var> sends;

        for (auto send : getSends (*s))
        {
            auto sendObj = new juce::DynamicObject();
            auto bus = findBus (send->getBusNumber());
            sendObj->setProperty ("bus", bus != nullptr ? bus->itemID.toString() : juce::String());
            sendObj->setProperty ("bus_name", bus != nullptr ? bus->getName() : juce::String ("(no bus)"));
            sendObj->setProperty ("level_db", std::round (send->getGainDb() * 100.0) / 100.0);
            sendObj->setProperty ("pre_fader", isPreFader (*send));
            sends.add (juce::var (sendObj));
        }

        obj->setProperty ("sends", sends);

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
    root->setProperty ("audio_inputs", getAudioInputNames());
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
