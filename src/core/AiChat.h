#pragma once

#include "CommandDispatcher.h"

namespace ng
{

/**
    A conversation with an AI model in which it can use every command the app has (plus batch,
    undo and redo) on the open project: the chat panel's engine. The person picks the provider:
    Claude through the Anthropic API, or any service with an OpenAI-style chat completions API
    (OpenAI, Google Gemini, OpenRouter, or a local model in Ollama or LM Studio).

    runTurn() sends what the person typed and keeps going while the model asks for tools, so it
    blocks; call it on a background thread. How requests are sent and how tools are run are
    passed in, so the app can send them over HTTPS and run commands on the message thread, and
    the tests can do both without a network.
*/
class AiChat
{
public:
    /** Which service to talk to, and how. */
    struct Provider
    {
        juce::String name;          // as the Settings window lists it, e.g. "OpenAI"
        juce::String api;           // "anthropic" or "openai" (an OpenAI-style chat completions API)
        juce::String baseUrl;       // up to and including the version, e.g. https://api.openai.com/v1
        juce::String model;
        juce::String apiKey;        // may be empty for a local model
        bool needsKey = true;
    };

    /** The providers on offer, with their addresses and (for Claude) the default model. */
    static juce::Array<Provider> getProviders();
    static Provider findProvider (const juce::String& name);

    explicit AiChat (const CommandRegistry&);
    ~AiChat();

    /** Switches provider or model. Starts a new conversation when the API or model changes, since
        one model can't carry on another's conversation. */
    void setProvider (const Provider&);
    const Provider& getProvider() const             { return provider; }

    /** POSTs a JSON body (or GETs, if the body is void) with these headers; returns the parsed
        response, or sets error. */
    using Transport = std::function<juce::var (const juce::String& url, const juce::String& headers,
                                               const juce::var& body, juce::String& error)>;

    /** Runs one tool; returns its result as text and sets isError when it failed. */
    using ToolRunner = std::function<juce::String (const juce::String& name, const juce::var& input, bool& isError)>;

    struct Event
    {
        enum class Type { text, toolStarted, toolFinished };
        Type type;
        juce::String text;      // the model's words, or the tool's name
        juce::var input;        // the tool's input
        bool failed = false;    // for toolFinished
        juce::String result;    // for toolFinished
    };

    /** Sends userText and runs the tools the model asks for until it's done. Returns "" when it
        finished normally, or what went wrong. A turn that fails before the model answers leaves
        the conversation as it was before it. */
    juce::String runTurn (const juce::String& userText, const Transport&, const ToolRunner&,
                          const std::function<void (const Event&)>& onEvent,
                          const std::function<bool()>& shouldStop = {});

    /** Forgets the conversation (not the project's changes). */
    void clear();
    int getNumMessages() const;

    /** The request runTurn sends next, and where. */
    juce::var buildRequest() const;
    juce::String getEndpoint() const;
    juce::String getHeaders() const;

    /** The models the provider offers, from its models list. */
    static juce::StringArray listModels (const Provider&, const Transport&, juce::String& error);

    /** Lets another thread stop requests that are under way. */
    class Cancellation
    {
    public:
        void cancel();
        void reset()                    { cancelled = false; }
        bool isCancelled() const        { return cancelled; }

    private:
        friend class AiChat;
        juce::CriticalSection lock;
        juce::WebInputStream* active = nullptr;
        std::atomic<bool> cancelled { false };
    };

    /** Sends requests over HTTPS (or HTTP to a local model), retrying a couple of times when the
        service is busy. Cancelling stops a request in progress. */
    static Transport httpTransport (std::shared_ptr<Cancellation> = {});

    /** Runs a tool call on the dispatcher, logged in the command log under source (the provider's
        name). Call it on the message thread. */
    static juce::String runTool (CommandDispatcher&, const juce::String& source, const juce::String& name,
                                 const juce::var& input, bool& isError);

    /** Most tool rounds one message may take before the model is stopped. */
    static constexpr int maxRounds = 40;

    /** The conversation in one API's shape. */
    class Format;

private:
    juce::var mcpTools;
    juce::String system;
    Provider provider;
    std::unique_ptr<Format> format;
};

} // namespace ng
