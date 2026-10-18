#include "AiChat.h"
#include "McpServer.h"

namespace ng
{
namespace
{
    using params::object;
    const char* const chatInstructions =
        "\n\nYou are talking to the person in Ng DAW's chat panel, beside the project they are working on. They see "
        "your replies there, and each change you make also appears in the app's command log and undo history. Keep "
        "replies short and plain: say what you changed, or ask when a request is unclear. Look before you change "
        "things, and prefer one batch for a change made of several commands so it undoes as one step.";

    juce::String withoutTrailingSlash (const juce::String& url)
    {
        return url.trim().trimCharactersAtEnd ("/");
    }
}

//==============================================================================
class AiChat::Format
{
public:
    virtual ~Format() = default;

    struct ToolCall
    {
        juce::String id, name;
        juce::var input;
        juce::String inputError;    // when the arguments weren't valid JSON
    };

    struct Reply
    {
        juce::StringArray texts;
        std::vector<ToolCall> calls;
        bool cutOff = false;
        juce::String error;         // nothing was added to the conversation
    };

    struct ToolResult
    {
        juce::String id, text;
        bool isError = false;
    };

    virtual juce::String endpoint (const Provider&) const = 0;
    virtual juce::String headers (const Provider&) const = 0;
    virtual juce::var request (const Provider&, const juce::var& mcpTools, const juce::String& system) const = 0;
    virtual void addUser (const juce::String&) = 0;
    virtual Reply addReply (const juce::var& response) = 0;
    virtual void addToolResults (const std::vector<ToolResult>&) = 0;

    juce::Array<juce::var> messages;
};

namespace
{
    /** Claude's Messages API. */
    class AnthropicFormat  : public AiChat::Format
    {
    public:
        juce::String endpoint (const AiChat::Provider& p) const override    { return withoutTrailingSlash (p.baseUrl) + "/messages"; }

        juce::String headers (const AiChat::Provider& p) const override
        {
            return "Content-Type: application/json\r\n"
                   "x-api-key: " + p.apiKey.trim() + "\r\n"
                   "anthropic-version: 2023-06-01\r\n"
                   "anthropic-beta: server-side-fallback-2026-07-01";
        }

        juce::var request (const AiChat::Provider& p, const juce::var& mcpTools, const juce::String& system) const override
        {
            juce::Array<juce::var> tools;

            for (auto& t : *mcpTools.getArray())
                tools.add (object ({ { "name", t["name"] }, { "description", t["description"] }, { "input_schema", t["inputSchema"] } }));

            return object ({ { "model", p.model },
                             { "max_tokens", 16000 },
                             { "system", system },
                             { "tools", tools },
                             { "messages", messages },
                             { "output_config", object ({ { "effort", "medium" } }) },
                             { "cache_control", object ({ { "type", "ephemeral" } }) },
                             // If a safety classifier declines, the API retries on the model it recommends.
                             { "fallbacks", "default" } });
        }

        void addUser (const juce::String& text) override
        {
            messages.add (object ({ { "role", "user" }, { "content", text } }));
        }

        Reply addReply (const juce::var& response) override
        {
            Reply reply;
            auto content = response["content"];
            auto stopReason = response["stop_reason"].toString();

            if (! content.isArray())
                reply.error = "The Claude API sent back something unexpected.";
            else if (stopReason == "refusal")
                reply.error = "Claude declined this request.";

            if (reply.error.isNotEmpty())
                return reply;

            // The turn goes back exactly as it came, thinking blocks and all.
            messages.add (object ({ { "role", "assistant" }, { "content", content } }));
            reply.cutOff = stopReason == "max_tokens";

            for (auto& block : *content.getArray())
            {
                if (block["type"] == "text")
                    reply.texts.add (block["text"].toString());
                else if (block["type"] == "tool_use")
                    reply.calls.push_back ({ block["id"].toString(), block["name"].toString(), block["input"], {} });
            }

            return reply;
        }

