#include "ChatPanel.h"

namespace ng
{
namespace
{
    const juce::Colour youColour (0xff7fb2ff), aiColour (0xffe8e8e8), toolColour (0xff8a8f98),
                       failedColour (0xffe0735f), noteColour (0xffd8b25a);

    /** "create_track" reads as "create track". */
    juce::String describeTool (const juce::String& name, const juce::var& input)
    {
        auto text = name.replaceCharacter ('_', ' ');

        if (name == "batch" && input["label"].toString().isNotEmpty())
            text << ": " << input["label"].toString();
        else if (input["name"].isString())
            text << " \"" << input["name"].toString() << "\"";

        return text;
    }
}

//==============================================================================
/** Runs one message's turn off the message thread, so the window stays responsive. */
class ChatPanel::Worker  : public juce::Thread
{
public:
    Worker (ChatPanel& p, const juce::String& message)
        : Thread ("AI chat"), panel (p), safePanel (&p), text (message), source (p.getSource()), cancellation (p.cancellation) {}

    void run() override
    {
        auto shouldStop = [this] { return threadShouldExit() || cancellation->isCancelled(); };
        auto& dispatcher = panel.dispatcher;

        // Each tool runs on the message thread, like a click would; this thread waits for it.
        auto runTool = [this, &dispatcher, shouldStop] (const juce::String& name, const juce::var& input, bool& isError)
        {
            struct Call
            {
                juce::String result;
                bool isError = true;
                juce::WaitableEvent done;
            };

            auto call = std::make_shared<Call>();
            auto safe = safePanel;
            auto from = source;

            juce::MessageManager::callAsync ([safe, call, name, input, from, &dispatcher]
            {
                if (safe != nullptr)
                    call->result = AiChat::runTool (dispatcher, from, name, input, call->isError);
                else
                    call->result = "The chat was closed.";

                call->done.signal();
            });

            while (! call->done.wait (100))
            {
                if (shouldStop())
                {
                    isError = true;
                    return juce::String ("Stopped.");
                }
            }

            isError = call->isError;
            return call->result;
        };

        auto onEvent = [this] (const AiChat::Event& e)
        {
            juce::MessageManager::callAsync ([safe = safePanel, e]
            {
                if (safe == nullptr)
                    return;

                using T = AiChat::Event::Type;

                if (e.type == T::text)
                    safe->add ((safe->afterTool ? "\n" : "") + e.text + "\n\n", aiColour);
                else if (e.type == T::toolStarted)
                    safe->setBusy (true, "Running " + e.text.replaceCharacter ('_', ' ') + "...");
                else if (e.failed)
                    safe->add ("  x " + describeTool (e.text, e.input) + ": " + e.result.upToFirstOccurrenceOf ("\n", false, false) + "\n", failedColour);
                else
                    safe->add ("  + " + describeTool (e.text, e.input) + "\n", toolColour);

                if (e.type != T::toolStarted)
                    safe->setBusy (true, "Thinking...");
            });
        };

        auto error = panel.chat.runTurn (text, AiChat::httpTransport (cancellation), runTool, onEvent, shouldStop);

        juce::MessageManager::callAsync ([safe = safePanel, error]
        {
            if (safe != nullptr)
                safe->finished (error);
        });
    }

private:
    ChatPanel& panel;
    juce::Component::SafePointer<ChatPanel> safePanel;
    juce::String text, source;
    std::shared_ptr<AiChat::Cancellation> cancellation;
};

//==============================================================================
ChatPanel::ChatPanel (Session& s, CommandDispatcher& d)
    : session (s), dispatcher (d), chat (d.getRegistry())
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    providerLabel.setFont (juce::FontOptions (12.0f));
    providerLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    providerLabel.setMinimumHorizontalScale (0.7f);
    status.setFont (juce::FontOptions (12.0f));
    status.setColour (juce::Label::textColourId, noteColour);

    transcript.setMultiLine (true);
    transcript.setReadOnly (true);
    transcript.setCaretVisible (false);
    transcript.setScrollbarsShown (true);
    transcript.setFont (juce::FontOptions (14.0f));

    input.setMultiLine (true);
    input.setReturnKeyStartsNewLine (false);
    input.setFont (juce::FontOptions (14.0f));
    input.setTextToShowWhenEmpty ("Ask for a change, e.g. \"add a bass line that follows the chords\"", juce::Colours::grey);
    input.onReturnKey = [this] { send(); };

