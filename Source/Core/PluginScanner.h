#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace wt
{
    /** Plug-ins are scanned in a separate process (the same executable started with a special
        command line). If a plug-in crashes or hangs while it is being scanned, only that helper
        process dies: the plug-in is put on the "blacklist" and the scan continues. */
    namespace PluginScanner
    {
        /** Unique ID on the command line of the helper process. */
        constexpr const char* processUID = "wavethingsPluginScanner";

        /** Seconds a single plug-in may take before it is considered hung. */
        constexpr int timeoutSeconds = 60;

        /** Installs the out-of-process scanner on a KnownPluginList. */
        void install (juce::KnownPluginList& list, juce::File executable = {}, int timeoutSecs = timeoutSeconds);

        /** Call at the very start of JUCEApplication::initialise() (or main()).
            Returns an object to keep alive if this process was started as a scanner, otherwise nullptr. */
        std::unique_ptr<juce::ChildProcessWorker> startWorkerIfRequested (const juce::String& commandLine);
    }
}
