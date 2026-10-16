#pragma once

#include "AppSettings.h"

namespace ng
{

/**
    The Settings window: the audio and MIDI devices (and the buffer size, with advice from the
    CPU meter), the colour theme, the default project folder, how often to autosave, and the AI
    provider the chat panel uses (and how to connect Claude Desktop or Claude Code).
*/
class PreferencesPanel  : public juce::Component
{
public:
    /** tab picks the page it opens on, e.g. "AI". */
    PreferencesPanel (Session&, std::function<void()> onChanged, const juce::String& tab = {});

    void resized() override;

private:
    class General;
    class Ai;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesPanel)
};

/** NgDawMcp, the MCP server program, next to the app (or in its build folder). */
juce::File findMcpServer();

/** The CPU meter's advice about the buffer size, or "" while all is well. */
juce::String getBufferAdvice (te::Engine&, int dropoutsSinceLastCheck);

} // namespace ng
