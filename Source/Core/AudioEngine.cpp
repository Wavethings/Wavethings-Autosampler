#include "AudioEngine.h"

namespace wt
{
    AudioEngine::AudioEngine()
    {
        juce::addDefaultFormatsToManager (formatManager);   // JUCE >= 8.0.11
    }

    AudioEngine::~AudioEngine()
    {
        shutdownDevices();
        unloadPlugin();
    }

    juce::String AudioEngine::initialiseDevices (const juce::XmlElement* savedState)
    {
        const auto error = deviceManager.initialise (2, 2, savedState, true);

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
        setMidiOutput ({});
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
        updateInputMapping();
    }

    //==============================================================================
    void AudioEngine::setSoundSource (SoundSource s)
    {
        if (source.load() != s)
        {
            sendAllNotesOff();
            source = s;
        }
    }

    bool AudioEngine::setMidiOutput (const juce::String& identifier)
    {
        std::unique_ptr<juce::MidiOutput> newOutput;
        if (identifier.isNotEmpty())
        {
            newOutput = juce::MidiOutput::openDevice (identifier);
            if (newOutput != nullptr)
                newOutput->startBackgroundThread();
        }

        std::unique_ptr<juce::MidiOutput> old;
        {
            const juce::SpinLock::ScopedLockType sl (midiOutputLock);
            old = std::move (midiOutput);
            midiOutput = std::move (newOutput);
            midiOutputId = midiOutput != nullptr ? identifier : juce::String();
        }

        if (old != nullptr)
        {
            // don't leave notes hanging on the instrument we are leaving
            for (int ch = 1; ch <= 16; ++ch)
                old->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
            old->stopBackgroundThread();
        }
        return identifier.isEmpty() || midiOutputId.isNotEmpty();
    }

