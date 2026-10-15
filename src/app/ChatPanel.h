#pragma once

#include "AppSettings.h"
#include "core/AiChat.h"

namespace ng
{

/**
    The AI chat beside the project: type what you want ("add a bass line following the chords")
    and the AI provider picked in Settings > AI makes the changes with the app's own commands.
    Its replies and each command it runs show here; the changes also go in the command log and the
    undo history, so they undo like anything done by hand.
*/
class ChatPanel  : public juce::Component
{
public:
    ChatPanel (Session&, CommandDispatcher&);
    ~ChatPanel() override;

    /** Called to open Settings at the AI page. */
    std::function<void()> onOpenSettings;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Puts the cursor in the message box. */
    void focusInput();

    /** Shows the provider and model in use, e.g. after Settings change them. */
    void updateProvider();

private:
    class Worker;

    void send();
    void stop();
    void newChat();
    void finished (const juce::String& error);
    void add (const juce::String& text, juce::Colour, bool bold = false);
    void setBusy (bool, const juce::String& status = {});
    juce::String getSource() const;

    Session& session;
    CommandDispatcher& dispatcher;
    AiChat chat;
    std::unique_ptr<Worker> worker;
    std::shared_ptr<AiChat::Cancellation> cancellation = std::make_shared<AiChat::Cancellation>();
    bool busy = false, afterTool = false;   // afterTool: the last line was a command it ran

    juce::Label title { {}, "AI chat" }, providerLabel, status;
    juce::TextButton newChatButton { "New chat" }, settingsButton { "Settings" }, sendButton { "Send" };
    juce::TextEditor transcript, input;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChatPanel)
};

} // namespace ng
