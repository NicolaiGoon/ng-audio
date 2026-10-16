#pragma once

#include "core/AiChat.h"
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

    //==============================================================================
    /** The chat panel's AI provider. Each provider keeps its own model and API key, so switching
        back and forth doesn't lose them. Keys are kept in this computer's Ng DAW settings file. */
    inline juce::String providerKey (const juce::String& providerName, const char* what)
    {
        return juce::String ("ngAi") + what + "_" + providerName.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    }

    /** The environment variable a provider's key can also come from. */
    inline juce::String getKeyVariable (const juce::String& providerName)
    {
        if (providerName.startsWith ("Claude"))     return "ANTHROPIC_API_KEY";
        if (providerName == "OpenAI")               return "OPENAI_API_KEY";
        if (providerName == "Google Gemini")        return "GEMINI_API_KEY";
        if (providerName == "OpenRouter")           return "OPENROUTER_API_KEY";
        return {};
    }

    /** The chosen provider, with its model, address and key filled in. */
    inline AiChat::Provider getAiProvider (te::Engine& engine, const juce::String& name = {})
    {
        auto& f = file (engine);
        auto provider = AiChat::findProvider (name.isNotEmpty() ? name : f.getValue ("ngAiProvider"));

        provider.model = f.getValue (providerKey (provider.name, "Model"), provider.model);
        provider.apiKey = f.getValue (providerKey (provider.name, "Key"));

        if (provider.baseUrl.isEmpty())
            provider.baseUrl = f.getValue (providerKey (provider.name, "Url"));

        if (provider.apiKey.isEmpty())
            if (auto variable = getKeyVariable (provider.name); variable.isNotEmpty())
                provider.apiKey = juce::SystemStats::getEnvironmentVariable (variable, {});

        return provider;
    }

    inline void setAiProvider (te::Engine& engine, const AiChat::Provider& provider)
    {
        auto& f = file (engine);
        f.setValue ("ngAiProvider", provider.name);
        f.setValue (providerKey (provider.name, "Model"), provider.model);
        f.setValue (providerKey (provider.name, "Key"), provider.apiKey);

        if (AiChat::findProvider (provider.name).baseUrl.isEmpty())
            f.setValue (providerKey (provider.name, "Url"), provider.baseUrl);
    }
}

} // namespace ng