        void addToolResults (const std::vector<ToolResult>& results) override
        {
            // Every result goes back in one message, so Claude keeps making parallel calls.
            juce::Array<juce::var> content;

            for (auto& r : results)
                content.add (object ({ { "type", "tool_result" }, { "tool_use_id", r.id }, { "content", r.text }, { "is_error", r.isError } }));

            messages.add (object ({ { "role", "user" }, { "content", content } }));
        }
    };

    /** The OpenAI-style chat completions API that OpenAI, Gemini, OpenRouter, Ollama and LM Studio offer. */
    class OpenAiFormat  : public AiChat::Format
    {
    public:
        juce::String endpoint (const AiChat::Provider& p) const override    { return withoutTrailingSlash (p.baseUrl) + "/chat/completions"; }

        juce::String headers (const AiChat::Provider& p) const override
        {
            juce::String h ("Content-Type: application/json");

            if (p.apiKey.trim().isNotEmpty())
                h << "\r\nAuthorization: Bearer " << p.apiKey.trim();

            return h;
        }

        juce::var request (const AiChat::Provider& p, const juce::var& mcpTools, const juce::String& system) const override
        {
            juce::Array<juce::var> tools, all;

            for (auto& t : *mcpTools.getArray())
                tools.add (object ({ { "type", "function" },
                                     { "function", object ({ { "name", t["name"] }, { "description", t["description"] },
                                                             { "parameters", t["inputSchema"] } }) } }));

            all.add (object ({ { "role", "system" }, { "content", system } }));
            all.addArray (messages);

            return object ({ { "model", p.model }, { "messages", all }, { "tools", tools } });
        }

        void addUser (const juce::String& text) override
        {
            messages.add (object ({ { "role", "user" }, { "content", text } }));
        }

        Reply addReply (const juce::var& response) override
        {
            Reply reply;
            auto choice = response["choices"][0];
            auto message = choice["message"];

            if (! message.isObject())
            {
                reply.error = "The AI service sent back something unexpected.";
                return reply;
            }

            auto finish = choice["finish_reason"].toString();

            if (finish == "content_filter")
            {
                reply.error = "The AI service declined this request.";
                return reply;
            }

            messages.add (message);
            reply.cutOff = finish == "length";

            if (message["content"].isString() && message["content"].toString().trim().isNotEmpty())
                reply.texts.add (message["content"].toString());

            if (auto* calls = message["tool_calls"].getArray())
            {
                for (auto& call : *calls)
                {
                    ToolCall c { call["id"].toString(), call["function"]["name"].toString(), {}, {} };
                    auto arguments = call["function"]["arguments"];

                    if (arguments.isObject())
                        c.input = arguments;
                    else if (arguments.toString().trim().isEmpty())
                        c.input = object ({});
                    else if (juce::JSON::parse (arguments.toString(), c.input).failed() || ! c.input.isObject())
                        c.inputError = "The arguments weren't valid JSON; send them again as a JSON object.";

                    reply.calls.push_back (c);
                }
            }

            return reply;
        }

