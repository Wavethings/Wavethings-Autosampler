#include "MidiLearn.h"
#include "Text.h"

namespace wt
{
    MidiLearn::~MidiLearn()
    {
        cancelPendingUpdate();
        setPlugin (nullptr);
    }

    void MidiLearn::setPlugin (juce::AudioPluginInstance* newPlugin)
    {
        {
            const juce::ScopedLock sl (lock);
            if (plugin != nullptr)
                plugin->removeListener (this);

            plugin = newPlugin;
            mappings.clear();

            if (plugin != nullptr)
                plugin->addListener (this);
        }

        learning = false;
        capturing = false;
        learnParameter = -1;
        learnCC = -1;
        sendChangeMessage();
    }

    juce::AudioProcessorParameter* MidiLearn::getParameter (int index) const
    {
        if (plugin == nullptr)
            return nullptr;

        auto& params = plugin->getParameters();
        return juce::isPositiveAndBelow (index, params.size()) ? params[index] : nullptr;
    }

    juce::StringArray MidiLearn::getParameterNames() const
    {
        juce::StringArray names;
        if (plugin != nullptr)
            for (auto* p : plugin->getParameters())
                names.add (p->getName (64));
        return names;
    }

    bool MidiLearn::handleIncomingMidi (const juce::MidiMessage& message)
    {
        if (! message.isController())
            return false;

        const int cc = message.getControllerNumber();
        const int channel = message.getChannel();

        if (learning.load())
        {
            learnCC = cc;
            learnChannel = channel;
            triggerAsyncUpdate();
            return true;
        }

        const juce::ScopedLock sl (lock);
        bool used = false;

        for (auto& m : mappings)
        {
            if (m.ccNumber == cc && (m.midiChannel == 0 || m.midiChannel == channel))
            {
                if (auto* p = getParameter (m.parameterIndex))
                {
                    ignoreParameterCallbacks = true;
                    const float value = m.type == ControlType::button ? (message.getControllerValue() >= 64 ? 1.0f : 0.0f)
                                                                      : (float) message.getControllerValue() / 127.0f;
                    p->setValueNotifyingHost (value);
                    ignoreParameterCallbacks = false;
                }
                used = true;
            }
        }

        return used;
    }

    void MidiLearn::setLearning (bool shouldLearn)
    {
        learning = shouldLearn;
        learnKeepsType = false;
        if (shouldLearn)
            capturing = false;
        learnParameter = -1;
        learnCC = -1;
        sendChangeMessage();
    }

    void MidiLearn::setLearnParameter (int parameterIndex)
    {
        learnParameter = parameterIndex;
        triggerAsyncUpdate();
    }

    juce::String MidiLearn::getLearnStatusText() const
    {
        if (! learning.load())
            return {};

        const int p = learnParameter.load();
        const int cc = learnCC.load();

        if (p < 0 && cc < 0)
            return "MIDI Learn: mueve un control del plugin y un control de tu teclado MIDI";
        if (p >= 0 && cc < 0)
            return "Parámetro \""_u8 + getParameterNames()[p] + "\" seleccionado: ahora mueve un control MIDI";
        return "CC " + juce::String (cc) + " recibido: ahora mueve el control del plugin";
    }

