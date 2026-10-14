#include "Bridge.h"

#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace ng
{
namespace
{
    // A header number of our own, so a stray connection from another JUCE app is ignored.
    constexpr juce::uint32 magicHeader = 0x4e674457;   // "NgDW"

    int getProcessId()
    {
       #if JUCE_WINDOWS
        return _getpid();
       #else
        return static_cast<int> (getpid());
       #endif
    }

    juce::String makeToken()
    {
        juce::Random random;
        random.setSeedRandomly();
        juce::String token;

        for (int i = 0; i < 4; ++i)
            token << juce::String::toHexString (random.nextInt64()).paddedLeft ('0', 16);

        return token;
    }

    juce::String toJson (const CommandResult& result)
    {
        return juce::JSON::toString (result.toVar(), true);
    }

    juce::var failure (const juce::String& message)
    {
        return CommandResult::failure (message).toVar();
    }

    juce::String fromBlock (const juce::MemoryBlock& block)
    {
        return juce::String::fromUTF8 (static_cast<const char*> (block.getData()), static_cast<int> (block.getSize()));
    }

    juce::MemoryBlock toBlock (const juce::String& text)
    {
        return { text.toRawUTF8(), text.getNumBytesAsUTF8() };
    }
}

//==============================================================================
juce::File bridge::getConnectionFile()
{
    auto overridden = juce::SystemStats::getEnvironmentVariable ("NG_DAW_BRIDGE_FILE", {});

    if (overridden.isNotEmpty())
        return juce::File::getCurrentWorkingDirectory().getChildFile (overridden);

    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    folder = folder.getChildFile ("Application Support");   // JUCE gives ~/Library on macOS
   #endif

    return folder.getChildFile ("Ng DAW").getChildFile ("bridge.json");
}

std::optional<bridge::ConnectionInfo> bridge::readConnectionFile (const juce::File& f)
{
    if (! f.existsAsFile())
        return std::nullopt;

    auto json = juce::JSON::parse (f.loadFileAsString());
    ConnectionInfo info { static_cast<int> (json["port"]), json["token"].toString(), static_cast<int> (json["pid"]) };

    if (info.port <= 0 || info.token.isEmpty())
        return std::nullopt;

    return info;
}

//==============================================================================
class BridgeServer::Connection  : public juce::InterprocessConnection
{
public:
    explicit Connection (BridgeServer& s) : InterprocessConnection (true, magicHeader), server (&s) {}
    ~Connection() override      { disconnect(); }

    void connectionMade() override {}

    void connectionLost() override
    {
        // Called on the message thread; the connection can't delete itself from inside it.
        juce::MessageManager::callAsync ([server = server, self = this]
        {
            if (server != nullptr)
                server->removeConnection (self);
        });
    }

    void messageReceived (const juce::MemoryBlock& message) override
    {
        if (server != nullptr)
            sendMessage (toBlock (server->handleRequest (fromBlock (message))));
    }

private:
    juce::WeakReference<BridgeServer> server;
};

BridgeServer::BridgeServer (CommandDispatcher& d) : dispatcher (d)
{
    // Connections are made on the listening thread and hold a weak reference to this; making
    // the first one here means they never race to set it up.
    [[maybe_unused]] juce::WeakReference<BridgeServer> first (this);
}

BridgeServer::~BridgeServer()
{
    stop();
}

juce::String BridgeServer::start (const juce::File& connectionFile)
{
    stop();

    if (! beginWaitingForSocket (0, "127.0.0.1"))
        return "Couldn't listen for AI clients on this computer";

    port = getBoundPort();
    token = makeToken();
    file = connectionFile;

    auto info = new juce::DynamicObject();
    info->setProperty ("port", port);
    info->setProperty ("token", token);
    info->setProperty ("pid", getProcessId());

    // Only this user may read the token: create the file empty, restrict it, then write.
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    if (! file.create())
    {
        stop();
        return "Couldn't write " + connectionFile.getFullPathName();
    }

   #if ! JUCE_WINDOWS
    ::chmod (file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
   #endif

    // Written in place (replaceWithText would swap in a new file without the restriction).
    auto written = false;
    {
        juce::FileOutputStream out (file);
        written = out.openedOk() && out.writeText (juce::JSON::toString (juce::var (info)), false, false, nullptr);
        out.flush();
        written = written && out.getStatus().wasOk();
    }

    if (! written)
    {
        stop();
        return "Couldn't write " + connectionFile.getFullPathName();
    }

    return {};
}

void BridgeServer::stop()
{
    InterprocessConnectionServer::stop();

    {
        const juce::ScopedLock sl (lock);
        connections.clear();
    }

    // Another copy of the app may have started since and written its own file.
    if (port > 0)
        if (auto info = bridge::readConnectionFile (file); info && info->token == token)
            file.deleteFile();

    port = 0;
}

juce::InterprocessConnection* BridgeServer::createConnectionObject()
{
    const juce::ScopedLock sl (lock);
    return connections.add (new Connection (*this));
}

void BridgeServer::removeConnection (Connection* c)
{
    const juce::ScopedLock sl (lock);

    if (connections.contains (c) && ! c->isConnected())
        connections.removeObject (c);
}

juce::String BridgeServer::handleRequest (const juce::String& json)
{
    juce::var request;

    if (juce::JSON::parse (json, request).failed() || ! request.isObject())
        return juce::JSON::toString (failure ("The request isn't a JSON object"), true);

    if (request["token"].toString() != token || token.isEmpty())
        return juce::JSON::toString (failure ("Wrong token: read it again from the connection file"), true);

    if (request["describe"])
        return toJson (CommandResult::success (dispatcher.getRegistry().describe()));

    auto source = request["source"].toString().trim();
    CommandDispatcher::ScopedSource scopedSource (dispatcher, source.isNotEmpty() ? source : "AI");

    if (request["undo"])
    {
        auto done = dispatcher.undo();
        return toJson (done ? CommandResult::success() : CommandResult::failure ("There's nothing to undo"));
    }

    if (request["redo"])
    {
        auto done = dispatcher.redo();
        return toJson (done ? CommandResult::success() : CommandResult::failure ("There's nothing to redo"));
    }

    auto body = request["request"];

    if (! body.isObject())
        return juce::JSON::toString (failure ("Give a request: {\"command\": ..., \"params\": ...} or {\"batch\": ..., \"commands\": [...]}"), true);

    if (body.hasProperty ("batch"))
        return toJson (dispatcher.executeBatch (body["batch"].toString(), body["commands"]));

    return toJson (dispatcher.execute (body["command"].toString(), body["params"]));
}

//==============================================================================
BridgeClient::BridgeClient() : InterprocessConnection (false, magicHeader) {}

BridgeClient::~BridgeClient()
{
    disconnect();
}

bool BridgeClient::ensureConnected (juce::String& error)
{
    if (isConnected() && ! lost)
        return true;

    disconnect();
    lost = false;

    auto info = bridge::readConnectionFile (file);

    if (! info)
    {
        error = "Ng DAW isn't running. Open it, then try again.";
        return false;
    }

    if (! connectToSocket ("127.0.0.1", info->port, 3000))
    {
        error = "Ng DAW isn't running, or isn't answering. Open it, then try again.";
        return false;
    }

    token = info->token;
    return true;
}

juce::var BridgeClient::send (const juce::var& request, int timeoutMs)
{
    // One request at a time, each waiting for its reply.
    static juce::CriticalSection sendLock;
    const juce::ScopedLock sl (sendLock);

    // A connection to an app that has since closed or restarted fails on the first try.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        juce::String error;

        if (! ensureConnected (error))
            return failure (error);

        auto copy = request.clone();

        if (auto obj = copy.getDynamicObject())
            obj->setProperty ("token", token);

        replied.reset();
        {
            const juce::ScopedLock rl (replyLock);
            reply = {};
        }

        if (! sendMessage (toBlock (juce::JSON::toString (copy, true))))
        {
            lost = true;
            continue;
        }

        if (! replied.wait (timeoutMs))
            return failure ("Ng DAW didn't answer in time");

        const juce::ScopedLock rl (replyLock);

        // Not sent again: the app may have run it before the connection dropped.
        if (reply.isEmpty())
            return failure ("Ng DAW closed the connection before answering");

        juce::var result;

        if (juce::JSON::parse (reply, result).failed())
            return failure ("Ng DAW sent a reply that isn't JSON");

        return result;
    }

    return failure ("Ng DAW closed the connection");
}

void BridgeClient::connectionLost()
{
    lost = true;
    replied.signal();
}

void BridgeClient::messageReceived (const juce::MemoryBlock& message)
{
    {
        const juce::ScopedLock rl (replyLock);
        reply = fromBlock (message);
    }

    replied.signal();
}

} // namespace ng
