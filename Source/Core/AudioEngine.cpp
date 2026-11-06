#include "AudioEngine.h"

namespace wt
{
    AudioEngine::AudioEngine()
    {
       #if JUCE_MAJOR_VERSION > 8 || (JUCE_MAJOR_VERSION == 8 && JUCE_MINOR_VERSION >= 1)
        juce::addDefaultFormatsToManager (formatManager);
       #else
        formatManager.addDefaultFormats();
       #endif
    }

    AudioEngine::~AudioEngine()
    {
        shutdownDevices();
        unloadPlugin();
    }

    juce::String AudioEngine::initialiseDevices (const juce::XmlElement* savedState)
    {
        const auto error = deviceManager.initialise (0, 2, savedState, true);

        // Listen to every MIDI input that is enabled (the device selector lets the user change this)
        if (savedState == nullptr)
            for (auto& input : juce::MidiInput::getAvailableDevices())
                deviceManager.setMidiInputDeviceEnabled (input.identifier, true);

        deviceManager.addMidiInputDeviceCallback ({}, this);
        deviceManager.addAudioCallback (this);
        return error;
    }

    void AudioEngine::shutdownDevices()
    {
        deviceManager.removeAudioCallback (this);
        deviceManager.removeMidiInputDeviceCallback ({}, this);
        deviceManager.closeAudioDevice();
    }

    //==============================================================================
    void AudioEngine::loadPlugin (const juce::PluginDescription& description, LoadCallback callback)
    {
        formatManager.createPluginInstanceAsync (description, sampleRate.load(), blockSize.load(),
            [this, description, callback] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
            {
                if (instance == nullptr)
                {
                    if (callback) callback (false, error.isNotEmpty() ? error : juce::String ("No se pudo cargar el plugin"));
                    return;
                }

                currentDescription = description;
                setPlugin (std::move (instance));
                if (callback) callback (true, {});
            });
    }

