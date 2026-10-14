#pragma once

#include "AppSettings.h"

namespace ng
{

/**
    The Settings window: the audio and MIDI devices (and the buffer size, with advice from the
    CPU meter), the colour theme, the default project folder and how often to autosave.
*/
class PreferencesPanel  : public juce::Component
{
public:
    PreferencesPanel (Session&, std::function<void()> onChanged);

    void resized() override;

private:
    class General;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesPanel)
};

/** The CPU meter's advice about the buffer size, or "" while all is well. */
juce::String getBufferAdvice (te::Engine&, int dropoutsSinceLastCheck);

} // namespace ng
