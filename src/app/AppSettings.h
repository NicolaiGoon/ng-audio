#pragma once

#include "core/Session.h"
#include "Theme.h"

namespace ng
{

/**
    The app's preferences, kept in the engine's settings file with its other settings: the
    colour theme, the default project folder and how often to autosave.
*/
namespace settings
{
    inline juce::PropertiesFile& file (te::Engine& engine)     { return engine.getPropertyStorage().getPropertiesFile(); }

    /** The colour themes on offer. The app's own drawing is dark, so they're all dark. */
    inline juce::StringArray getThemes()                      { return { "Dark", "Midnight", "Grey" }; }

    inline juce::String getTheme (te::Engine& engine)
    {
        auto theme = file (engine).getValue ("ngTheme", "Dark");
        return getThemes().contains (theme) ? theme : juce::String ("Dark");
    }

    /** Applies the theme to every window. */
    inline void applyTheme (const juce::String& theme)
    {
        auto scheme = theme == "Midnight" ? juce::LookAndFeel_V4::getMidnightColourScheme()
                    : theme == "Grey"     ? juce::LookAndFeel_V4::getGreyColourScheme()
                                          : juce::LookAndFeel_V4::getDarkColourScheme();

        if (auto* lf = dynamic_cast<juce::LookAndFeel_V4*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        {
            lf->setColourScheme (scheme);

            if (auto* ng = dynamic_cast<NgLookAndFeel*> (lf))
                ng->applyAccents();
        }

        for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
            if (auto* window = juce::TopLevelWindow::getTopLevelWindow (i))
            {
                if (auto* document = dynamic_cast<juce::DocumentWindow*> (window))
                    document->setBackgroundColour (scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::windowBackground));

                window->sendLookAndFeelChange();
                window->repaint();
            }
    }

    inline void setTheme (te::Engine& engine, const juce::String& theme)
    {
        file (engine).setValue ("ngTheme", theme);
        applyTheme (theme);
    }

    /** Where projects go by default; empty for Music/Ng DAW. */
    inline juce::File getProjectFolder (te::Engine& engine)
    {
        auto path = file (engine).getValue ("ngProjectFolder");
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }

    inline void setProjectFolder (te::Engine& engine, const juce::File& folder)
    {
        file (engine).setValue ("ngProjectFolder", folder.getFullPathName());
    }

    /** Minutes between autosaves; 0 turns autosave off. */
    inline juce::Array<int> getAutosaveChoices()              { return { 0, 1, 2, 5, 10 }; }

    inline int getAutosaveMinutes (te::Engine& engine)
    {
        auto minutes = file (engine).getIntValue ("ngAutosaveMinutes", 1);
        return getAutosaveChoices().contains (minutes) ? minutes : 1;
    }

    inline void setAutosaveMinutes (te::Engine& engine, int minutes)
    {
        file (engine).setValue ("ngAutosaveMinutes", minutes);
    }
}

} // namespace ng