    void AudioEngine::setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin)
    {
        if (newPlugin != nullptr)
        {
            // Instruments: no inputs, stereo output if the plug-in allows it
            newPlugin->enableAllBuses();
            auto layout = newPlugin->getBusesLayout();
            if (layout.outputBuses.size() > 0)
            {
                layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
                if (newPlugin->checkBusesLayoutSupported (layout))
                    newPlugin->setBusesLayout (layout);
            }

            newPlugin->setPlayHead (&playHead);
            newPlugin->setRateAndBufferSizeDetails (sampleRate.load(), blockSize.load());
            newPlugin->prepareToPlay (sampleRate.load(), blockSize.load());
            newPlugin->setNonRealtime (false);
            if (currentDescription.name.isEmpty())
                currentDescription = newPlugin->getPluginDescription();
        }

        std::unique_ptr<juce::AudioPluginInstance> old;
        {
            const juce::ScopedLock sl (pluginLock);
            old = std::move (plugin);
            plugin = std::move (newPlugin);
        }

        midiLearn.setPlugin (plugin.get());

        if (old != nullptr)
            old->releaseResources();
    }

    void AudioEngine::unloadPlugin()
    {
        midiLearn.setPlugin (nullptr);
        std::unique_ptr<juce::AudioPluginInstance> old;
        {
            const juce::ScopedLock sl (pluginLock);
            old = std::move (plugin);
        }
        if (old != nullptr)
            old->releaseResources();
        currentDescription = {};
    }

    int AudioEngine::getPluginLatency() const
    {
        const juce::ScopedLock sl (pluginLock);
        return plugin != nullptr ? plugin->getLatencySamples() : 0;
    }

    //==============================================================================
    void AudioEngine::startCapture (CaptureRequest* request)
    {
        const juce::SpinLock::ScopedLockType sl (captureLock);
        activeCapture.store (request);
    }

    void AudioEngine::stopCapture (CaptureRequest* request)
    {
        const juce::SpinLock::ScopedLockType sl (captureLock);
        if (activeCapture.load() == request)
            activeCapture.store (nullptr);
    }

    void AudioEngine::sendAllNotesOff()
    {
        allNotesOffPending = true;
    }

    //==============================================================================
    void AudioEngine::prepare (double newSampleRate, int newBlockSize)
    {
        sampleRate = newSampleRate;
        blockSize = newBlockSize;
        processBuffer.setSize (32, juce::jmax (newBlockSize, 8192));
        midiBuffer.ensureSize (4096);
        midiCollector.reset (newSampleRate);

        const juce::ScopedLock sl (pluginLock);
        if (plugin != nullptr)
        {
            plugin->releaseResources();
            plugin->setRateAndBufferSizeDetails (newSampleRate, newBlockSize);
            plugin->prepareToPlay (newSampleRate, newBlockSize);
        }
        prepared = true;
    }

    void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
    {
        prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    }

    void AudioEngine::audioDeviceStopped()
    {
        const juce::ScopedLock sl (pluginLock);
        if (plugin != nullptr)
            plugin->releaseResources();
        prepared = false;
    }

    void AudioEngine::prepareOffline (double newSampleRate, int newBlockSize)
    {
        prepare (newSampleRate, newBlockSize);
    }

    void AudioEngine::processOffline (juce::AudioBuffer<float>& output, juce::MidiBuffer& extraMidi)
    {
        renderBlock (output.getArrayOfWritePointers(), output.getNumChannels(), output.getNumSamples(), &extraMidi);
    }

    void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                        float* const* outputChannelData, int numOutputChannels,
                                                        int numSamples, const juce::AudioIODeviceCallbackContext&)
    {
        renderBlock (outputChannelData, numOutputChannels, numSamples, nullptr);
    }

    void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
    {
        if (message.isActiveSense() || message.isMidiClock())
            return;

        const bool usedByMapping = midiLearn.handleIncomingMidi (message);

        if (! usedByMapping)
        {
            auto copy = message;
            copy.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
            midiCollector.addMessageToQueue (copy);
        }

        if (onMidiMessage != nullptr && (message.isNoteOnOrOff() || message.isController()))
        {
            juce::MessageManager::callAsync ([this, message]
            {
                if (onMidiMessage != nullptr)
                    onMidiMessage (message);
            });
        }
    }

    //==============================================================================
    void AudioEngine::runCapture (CaptureRequest& c, juce::MidiBuffer& midi, int numSamples)
    {
        if (! c.noteOnSent)
        {
            midi.addEvent (juce::MidiMessage::noteOn (c.midiChannel, c.note, (juce::uint8) juce::jlimit (1, 127, c.velocity)), 0);
            c.noteOnSent = true;
        }

        if (! c.noteOffSent)
        {
            int offset = c.holdFrames - c.position;
            if (c.abortRequested.load())
                offset = 0;

            if (offset < numSamples)
            {
                offset = juce::jmax (0, offset);
                midi.addEvent (juce::MidiMessage::noteOff (c.midiChannel, c.note), offset);
                c.noteOffSent = true;
                c.noteOffFrame.store (c.position + offset);
            }
        }
    }

    void AudioEngine::recordCapture (CaptureRequest& c, int numSamples)
    {
        const int space = c.maxFrames - c.position;
        const int n = juce::jmin (numSamples, juce::jmax (0, space));
        const int numPluginChannels = juce::jmin (processBuffer.getNumChannels(), c.buffer.getNumChannels());
        bool done = false;
        int endPosition = c.position + n;

        for (int ch = 0; ch < c.buffer.getNumChannels(); ++ch)
        {
            const int srcCh = juce::jmin (ch, juce::jmax (0, numPluginChannels - 1));
            c.buffer.copyFrom (ch, c.position, processBuffer, srcCh, 0, n);
        }

        // silence detection after the note-off
        for (int i = 0; i < n && ! done; ++i)
        {
            const int pos = c.position + i;
            if (! c.noteOffSent || pos < c.holdFrames)
                continue;

            float level = 0.0f;
            for (int ch = 0; ch < c.buffer.getNumChannels(); ++ch)
                level = juce::jmax (level, std::abs (c.buffer.getSample (ch, pos)));

            c.silentRun = level < c.threshold ? c.silentRun + 1 : 0;

            if (c.silentRun >= c.silenceFrames && pos >= c.holdFrames + c.minTailFrames)
            {
                done = true;
                endPosition = pos + 1;
            }
        }

        c.position = endPosition;

        if (c.abortRequested.load() && c.noteOffSent)
            done = true;

        if (done || c.position >= c.maxFrames)
        {
            c.recordedFrames.store (c.position);
            activeCapture.store (nullptr);
            c.finished.store (true);   // last access by the audio thread
        }
    }

    void AudioEngine::renderBlock (float* const* outputs, int numOutputs, int numSamples, juce::MidiBuffer* extraMidi)
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);

        if (numSamples > processBuffer.getNumSamples())
            return; // should never happen: prepare() allocates generously

        midiBuffer.clear();
        midiCollector.removeNextBlockOfMessages (midiBuffer, numSamples);
        if (extraMidi != nullptr)
            midiBuffer.addEvents (*extraMidi, 0, numSamples, 0);

        // on-screen keyboard -> plug-in, and incoming notes -> on-screen keyboard
        keyboardState.processNextMidiBuffer (midiBuffer, 0, numSamples, true);

        const juce::SpinLock::ScopedTryLockType captureTryLock (captureLock);
        CaptureRequest* capture = captureTryLock.isLocked() ? activeCapture.load() : nullptr;
        if (capture != nullptr && capture->finished.load())
            capture = nullptr;

        if (capture != nullptr)
        {
            // While recording only the sampler talks to the instrument
            midiBuffer.clear();
            runCapture (*capture, midiBuffer, numSamples);
            juce::MidiBuffer display (midiBuffer);
            keyboardState.processNextMidiBuffer (display, 0, numSamples, false);
        }

        if (allNotesOffPending.exchange (false))
            for (int channel = 1; channel <= 16; ++channel)
            {
                midiBuffer.addEvent (juce::MidiMessage::allNotesOff (channel), 0);
                midiBuffer.addEvent (juce::MidiMessage::controllerEvent (channel, 64, 0), 0);
            }

        float peak = 0.0f;
        {
            const juce::ScopedTryLock sl (pluginLock);

            if (sl.isLocked() && plugin != nullptr && prepared)
            {
                const int numChannels = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
                juce::AudioBuffer<float> buffer (processBuffer.getArrayOfWritePointers(),
                                                 juce::jmin (numChannels, processBuffer.getNumChannels()), numSamples);
                buffer.clear();

                plugin->processBlock (buffer, midiBuffer);

                const int pluginOuts = juce::jmax (1, plugin->getTotalNumOutputChannels());
                for (int ch = 0; ch < numOutputs; ++ch)
                    if (outputs[ch] != nullptr)
                        juce::FloatVectorOperations::copy (outputs[ch], buffer.getReadPointer (juce::jmin (ch, pluginOuts - 1)), numSamples);

                // a mono instrument is recorded on both channels
                if (pluginOuts == 1)
                    processBuffer.copyFrom (1, 0, processBuffer, 0, 0, numSamples);

                peak = juce::jmax (buffer.getMagnitude (0, 0, numSamples),
                                   buffer.getMagnitude (juce::jmin (1, pluginOuts - 1), 0, numSamples));
            }
            else
            {
                processBuffer.clear (0, numSamples);
            }
        }

        if (capture != nullptr)
            recordCapture (*capture, numSamples);

        blockLevel.store (peak);
        float previous = outputPeak.load();
        while (peak > previous && ! outputPeak.compare_exchange_weak (previous, peak)) {}
        float previousMeter = meterPeak.load();
        while (peak > previousMeter && ! meterPeak.compare_exchange_weak (previousMeter, peak)) {}
    }
}