    void MidiLearn::setCapturing (bool shouldCapture)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingCaptures.clear();
            movingByThemselves.clear();
            lastCaptured = {};
        }
        captureStartMs = juce::Time::getMillisecondCounterHiRes();
        capturing = shouldCapture && plugin != nullptr;
        if (capturing.load())
            learning = false;
        sendChangeMessage();
    }

    void MidiLearn::captureParameter (int parameterIndex, bool fromGesture)
    {
        const juce::ScopedLock sl (lock);

        // first half second: whatever changes on its own (LFOs, meters, envelopes) is not the user
        if (! fromGesture)
        {
            if (juce::Time::getMillisecondCounterHiRes() - captureStartMs.load() < 600.0)
            {
                movingByThemselves.insert (parameterIndex);
                return;
            }
            if (movingByThemselves.count (parameterIndex) > 0)
                return;
        }

        if (std::find (pendingCaptures.begin(), pendingCaptures.end(), parameterIndex) == pendingCaptures.end())
            pendingCaptures.push_back (parameterIndex);
        triggerAsyncUpdate();
    }

    void MidiLearn::learnCcFor (int parameterIndex)
    {
        capturing = false;
        learning = true;
        learnKeepsType = true;
        learnCC = -1;
        learnParameter = parameterIndex;
        sendChangeMessage();
    }

    void MidiLearn::setCc (int parameterIndex, int ccNumber, int midiChannel)
    {
        {
            const juce::ScopedLock sl (lock);
            for (auto& m : mappings)
            {
                if (ccNumber >= 0 && m.ccNumber == ccNumber && m.parameterIndex != parameterIndex
                    && (m.midiChannel == 0 || midiChannel == 0 || m.midiChannel == midiChannel))
                    m.ccNumber = -1;   // a CC moves only one parameter
                if (m.parameterIndex == parameterIndex)
                {
                    m.ccNumber = ccNumber;
                    m.midiChannel = juce::jlimit (0, 16, midiChannel);
                }
            }
        }
        sendChangeMessage();
    }

    void MidiLearn::audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int parameterIndex)
    {
        if (capturing.load() && ! ignoreParameterCallbacks.load())
        {
            captureParameter (parameterIndex, true);
            return;
        }

        if (learning.load() && ! ignoreParameterCallbacks.load())
        {
            learnParameter = parameterIndex;
            triggerAsyncUpdate();
        }
    }

    void MidiLearn::audioProcessorParameterChanged (juce::AudioProcessor*, int parameterIndex, float)
    {
        if (capturing.load() && ! ignoreParameterCallbacks.load())
        {
            captureParameter (parameterIndex, false);
            return;
        }

        // Some plug-ins don't send gestures; take value changes as a fallback while learning
        if (learning.load() && ! ignoreParameterCallbacks.load() && learnParameter.load() < 0)
        {
            learnParameter = parameterIndex;
            triggerAsyncUpdate();
        }
    }

    void MidiLearn::handleAsyncUpdate()
    {
        std::vector<int> captured;
        {
            const juce::ScopedLock sl (lock);
            captured.swap (pendingCaptures);
        }
        if (capturing.load())
        {
            const auto names = getParameterNames();
            for (int index : captured)
            {
                if (! juce::isPositiveAndBelow (index, names.size()) || isHelperParameterName (names[index]))
                    continue;

                bool exists = false;
                for (auto& m : getMappings())
                    exists = exists || m.parameterIndex == index;
                if (! exists)
                    addMapping (index, -1, 0, learnType.load());

                const juce::ScopedLock sl (lock);
                lastCaptured = names[index];
            }
        }

        completeLearnIfReady();
        sendChangeMessage();
    }

    void MidiLearn::completeLearnIfReady()
    {
        if (! learning.load())
            return;

        const int p = learnParameter.load();
        const int cc = learnCC.load();

        if (p >= 0 && cc >= 0)
        {
            bool exists = false;
            for (auto& m : getMappings())
                exists = exists || m.parameterIndex == p;
            addMapping (p, cc, 0, (learnKeepsType.load() || exists) ? -1 : learnType.load());
            learnKeepsType = false;
            learning = false;
            learnParameter = -1;
            learnCC = -1;
        }
    }

    std::vector<MidiLearn::Mapping> MidiLearn::getMappings() const
    {
        const juce::ScopedLock sl (lock);
        return mappings;
    }

    MidiLearn::ControlType MidiLearn::guessControlType (int parameterIndex) const
    {
        if (auto* p = getParameter (parameterIndex))
            if (p->isBoolean() || p->getNumSteps() == 2)
                return ControlType::button;
        return ControlType::knob;
    }

    void MidiLearn::setControlType (int parameterIndex, ControlType type)
    {
        {
            const juce::ScopedLock sl (lock);
            for (auto& m : mappings)
                if (m.parameterIndex == parameterIndex)
                    m.type = type;
        }
        sendChangeMessage();
    }

    void MidiLearn::addMapping (int parameterIndex, int ccNumber, int midiChannel, int type)
    {
        {
            const juce::ScopedLock sl (lock);

            auto controlType = type >= 0 ? (ControlType) type : guessControlType (parameterIndex);
            if (type < 0)
                for (auto& existing : mappings)
                    if (existing.parameterIndex == parameterIndex)
                        controlType = existing.type;

            // a CC controls only one parameter, a parameter has only one entry
            mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const Mapping& m)
            {
                return m.parameterIndex == parameterIndex || (ccNumber >= 0 && m.ccNumber == ccNumber);
            }), mappings.end());

            Mapping m;
            m.parameterIndex = parameterIndex;
            m.ccNumber = ccNumber;
            m.midiChannel = midiChannel;
            m.parameterName = getParameterNames()[parameterIndex];
            m.type = controlType;
            mappings.push_back (m);
        }
        sendChangeMessage();
    }

    void MidiLearn::removeMapping (int parameterIndex)
    {
        {
            const juce::ScopedLock sl (lock);
            mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const Mapping& m)
            {
                return m.parameterIndex == parameterIndex;
            }), mappings.end());
        }
        sendChangeMessage();
    }

    void MidiLearn::clearMappings()
    {
        {
            const juce::ScopedLock sl (lock);
            mappings.clear();
        }
        sendChangeMessage();
    }

    std::unique_ptr<juce::XmlElement> MidiLearn::toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("MidiMappings");
        const juce::ScopedLock sl (lock);
        for (auto& m : mappings)
        {
            auto* e = xml->createNewChildElement ("Mapping");
            e->setAttribute ("parameter", m.parameterIndex);
            e->setAttribute ("cc", m.ccNumber);
            e->setAttribute ("channel", m.midiChannel);
            e->setAttribute ("name", m.parameterName);
            e->setAttribute ("type", m.type == ControlType::button ? "button" : "knob");
        }
        return xml;
    }

    void MidiLearn::fromXml (const juce::XmlElement& xml)
    {
        {
            const juce::ScopedLock sl (lock);
            mappings.clear();
            const int numParams = plugin != nullptr ? plugin->getParameters().size() : 0;

            for (auto* e : xml.getChildWithTagNameIterator ("Mapping"))
            {
                Mapping m;
                m.parameterIndex = e->getIntAttribute ("parameter", -1);
                m.ccNumber = e->getIntAttribute ("cc", -1);
                m.midiChannel = e->getIntAttribute ("channel", 0);
                m.parameterName = e->getStringAttribute ("name");
                m.type = e->getStringAttribute ("type") == "button" ? ControlType::button : ControlType::knob;
                if (juce::isPositiveAndBelow (m.parameterIndex, numParams))
                    mappings.push_back (m);
            }
        }
        sendChangeMessage();
    }
}
