#include "Session.h"

namespace ng
{
namespace
{
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
    te::Edit::Options options { engine };
    options.editState = te::createEmptyEdit (engine);
    options.editProjectItemID = te::ProjectItemID::createNewID (0);
    options.numAudioTracks = 0;
    edit = te::Edit::createEdit (std::move (options));

    // Tracktion creates the scene list lazily, inside the first track insertion. That
    // nested change lands in the undo history out of order, so the first undo would
    // fail. Create it now, before the history starts.
    edit->getSceneList();

    // Tracktion gives every track a level meter but not the master output, so add one there.
    if (getMasterMeter() == nullptr)
        if (auto meter = edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}))
            edit->getMasterPluginList().insertPlugin (meter, -1, nullptr);

    edit->getUndoManager().clearUndoHistory();
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
        obj->setProperty ("clips", c->getClips().size());
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
    transportObj->setProperty ("looping", transport.looping.get());

    auto master = new juce::DynamicObject();

    if (auto vp = edit->getMasterVolumePlugin())
    {
        master->setProperty ("volume_db", vp->getVolumeDb());
        master->setProperty ("pan", vp->getPan());
    }

    auto root = new juce::DynamicObject();
    root->setProperty ("tempo_bpm", edit->tempoSequence.getBpmAt (te::TimePosition()));
    root->setProperty ("channels", channels);
    root->setProperty ("strips", strips);
    root->setProperty ("master", juce::var (master));
    root->setProperty ("transport", juce::var (transportObj));
    return juce::var (root);
}

} // namespace ng
