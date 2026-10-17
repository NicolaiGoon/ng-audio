#pragma once

#include "Session.h"

namespace ng
{

/** What a command returns: a value on success, or an error message. */
struct CommandResult
{
    bool ok = false;
    juce::var value;
    juce::String error;

    static CommandResult success (juce::var v = {})       { return { true, std::move (v), {} }; }
    static CommandResult failure (juce::String message)   { return { false, {}, std::move (message) }; }

    /** {"ok": true, "result": ...} or {"ok": false, "error": "..."} */
    juce::var toVar() const;
};

/**
    A named action with parameters. Everything that changes the project goes through
    one of these: UI buttons, keyboard shortcuts, macros, undo, and the MCP server
    (which is a thin wrapper listing these specs as tools).
*/
struct CommandSpec
{
    juce::String name;
    juce::String description;

    /** JSON Schema for the params object, published as the MCP tool's input schema. */
    juce::var paramsSchema;

    /** False for commands that don't edit the project (transport, queries). */
    bool undoable = true;

    std::function<CommandResult (Session&, const juce::var& params)> run;
};

class CommandRegistry
{
public:
    void add (CommandSpec);

    const CommandSpec* find (const juce::String& name) const;
    const std::vector<CommandSpec>& getAll() const noexcept     { return specs; }

    /** Every command as {name, description, inputSchema}, ready for MCP tools/list. */
    juce::var describe() const;

private:
    std::vector<CommandSpec> specs;
};

/** Registers create_track, route, rename_track, set_track_colour, delete_track, set_instrument, show_plugin_window,
    set_mixer_strip, new_project, open_project, save_project and get_state, plus every other group of commands below
    (ProjectCommands.cpp). This is the one to call. */
void registerProjectCommands (CommandRegistry&);

/** Registers add_midi_clip, set_steps, move_clip, resize_clip, split_clip, mute_clip, duplicate_clip, delete_clip,
    get_notes, set_notes and quantize_notes (ClipCommands.cpp). */
void registerClipCommands (CommandRegistry&);

/** A clip's notes as get_notes returns them: [{pitch, start, length, velocity}] (ClipCommands.cpp). */
juce::var notesToVar (const juce::Array<ClipNote>&);

/** Registers play, stop, record, arm_channel, set_position, set_metronome, set_tempo, set_time_signature and
    set_loop (TransportCommands.cpp). */
void registerTransportCommands (CommandRegistry&);

/** Registers list_plugins and scan_plugins (PluginCommands.cpp). */
void registerPluginCommands (CommandRegistry&);

/** Registers add_effect, remove_effect, move_effect, set_effect, get_effect and
    show_effect_window (EffectCommands.cpp). */
void registerEffectCommands (CommandRegistry&);

/** Registers create_bus, set_send, remove_send, route_strip and tag_channel (BusCommands.cpp). */
void registerBusCommands (CommandRegistry&);

/** Gives a channel a tag (or none, for ""), creating the tag's bus and routing the channel's
    strip into it, as tag_channel does; command names the command in errors (BusCommands.cpp). */
CommandResult tagChannel (Session&, te::AudioTrack& channel, const juce::String& tag, const juce::String& command);

/** Registers set_multi_output (OutputCommands.cpp). */
void registerOutputCommands (CommandRegistry&);

/** Registers add_sample, set_sample, remove_sample and load_drum_kit (SamplerCommands.cpp). */
void registerSamplerCommands (CommandRegistry&);

/** Registers get_library, set_library, find_samples, save_preset, list_presets and
    load_preset (LibraryCommands.cpp). */
void registerLibraryCommands (CommandRegistry&);

/** Registers import_audio, set_audio_clip and set_audio_input (AudioCommands.cpp). */
void registerAudioCommands (CommandRegistry&);

/** Registers set_automation and get_automation (AutomationCommands.cpp). */
void registerAutomationCommands (CommandRegistry&);

/** A parameter that can be automated: a strip's (or the master's) volume or pan, or one of an
    effect's parameters, as a command names it with "strip", "parameter" and maybe "effect".
    On failure, param is nullptr and error says why (AutomationCommands.cpp). */
struct AutomationTarget
{
    te::AutomatableParameter* param = nullptr;
    te::AudioTrack* strip = nullptr;        // nullptr for the master
    te::Plugin* effect = nullptr;           // nullptr for volume and pan
    juce::String parameterId, name, error;
    bool isVolume = false;                  // values in dB, stored as fader positions
    double min = 0.0, max = 1.0;
};

AutomationTarget findAutomationTarget (Session&, const juce::String& command, const juce::var& params);

/** {strip, effect?, parameter, name, min, max, value, points: [{beat, value}]}, as get_automation returns it. */
juce::var describeAutomation (Session&, const AutomationTarget&);

/** Registers export_audio (ExportCommands.cpp). */
void registerExportCommands (CommandRegistry&);

/** The LAME or FFmpeg program MP3 export uses, or a non-existent file if neither is installed
    (ExportCommands.cpp). */
juce::File findMp3Encoder();

//==============================================================================
/** The strip and effect a command names with "strip" and "effect": the effect by its id,
    its position among the strip's effects (0 is the first), or its name if no other effect
    there shares it. On failure, effect is nullptr and error says why (EffectCommands.cpp). */
struct EffectRef
{
    te::PluginList* list = nullptr;
    te::Plugin* effect = nullptr;
    juce::String error;
};

EffectRef findEffect (Session&, const juce::String& command, const juce::var& params);

/** The effect list of the strip a command names with "strip" (or "master"), or nullptr and an error. */
te::PluginList* findList (Session&, const juce::String& command, const juce::var& params, juce::String& error);

//==============================================================================
/** Small helpers for building schemas and reading params. */
namespace params
{
    juce::var schema (std::initializer_list<std::pair<juce::String, juce::var>> properties,
                      juce::StringArray required = {});
    juce::var prop (const juce::String& type, const juce::String& description);

    juce::String getString (const juce::var& params, const juce::Identifier&, const juce::String& fallback = {});
    double getDouble (const juce::var& params, const juce::Identifier&, double fallback);

    /** An object with these properties, e.g. a command's result: object ({ { "id", id } }). */
    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties);

    /** True for a JSON number (not a string that looks like one). */
    bool isNumber (const juce::var&);

    /** A time in beats rounded to a millionth, tidying float noise like 6.000000000000001. */
    double roundBeats (double beats);
}

} // namespace ng