    void AudioEngine::setInputChannels (int firstChannel, bool stereo)
    {
        wantedInputChannel = juce::jmax (0, firstChannel);
        wantedInputStereo = stereo;

        // the chosen inputs must be active on the device
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            auto setup = deviceManager.getAudioDeviceSetup();
            auto active = device->getActiveInputChannels();
            const int numInputs = device->getInputChannelNames().size();
            bool changed = false;
            for (int ch = wantedInputChannel; ch <= wantedInputChannel + (stereo ? 1 : 0) && ch < numInputs; ++ch)
                if (! active[ch]) { active.setBit (ch); changed = true; }

            if (changed)
            {
                setup.useDefaultInputChannels = false;
                setup.inputChannels = active;
                deviceManager.setAudioDeviceSetup (setup, true);   // restarts the device -> updateInputMapping()
                return;
            }
        }
        updateInputMapping();
    }

    void AudioEngine::updateInputMapping()
    {
        // the callback receives only the active inputs, packed: find where ours are
        int left = -1, right = -1;
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            const auto active = device->getActiveInputChannels();
            int index = 0;
            for (int ch = 0; ch <= active.getHighestBit(); ++ch)
            {
                if (! active[ch]) continue;
                if (ch == wantedInputChannel) left = index;
                if (ch == wantedInputChannel + 1 && wantedInputStereo) right = index;
                ++index;
            }
        }
        inputIndexLeft = left;
        inputIndexRight = wantedInputStereo ? right : left;
    }

    juce::StringArray AudioEngine::getInputChannelNames() const
    {
        if (auto* device = deviceManager.getCurrentAudioDevice())
            return device->getInputChannelNames();
        return {};
    }

    int AudioEngine::getRecordingLatency() const
    {
        if (! isHardware())
            return getPluginLatency();

        int latency = (int) (extraLatencyMs.load() * sampleRate.load() / 1000.0);
        if (hardwareSimulator == nullptr)
            if (auto* device = const_cast<juce::AudioDeviceManager&> (deviceManager).getCurrentAudioDevice())
                latency += device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
        return juce::jmax (0, latency);
    }

    void AudioEngine::queueMidi (const juce::MidiMessage& message)
    {
        const auto scope = queuedMidiFifo.write (1);
        if (scope.blockSize1 > 0) queuedMidi[(size_t) scope.startIndex1] = message;
        else if (scope.blockSize2 > 0) queuedMidi[(size_t) scope.startIndex2] = message;
    }

    void AudioEngine::setPluginProgram (int index)
    {
        const juce::ScopedLock rl (renderLock);   // not in the middle of processBlock
        const juce::ScopedLock sl (pluginLock);
        if (plugin != nullptr && index >= 0 && index < plugin->getNumPrograms())
            plugin->setCurrentProgram (index);
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
        const juce::ScopedLock rl (renderLock);
        renderBlock (nullptr, 0, output.getArrayOfWritePointers(), output.getNumChannels(), output.getNumSamples(), &extraMidi);
    }

    void AudioEngine::setOfflineMode (bool shouldBeOffline)
    {
        const juce::ScopedLock rl (renderLock);   // waits until the audio callback has left the plug-in
        offlineMode = shouldBeOffline;

        const juce::ScopedLock sl (pluginLock);
        if (plugin != nullptr)
            plugin->setNonRealtime (shouldBeOffline);
    }

    void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                        float* const* outputChannelData, int numOutputChannels,
                                                        int numSamples, const juce::AudioIODeviceCallbackContext&)
    {
        const juce::ScopedTryLock rl (renderLock);

        if (! rl.isLocked() || offlineMode.load())
        {
            // the sampler is rendering faster than real time on its own thread
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (outputChannelData[ch] != nullptr)
                    juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
            return;
        }

        renderBlock (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples, nullptr);

        if (muteOutput.load())
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (outputChannelData[ch] != nullptr)
                    juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
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

    void AudioEngine::renderBlock (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                   int numSamples, juce::MidiBuffer* extraMidi)
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);

        if (numSamples > processBuffer.getNumSamples())
            return; // should never happen: prepare() allocates generously

        midiBuffer.clear();
        midiCollector.removeNextBlockOfMessages (midiBuffer, numSamples);
        if (offlineMode.load())
            midiBuffer.clear();   // live playing is ignored while rendering offline
        if (extraMidi != nullptr)
            midiBuffer.addEvents (*extraMidi, 0, numSamples, 0);

        // on-screen keyboard -> plug-in, and incoming notes -> on-screen keyboard
        keyboardState.processNextMidiBuffer (midiBuffer, 0, numSamples, true);

        const bool hardware = isHardware();
        if (hardware)
        {
            // live MIDI thru: whatever channel the controller uses, the instrument listens on ours
            juce::MidiBuffer remapped;
            const int channel = midiChannel.load();
            for (const auto meta : midiBuffer)
            {
                auto m = meta.getMessage();
                if (m.getChannel() > 0)
                    m.setChannel (channel);
                remapped.addEvent (m, meta.samplePosition);
            }
            midiBuffer.swapWith (remapped);
        }

        // program changes etc. from the sampler (also while rendering offline)
        if (queuedMidiFifo.getNumReady() > 0)
        {
            const auto scope = queuedMidiFifo.read (queuedMidiFifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) midiBuffer.addEvent (queuedMidi[(size_t) (scope.startIndex1 + i)], 0);
            for (int i = 0; i < scope.blockSize2; ++i) midiBuffer.addEvent (queuedMidi[(size_t) (scope.startIndex2 + i)], 0);
        }

        const juce::SpinLock::ScopedTryLockType captureTryLock (captureLock);
        CaptureRequest* capture = captureTryLock.isLocked() ? activeCapture.load() : nullptr;
        if (capture != nullptr && capture->finished.load())
            capture = nullptr;

        if (capture != nullptr)
        {
            // While recording only the sampler talks to the instrument
            midiBuffer.clear();
            capture->midiChannel = midiChannel.load();
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
        if (hardware)
        {
            juce::AudioBuffer<float> in (processBuffer.getArrayOfWritePointers(), 2, numSamples);
            in.clear();

            if (hardwareSimulator != nullptr)
            {
                hardwareSimulator (midiBuffer, in);
            }
            else
            {
                {
                    const juce::SpinLock::ScopedTryLockType sl (midiOutputLock);
                    if (sl.isLocked() && midiOutput != nullptr && ! midiBuffer.isEmpty())
                        midiOutput->sendBlockOfMessages (midiBuffer, juce::Time::getMillisecondCounterHiRes(), sampleRate.load());
                }

                const int left = inputIndexLeft.load(), right = inputIndexRight.load();
                if (inputs != nullptr && left >= 0 && left < numInputs && inputs[left] != nullptr)
                {
                    in.copyFrom (0, 0, inputs[left], numSamples);
                    const int r = (right >= 0 && right < numInputs && inputs[right] != nullptr) ? right : left;
                    in.copyFrom (1, 0, inputs[r], numSamples);
                }
            }

            peak = in.getMagnitude (0, numSamples);
            if (peak >= 0.999f)
                clipped = true;

            if (monitorInput.load())
                for (int ch = 0; ch < numOutputs; ++ch)
                    if (outputs[ch] != nullptr)
                        juce::FloatVectorOperations::copy (outputs[ch], in.getReadPointer (juce::jmin (ch, 1)), numSamples);
        }
        else
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
        float previousNoise = noisePeak.load();
        while (peak > previousNoise && ! noisePeak.compare_exchange_weak (previousNoise, peak)) {}
        float previous = outputPeak.load();
        while (peak > previous && ! outputPeak.compare_exchange_weak (previous, peak)) {}
        float previousMeter = meterPeak.load();
        while (peak > previousMeter && ! meterPeak.compare_exchange_weak (previousMeter, peak)) {}
    }
}
