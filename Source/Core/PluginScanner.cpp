#include "PluginScanner.h"
#include <condition_variable>
#include <mutex>
#include <queue>

// Based on the out-of-process scanner of JUCE's AudioPluginHost, plus a time-out for plug-ins
// that never return (the original waits forever).

namespace wt::PluginScanner
{
    //==============================================================================
    /** Lives in the main application: starts the helper and talks to it. */
    class Coordinator : private juce::ChildProcessCoordinator
    {
    public:
        explicit Coordinator (const juce::File& executable)
        {
            launched = launchWorkerProcess (executable, processUID, 0, 0);
        }

        ~Coordinator() override
        {
            killWorkerProcess();
        }

        bool isRunning() const { return launched; }

        enum class State { timeout, gotResult, connectionLost };

        struct Response
        {
            State state;
            std::unique_ptr<juce::XmlElement> xml;
        };

        Response getResponse (int waitMs)
        {
            std::unique_lock<std::mutex> lock { mutex };

            if (! condition.wait_for (lock, std::chrono::milliseconds { waitMs }, [&] { return gotResult || connectionLost; }))
                return { State::timeout, nullptr };

            const auto state = connectionLost ? State::connectionLost : State::gotResult;
            connectionLost = false;
            gotResult = false;
            return { state, std::move (result) };
        }

        using juce::ChildProcessCoordinator::sendMessageToWorker;

    private:
        void handleMessageFromWorker (const juce::MemoryBlock& mb) override
        {
            const std::lock_guard<std::mutex> lock { mutex };
            result = juce::parseXML (mb.toString());
            gotResult = true;
            condition.notify_one();
        }

        void handleConnectionLost() override
        {
            const std::lock_guard<std::mutex> lock { mutex };
            connectionLost = true;
            condition.notify_one();
        }

        bool launched = false;
        std::mutex mutex;
        std::condition_variable condition;
        std::unique_ptr<juce::XmlElement> result;
        bool connectionLost = false, gotResult = false;
    };

    //==============================================================================
    class OutOfProcessScanner : public juce::KnownPluginList::CustomScanner
    {
    public:
        OutOfProcessScanner (juce::File exe, int timeout) : executable (std::move (exe)), timeoutSecs (timeout) {}

        bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            const std::lock_guard<std::mutex> lock (scanMutex);

            if (coordinator == nullptr)
                coordinator = std::make_unique<Coordinator> (executable);

            if (! coordinator->isRunning())
            {
                // the helper could not be started: fall back to scanning here
                coordinator.reset();
                format.findAllTypesForFile (result, fileOrIdentifier);
                return true;
            }

            juce::MemoryBlock block;
            {
                juce::MemoryOutputStream stream { block, true };
                stream.writeString (format.getName());
                stream.writeString (fileOrIdentifier);
            }

            if (! coordinator->sendMessageToWorker (block))
            {
                coordinator.reset();
                return false;
            }

            const auto start = juce::Time::getMillisecondCounter();

            for (;;)
            {
                if (shouldExit())
                {
                    coordinator.reset();
                    return true;
                }

                auto response = coordinator->getResponse (50);

                if (response.state == Coordinator::State::timeout)
                {
                    if (juce::Time::getMillisecondCounter() - start > (juce::uint32) timeoutSecs * 1000u)
                    {
                        coordinator.reset();   // kills the hung helper
                        return false;          // -> blacklisted
                    }
                    continue;
                }

                if (response.xml != nullptr)
                {
                    for (auto* item : response.xml->getChildIterator())
                    {
                        auto description = std::make_unique<juce::PluginDescription>();
                        if (description->loadFromXml (*item))
                            result.add (std::move (description));
                    }
                }

                if (response.state == Coordinator::State::connectionLost)
                {
                    coordinator.reset();       // the plug-in crashed the helper
                    return false;
                }

                return true;
            }
        }

        void scanFinished() override
        {
            const std::lock_guard<std::mutex> lock (scanMutex);
            coordinator.reset();
        }

    private:
        juce::File executable;
        int timeoutSecs;
        std::mutex scanMutex;
        std::unique_ptr<Coordinator> coordinator;
    };

    void install (juce::KnownPluginList& list, juce::File executable, int timeoutSecs)
    {
        if (executable == juce::File())
            executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

        list.setCustomScanner (std::make_unique<OutOfProcessScanner> (executable, timeoutSecs));
    }

    //==============================================================================
    /** Lives in the helper process: scans what it is asked for and sends the result back. */
    class Worker : public juce::ChildProcessWorker, private juce::AsyncUpdater
    {
    public:
        Worker()
        {
            juce::addDefaultFormatsToManager (formatManager);
        }

    private:
        void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
        {
            if (mb.isEmpty())
                return;

            const std::lock_guard<std::mutex> lock (mutex);

            // Formats that need a free message thread (AUv3) are scanned right here on the IPC thread,
            // everything else is scanned on the message thread (like JUCE's AudioPluginHost does)
            if (auto results = scan (mb, false); results.has_value())
            {
                sendResults (*results);
            }
            else
            {
                pending.emplace (mb);
                triggerAsyncUpdate();
            }
        }

        void handleConnectionLost() override
        {
            juce::JUCEApplicationBase::quit();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }

        void handleAsyncUpdate() override
        {
            for (;;)
            {
                const std::lock_guard<std::mutex> lock (mutex);
                if (pending.empty())
                    return;

                if (auto results = scan (pending.front(), true))
                    sendResults (*results);
                else
                    sendResults ({});
                pending.pop();
            }
        }

        /** Returns nothing if the scan must be repeated on the message thread. */
        std::optional<juce::OwnedArray<juce::PluginDescription>> scan (const juce::MemoryBlock& block, bool onMessageThread)
        {
            juce::MemoryInputStream stream { block, false };
            const auto formatName = stream.readString();
            const auto identifier = stream.readString();

            juce::PluginDescription pd;
            pd.fileOrIdentifier = identifier;
            pd.uniqueId = pd.deprecatedUid = 0;

            juce::AudioPluginFormat* format = nullptr;
            for (auto* f : formatManager.getFormats())
                if (f->getName() == formatName)
                    format = f;

            juce::OwnedArray<juce::PluginDescription> results;
            if (format == nullptr)
                return results;

            const bool canRunHere = onMessageThread
                                    || juce::MessageManager::getInstance()->isThisTheMessageThread()
                                    || format->requiresUnblockedMessageThreadDuringCreation (pd);
            if (! canRunHere)
                return std::nullopt;

            format->findAllTypesForFile (results, identifier);
            return results;
        }

        void sendResults (const juce::OwnedArray<juce::PluginDescription>& results)
        {
            juce::XmlElement xml ("LIST");
            for (auto* d : results)
                xml.addChildElement (d->createXml().release());

            const auto text = xml.toString();
            sendMessageToCoordinator ({ text.toRawUTF8(), text.getNumBytesAsUTF8() });
        }

        std::mutex mutex;
        std::queue<juce::MemoryBlock> pending;
        juce::AudioPluginFormatManager formatManager;
    };

    std::unique_ptr<juce::ChildProcessWorker> startWorkerIfRequested (const juce::String& commandLine)
    {
        auto worker = std::make_unique<Worker>();
        if (worker->initialiseFromCommandLine (commandLine, processUID, 0))
            return worker;
        return nullptr;
    }
}