        void addToolResults (const std::vector<ToolResult>& results) override
        {
            for (auto& r : results)
                messages.add (object ({ { "role", "tool" }, { "tool_call_id", r.id },
                                        { "content", r.isError ? "Error: " + r.text : r.text } }));
        }
    };
}

//==============================================================================
juce::Array<AiChat::Provider> AiChat::getProviders()
{
    auto provider = [] (const char* name, const char* api, const char* url, const char* model, bool needsKey)
    {
        return Provider { name, api, url, model, {}, needsKey };
    };

    return { provider ("Claude (Anthropic)",           "anthropic", "https://api.anthropic.com/v1",                             "claude-opus-5-5", true),
             provider ("OpenAI",                       "openai",    "https://api.openai.com/v1",                                "",                true),
             provider ("Google Gemini",                "openai",    "https://generativelanguage.googleapis.com/v1beta/openai", "",                true),
             provider ("OpenRouter",                   "openai",    "https://openrouter.ai/api/v1",                             "",                true),
             provider ("Ollama (on this computer)",    "openai",    "http://localhost:11434/v1",                                "",                false),
             provider ("LM Studio (on this computer)", "openai",    "http://localhost:1234/v1",                                 "",                false),
             provider ("Other (OpenAI-compatible)",    "openai",    "",                                                         "",                false) };
}

AiChat::Provider AiChat::findProvider (const juce::String& name)
{
    for (auto& p : getProviders())
        if (p.name == name)
            return p;

    return getProviders().getFirst();
}

AiChat::AiChat (const CommandRegistry& registry)
    : mcpTools (McpServer::listTools (registry)),
      system (McpServer::getInstructions() + chatInstructions)
{
    setProvider (getProviders().getFirst());
}

AiChat::~AiChat() = default;

void AiChat::setProvider (const Provider& p)
{
    auto sameConversation = format != nullptr && p.api == provider.api && p.model == provider.model
                              && withoutTrailingSlash (p.baseUrl) == withoutTrailingSlash (provider.baseUrl);
    provider = p;

    if (! sameConversation)
    {
        if (p.api == "anthropic")
            format = std::make_unique<AnthropicFormat>();
        else
            format = std::make_unique<OpenAiFormat>();
    }
}

void AiChat::clear()                         { format->messages.clearQuick(); }
int AiChat::getNumMessages() const           { return format->messages.size(); }
juce::var AiChat::buildRequest() const       { return format->request (provider, mcpTools, system); }
juce::String AiChat::getEndpoint() const     { return format->endpoint (provider); }
juce::String AiChat::getHeaders() const      { return format->headers (provider); }

juce::String AiChat::runTurn (const juce::String& userText, const Transport& transport, const ToolRunner& runToolCall,
                              const std::function<void (const Event&)>& onEvent, const std::function<bool()>& shouldStop)
{
    auto stopping = [&shouldStop] { return shouldStop != nullptr && shouldStop(); };

    if (provider.model.trim().isEmpty())
        return "Pick a model for " + provider.name + " in Settings > AI first.";

    if (provider.baseUrl.trim().isEmpty())
        return "Give the address of the AI service in Settings > AI first.";

    auto& messages = format->messages;
    auto turnStart = messages.size();
    auto answered = false;

    // Forgets a turn the model never answered, so the next message starts from a valid conversation.
    auto failBeforeAnswer = [&] (const juce::String& error)
    {
        if (! answered)
            messages.removeRange (turnStart, messages.size() - turnStart);

        return error;
    };

    format->addUser (userText);

    for (int round = 0; round < maxRounds; ++round)
    {
        if (stopping())
            return failBeforeAnswer ("Stopped.");

        juce::String error;
        auto response = transport (getEndpoint(), getHeaders(), buildRequest(), error);

        if (error.isNotEmpty())
            return failBeforeAnswer (error);

        auto reply = format->addReply (response);

        if (reply.error.isNotEmpty())
            return failBeforeAnswer (reply.error);

        answered = true;

        for (auto& text : reply.texts)
            if (text.trim().isNotEmpty())
                onEvent ({ Event::Type::text, text.trim() });

        if (reply.calls.empty())
            return reply.cutOff ? juce::String ("The reply was cut off (too long).") : juce::String();

        std::vector<Format::ToolResult> results;

        for (auto& call : reply.calls)
        {
            onEvent ({ Event::Type::toolStarted, call.name, call.input });

            auto isError = true;
            juce::String result;

            if (call.inputError.isNotEmpty())
                result = call.inputError;
            else if (stopping())
                result = "Stopped by the person before it ran.";
            else
                result = runToolCall (call.name, call.input, isError);

            onEvent ({ Event::Type::toolFinished, call.name, call.input, isError, result });
            results.push_back ({ call.id, result, isError });
        }

        format->addToolResults (results);
    }

    return "Stopped after " + juce::String (maxRounds) + " rounds of changes; ask it to carry on if it wasn't done.";
}

//==============================================================================
juce::StringArray AiChat::listModels (const Provider& p, const Transport& transport, juce::String& error)
{
    AiChat chat (CommandRegistry {});
    chat.setProvider (p);

    // Only the auth headers: a GET has no body.
    auto headers = chat.getHeaders().replace ("Content-Type: application/json\r\n", "").replace ("Content-Type: application/json", "");
    auto response = transport (withoutTrailingSlash (p.baseUrl) + "/models", headers, {}, error);
    juce::StringArray models;

    if (auto* list = response["data"].getArray())
        for (auto& m : *list)
        {
            auto id = m["id"].toString();
            models.addIfNotAlreadyThere (id.startsWith ("models/") ? id.substring (7) : id);   // Gemini says "models/..."
        }

    if (models.isEmpty() && error.isEmpty())
        error = "The service didn't list any models; type the model's name instead.";

    models.removeEmptyStrings();
    return models;
}

void AiChat::Cancellation::cancel()
{
    const juce::ScopedLock sl (lock);
    cancelled = true;

    if (active != nullptr)
        active->cancel();
}

AiChat::Transport AiChat::httpTransport (std::shared_ptr<Cancellation> cancellation)
{
    if (cancellation == nullptr)
        cancellation = std::make_shared<Cancellation>();

    return [cancellation] (const juce::String& address, const juce::String& headers, const juce::var& body, juce::String& error) -> juce::var
    {
        auto isPost = ! body.isVoid();
        juce::URL url (address);

        if (isPost)
            url = url.withPOSTData (juce::JSON::toString (body, true));

        for (int attempt = 0;; ++attempt)
        {
            juce::WebInputStream stream (url, isPost);
            stream.withExtraHeaders (headers).withConnectionTimeout (10 * 60 * 1000);

            {
                const juce::ScopedLock sl (cancellation->lock);

                if (cancellation->isCancelled())
                {
                    error = "Stopped.";
                    return {};
                }

                cancellation->active = &stream;
            }

            auto connected = stream.connect (nullptr);
            auto text = connected ? stream.readEntireStreamAsString() : juce::String();
            auto status = stream.getStatusCode();

            {
                const juce::ScopedLock sl (cancellation->lock);
                cancellation->active = nullptr;
            }

            if (cancellation->isCancelled())
            {
                error = "Stopped.";
                return {};
            }

            if (status == 0)
            {
                error = address.contains ("localhost") ? "Couldn't reach " + address + ". Is the local model server running?"
                                                       : "Couldn't reach " + address + ". Check the internet connection.";
                return {};
            }

            juce::var response;
            juce::JSON::parse (text, response);

            if (status >= 200 && status < 300 && response.isObject())
                return response;

            // Busy or overloaded: wait a little and try again, twice.
            if ((status == 429 || status == 529 || status >= 500) && attempt < 2)
            {
                for (int i = 0; i < (attempt + 1) * 20 && ! cancellation->isCancelled(); ++i)
                    juce::Thread::sleep (100);

                continue;
            }

            auto message = response["error"]["message"].toString();

            if (message.isEmpty())
                message = response["error"].isString() ? response["error"].toString() : text.substring (0, 300).trim();

            if (status == 401 || status == 403)
                error = "The service didn't accept the API key (" + message + "). Check it in Settings > AI.";
            else
                error = "The AI service said: " + (message.isNotEmpty() ? message : "error " + juce::String (status));

            return {};
        }
    };
}

juce::String AiChat::runTool (CommandDispatcher& dispatcher, const juce::String& source, const juce::String& name,
                              const juce::var& input, bool& isError)
{
    CommandDispatcher::ScopedSource scopedSource (dispatcher, source);
    auto args = input.isObject() ? input : object ({});
    CommandResult result;

    if (name == "undo" || name == "redo")
    {
        auto done = name == "undo" ? dispatcher.undo() : dispatcher.redo();
        result = done ? CommandResult::success() : CommandResult::failure ("There's nothing to " + name);
    }
    else if (name == "batch")
    {
        auto label = args["label"].toString();
        result = dispatcher.executeBatch (label.isNotEmpty() ? label : juce::String ("AI changes"), args["commands"]);
    }
    else if (dispatcher.getRegistry().find (name) != nullptr)
    {
        result = dispatcher.execute (name, args);
    }
    else
    {
        result = CommandResult::failure ("Unknown tool: " + name);
    }

    isError = ! result.ok;

    if (! result.ok)
        return result.error;

    return result.value.isVoid() ? juce::String ("Done.") : juce::JSON::toString (result.value, true);
}

} // namespace ng