    sendButton.onClick = [this] { busy ? stop() : send(); };
    sendButton.setTooltip ("Send (Enter; Shift+Enter for a new line)");
    newChatButton.onClick = [this] { newChat(); };
    newChatButton.setTooltip ("Start a new conversation (the project keeps its changes)");
    settingsButton.onClick = [this] { if (onOpenSettings != nullptr) onOpenSettings(); };
    settingsButton.setTooltip ("Choose the AI provider and model, and enter its API key");

    for (auto* c : std::initializer_list<juce::Component*> { &title, &providerLabel, &newChatButton, &settingsButton,
                                                             &transcript, &status, &input, &sendButton })
        addAndMakeVisible (c);

    add ("Ask the AI to change the project: add parts, write notes, set up the mix. What it does goes in the "
         "undo history and the command log.\n\n", toolColour);
    updateProvider();
}

ChatPanel::~ChatPanel()
{
    cancellation->cancel();

    if (worker != nullptr)
        worker->stopThread (10000);
}

void ChatPanel::paint (juce::Graphics& g)
{
    g.setColour (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter (0.05f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
}

void ChatPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (24);
    settingsButton.setBounds (top.removeFromRight (64).reduced (0, 1));
    top.removeFromRight (4);
    newChatButton.setBounds (top.removeFromRight (70).reduced (0, 1));
    title.setBounds (top);
    providerLabel.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    auto bottom = r.removeFromBottom (64);
    sendButton.setBounds (bottom.removeFromRight (56).withSizeKeepingCentre (56, 28));
    bottom.removeFromRight (6);
    input.setBounds (bottom);
    r.removeFromBottom (2);
    status.setBounds (r.removeFromBottom (18));
    r.removeFromBottom (2);
    transcript.setBounds (r);
}

void ChatPanel::focusInput()
{
    input.grabKeyboardFocus();
}

void ChatPanel::updateProvider()
{
    auto p = settings::getAiProvider (session.getEngine());

    if (! busy)
        chat.setProvider (p);

    providerLabel.setText (p.model.isNotEmpty() ? p.name + ": " + p.model : p.name + ": no model chosen", juce::dontSendNotification);
}

juce::String ChatPanel::getSource() const
{
    return chat.getProvider().name.upToFirstOccurrenceOf (" (", false, false) + " (chat)";
}

void ChatPanel::add (const juce::String& text, juce::Colour colour, bool bold)
{
    afterTool = colour == toolColour || colour == failedColour;
    transcript.moveCaretToEnd();
    transcript.setColour (juce::TextEditor::textColourId, colour);
    transcript.setFont (juce::FontOptions (14.0f, bold ? juce::Font::bold : juce::Font::plain));
    transcript.insertTextAtCaret (text);
    transcript.moveCaretToEnd();
}

void ChatPanel::setBusy (bool isBusy, const juce::String& text)
{
    busy = isBusy;
    status.setText (text, juce::dontSendNotification);
    sendButton.setButtonText (busy ? "Stop" : "Send");
    newChatButton.setEnabled (! busy);
}

void ChatPanel::send()
{
    auto text = input.getText().trim();

    if (busy || text.isEmpty())
        return;

    updateProvider();
    auto p = chat.getProvider();

    if (p.needsKey && p.apiKey.isEmpty())
    {
        add ("Add your " + p.name + " API key in Settings > AI first (or pick a local model there).\n\n", noteColour);

        if (onOpenSettings != nullptr)
            onOpenSettings();

        return;
    }

    if (p.model.isEmpty())
    {
        add ("Pick a model for " + p.name + " in Settings > AI first.\n\n", noteColour);

        if (onOpenSettings != nullptr)
            onOpenSettings();

        return;
    }

    input.clear();
    add ("You\n", youColour, true);
    add (text + "\n\n", youColour);
    setBusy (true, "Thinking...");

    if (worker != nullptr)
        worker->stopThread (1000);

    cancellation->reset();
    worker = std::make_unique<Worker> (*this, text);
    worker->startThread();
}

void ChatPanel::stop()
{
    status.setText ("Stopping...", juce::dontSendNotification);
    cancellation->cancel();

    if (worker != nullptr)
        worker->signalThreadShouldExit();
}

void ChatPanel::finished (const juce::String& error)
{
    if (worker != nullptr)
        worker->stopThread (1000);

    if (error.isNotEmpty())
        add (error + "\n\n", error == "Stopped." ? toolColour : failedColour);

    setBusy (false);
    updateProvider();
}

void ChatPanel::newChat()
{
    if (busy)
        return;

    chat.clear();
    transcript.clear();
    add ("New conversation. The project keeps the changes already made.\n\n", toolColour);
}

} // namespace ng
