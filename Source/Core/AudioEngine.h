#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "MidiLearn.h"

namespace wt
{
    /** A note that the sampler wants to record. The buffer is allocated by the sampling thread;
        the audio thread only writes into it. */
    struct CaptureRequest
    {
        int note = 60;
        int velocity = 127;
        int midiChannel = 1;
        int holdFrames = 0;             // note-off is sent after this many frames
        int maxFrames = 0;              // hard limit (hold + max release)
        float threshold = 0.001f;       // linear
        int silenceFrames = 0;          // the tail must stay below the threshold this long
        int minTailFrames = 0;

        juce::AudioBuffer<float> buffer;

        std::atomic<int> recordedFrames { 0 };
        std::atomic<int> noteOffFrame { -1 };
        std::atomic<bool> finished { false };
        std::atomic<bool> abortRequested { false };

        // used by the audio thread only
        int position = 0;
        int silentRun = 0;
        bool noteOnSent = false;
        bool noteOffSent = false;
    };

    /** Owns the audio device, the hosted plug-in, the MIDI routing and the recording tap.
        Live MIDI always reaches the plug-in, except while a note is being recorded. */
    class AudioEngine : private juce::AudioIODeviceCallback,
                        private juce::MidiInputCallback
    {
    public:
        AudioEngine();
        ~AudioEngine() override;

        /** Opens the default (or saved) audio device and every MIDI input. */
        juce::String initialiseDevices (const juce::XmlElement* savedState);
        void shutdownDevices();

        juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
        juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
        juce::KnownPluginList& getKnownPluginList() { return knownPlugins; }
        juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }
        MidiLearn& getMidiLearn() { return midiLearn; }

        // Plug-in ----------------------------------------------------------------
        using LoadCallback = std::function<void (bool ok, const juce::String& error)>;
        void loadPlugin (const juce::PluginDescription& description, LoadCallback callback);
        void setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin);   // takes ownership
        void unloadPlugin();
        juce::AudioPluginInstance* getPlugin() const { return plugin.get(); }
        juce::PluginDescription getPluginDescription() const { return currentDescription; }
        int getPluginLatency() const;

        // Recording --------------------------------------------------------------
        void startCapture (CaptureRequest* request);
        void stopCapture (CaptureRequest* request);
        bool isCapturing() const { return activeCapture.load() != nullptr; }
        void sendAllNotesOff();

        double getSampleRate() const { return sampleRate.load(); }
        int getBlockSize() const { return blockSize.load(); }

        /** Peak of the output since the last call (linear). Safe from any thread. */
        float getAndResetPeak() { return outputPeak.exchange (0.0f); }
        float getCurrentLevel() const { return blockLevel.load(); }
        /** Separate peak holder for the level meter of the UI. */
        float getMeterPeakAndReset() { return meterPeak.exchange (0.0f); }

        /** Called on the message thread for every incoming MIDI message (note learning etc.). */
        std::function<void (const juce::MidiMessage&)> onMidiMessage;

        /** Prepares the plug-in without an audio device (used by the tests and offline rendering). */
        void prepareOffline (double sampleRate, int blockSize);
        void processOffline (juce::AudioBuffer<float>& output, juce::MidiBuffer& extraMidi);

        juce::MidiMessageCollector& getMidiCollector() { return midiCollector; }

        /** Silent mode: the instrument is still recorded but nothing reaches the speakers. */
        void setMuteOutput (bool shouldMute) { muteOutput = shouldMute; }
        bool isOutputMuted() const { return muteOutput.load(); }

        /** Offline mode: the audio device outputs silence and the plug-in is rendered by
            processOffline() on another thread, as fast as it can (non-realtime). */
        void setOfflineMode (bool shouldBeOffline);
        bool isOffline() const { return offlineMode.load(); }

    private:
        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                               float* const* outputChannelData, int numOutputChannels,
                                               int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
        void audioDeviceStopped() override;
        void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

        void prepare (double sampleRate, int blockSize);
        void renderBlock (float* const* outputs, int numOutputs, int numSamples, juce::MidiBuffer* extraMidi);
        void runCapture (CaptureRequest& capture, juce::MidiBuffer& midi, int numSamples);
        void recordCapture (CaptureRequest& capture, int numSamples);

        struct SimplePlayHead : public juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                info.setBpm (120.0);
                info.setTimeSignature (TimeSignature { 4, 4 });
                info.setIsPlaying (false);
                return info;
            }
        };

        juce::AudioDeviceManager deviceManager;
        juce::AudioPluginFormatManager formatManager;
        juce::KnownPluginList knownPlugins;
        juce::MidiKeyboardState keyboardState;
        juce::MidiMessageCollector midiCollector;
        MidiLearn midiLearn;
        SimplePlayHead playHead;

        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::PluginDescription currentDescription;
        juce::CriticalSection pluginLock;

        juce::SpinLock captureLock;
        std::atomic<CaptureRequest*> activeCapture { nullptr };
        std::atomic<bool> allNotesOffPending { false };
        std::atomic<bool> muteOutput { false };
        std::atomic<bool> offlineMode { false };
        juce::CriticalSection renderLock;

        juce::AudioBuffer<float> processBuffer;
        juce::MidiBuffer midiBuffer;
        std::atomic<double> sampleRate { 48000.0 };
        std::atomic<int> blockSize { 512 };
        std::atomic<float> outputPeak { 0.0f };
        std::atomic<float> blockLevel { 0.0f };
        std::atomic<float> meterPeak { 0.0f };
        bool prepared = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
    };
}
