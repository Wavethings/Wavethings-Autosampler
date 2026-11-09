#pragma once

#include "AudioEngine.h"
#include "Instrument.h"
#include "SampleProcessing.h"
#include "Settings.h"

namespace wt
{
    /** Runs a complete sampling session on a background thread:
        for every root note and velocity layer -> wait for silence -> record -> process -> save,
        then normalise/convert the samples and run all selected exporters.
        All callbacks are delivered on the message thread. */
    class SamplingJob : public juce::Thread
    {
    public:
        struct Progress
        {
            int index = 0;
            int total = 0;
            int note = 60;
            int velocity = 127;
            int layer = 0;
            int programIndex = 1, programTotal = 1;   // batch
            juce::String programName;
            juce::String status;
        };

        std::function<void (const Progress&)> onProgress;
        std::function<void (std::shared_ptr<ProcessedSample>, int note, int velocity)> onSampleCaptured;
        std::function<void (const juce::String& line)> onLog;
        std::function<void (bool success, const Instrument& instrument)> onFinished;

        SamplingJob (AudioEngine& engine, const SamplingSettings& settings);
        ~SamplingJob() override;

        void run() override;

        /** Offline mode for tests: instead of waiting for the device, the job calls this to render audio. */
        std::function<void (int numFrames)> offlineRenderer;

        /** Tests: deliver the callbacks directly on the job thread instead of the message thread. */
        bool synchronousCallbacks = false;

        const Instrument& getInstrument() const { return instrument; }
        /** The instrument folder, or the batch folder with one sub-folder per program. */
        juce::File getOutputFolder() const { return outputFolder; }

    private:
        enum class Result { ok, empty, failed, cancelled };
        Result sampleInstrument (const juce::File& root, const juce::String& name, bool batch);
        bool selectProgram (const BatchItem& item, juce::String& name);
        bool waitForSilence (double maxSeconds);
        void sendHardwareControls();
        bool captureNote (int note, int velocity, CaptureRequest& request);
        bool finaliseSamples (float globalPeak);
        void runExporters();
        void log (const juce::String& line);
        void post (std::function<void()> fn);
        void sleepFor (double ms);

        AudioEngine& engine;
        SamplingSettings settings;
        Instrument instrument;
        juce::File captureFolder, outputFolder;
        int programIndex = 1, programTotal = 1;
        juce::String programName;
        juce::AudioBuffer<float> offlineBuffer;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplingJob)
    };

    juce::String makeSafeFileName (const juce::String& name);
}
