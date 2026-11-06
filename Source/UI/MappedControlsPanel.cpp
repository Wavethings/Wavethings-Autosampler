#include "MappedControlsPanel.h"
#include "WavethingsLookAndFeel.h"
#include "../Core/Text.h"

namespace wt
{
    class MappedControlsPanel::Knob : public juce::Component
    {
    public:
        Knob (MidiLearn& ml, const MidiLearn::Mapping& m) : midiLearn (ml), mapping (m)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange (0.0, 1.0);
            slider.setPopupDisplayEnabled (true, true, nullptr);
            slider.textFromValueFunction = [this] (double v)
            {
                if (auto* p = midiLearn.getParameter (mapping.parameterIndex))
                    return p->getText ((float) v, 32) + " " + p->getLabel();
                return juce::String (v, 2);
            };
            slider.onDragStart = [this] { if (auto* p = param()) p->beginChangeGesture(); };
            slider.onDragEnd = [this] { if (auto* p = param()) p->endChangeGesture(); };
            slider.onValueChange = [this]
            {
                if (auto* p = param())
                    if (std::abs (p->getValue() - (float) slider.getValue()) > 1.0e-5f)
                        p->setValueNotifyingHost ((float) slider.getValue());
                valueLabel.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
            };
            addAndMakeVisible (slider);

            name.setText (mapping.parameterName, juce::dontSendNotification);
            name.setJustificationType (juce::Justification::centred);
            name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            name.setMinimumHorizontalScale (0.6f);
            addAndMakeVisible (name);

            valueLabel.setJustificationType (juce::Justification::centred);
            valueLabel.setFont (juce::FontOptions (11.0f));
            valueLabel.setColour (juce::Label::textColourId, Colours::textDim);
            addAndMakeVisible (valueLabel);

            ccLabel.setText (mapping.ccNumber >= 0 ? "CC " + juce::String (mapping.ccNumber) : juce::String ("sin CC"), juce::dontSendNotification);
            ccLabel.setJustificationType (juce::Justification::centred);
            ccLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            ccLabel.setColour (juce::Label::textColourId, Colours::accent2);
            addAndMakeVisible (ccLabel);

            remove.setButtonText ("x");
            remove.setTooltip ("Quitar del panel"_u8);
            remove.onClick = [this] { auto& learn = midiLearn; const int index = mapping.parameterIndex;
                                      juce::MessageManager::callAsync ([&learn, index] { learn.removeMapping (index); }); };
            addAndMakeVisible (remove);

            sync();
        }

        juce::AudioProcessorParameter* param() const { return midiLearn.getParameter (mapping.parameterIndex); }

        void sync()
        {
            if (auto* p = param())
                if (! slider.isMouseButtonDown())
                    slider.setValue (p->getValue(), juce::dontSendNotification);
            valueLabel.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Colours::panelLight);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (4, 3);
            auto top = b.removeFromTop (14);
            remove.setBounds (top.removeFromRight (14));
            ccLabel.setBounds (top);
            name.setBounds (b.removeFromBottom (14));
            valueLabel.setBounds (b.removeFromBottom (13));
            slider.setBounds (b);
        }

        MidiLearn& midiLearn;
        MidiLearn::Mapping mapping;
        juce::Slider slider;
        juce::Label name, valueLabel, ccLabel;
        juce::TextButton remove;
    };

    //==============================================================================
    MappedControlsPanel::MappedControlsPanel (MidiLearn& ml) : midiLearn (ml)
    {
        learnButton.setClickingTogglesState (true);
        learnButton.setTooltip ("Activa MIDI Learn, mueve un control del plugin (o elígelo en la lista) y después un control de tu teclado MIDI"_u8);
        learnButton.onClick = [this] { midiLearn.setLearning (learnButton.getToggleState()); };
        addAndMakeVisible (learnButton);

        parameterBox.setTextWhenNothingSelected ("Parámetro del plugin..."_u8);
        parameterBox.setTextWhenNoChoicesAvailable ("Carga un plugin"_u8);
        parameterBox.onChange = [this]
        {
            if (midiLearn.isLearning() && parameterBox.getSelectedId() > 0)
                midiLearn.setLearnParameter (parameterBox.getSelectedId() - 1);
        };
        addAndMakeVisible (parameterBox);

        addButton.setButtonText ("Añadir al panel"_u8);
        addButton.setTooltip ("Añade el parámetro seleccionado sin asignarle un CC"_u8);
        addButton.onClick = [this]
        {
            const int index = parameterBox.getSelectedId() - 1;
            if (index >= 0)
            {
                // keep an existing CC assignment
                int cc = -1;
                for (auto& m : midiLearn.getMappings())
                    if (m.parameterIndex == index) cc = m.ccNumber;
                midiLearn.addMapping (index, cc);
            }
        };
        addAndMakeVisible (addButton);

        clearButton.setButtonText ("Borrar todo");
        clearButton.onClick = [this] { midiLearn.clearMappings(); };
        addAndMakeVisible (clearButton);

        statusLabel.setFont (juce::FontOptions (12.0f));
        statusLabel.setColour (juce::Label::textColourId, Colours::accent2);
        addAndMakeVisible (statusLabel);

        viewport.setViewedComponent (&knobHolder, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);

        midiLearn.addChangeListener (this);
        startTimerHz (15);
        refresh();
    }

    MappedControlsPanel::~MappedControlsPanel()
    {
        midiLearn.removeChangeListener (this);
    }

    void MappedControlsPanel::updateParameterBox()
    {
        const auto names = midiLearn.getParameterNames();
        if (cachedParameterNames != names || parameterBox.getNumItems() != names.size())
        {
            cachedParameterNames = names;
            parameterBox.clear (juce::dontSendNotification);
            for (int i = 0; i < names.size(); ++i)
            {
                // VST3 plug-ins made with JUCE expose 2000+ "MIDI CC x|y" helper parameters: hide them
                if (names[i].startsWith ("MIDI CC ") && names[i].containsChar ('|'))
                    continue;
                parameterBox.addItem (juce::String (i + 1) + ". " + names[i], i + 1);
            }
        }
    }

    void MappedControlsPanel::refresh()
    {
        updateParameterBox();

        knobs.clear();
        for (auto& m : midiLearn.getMappings())
        {
            auto knob = std::make_unique<Knob> (midiLearn, m);
            knobHolder.addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        }

        learnButton.setToggleState (midiLearn.isLearning(), juce::dontSendNotification);
        statusLabel.setText (midiLearn.isLearning() ? midiLearn.getLearnStatusText()
                                                    : (knobs.empty() ? "Asigna controles del plugin (p. ej. el Delay o la Reverb) para tenerlos a mano"_u8
                                                                     : juce::String (knobs.size()) + " controles asignados"),
                             juce::dontSendNotification);
        resized();
    }

    void MappedControlsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        refresh();
        if (onMappingsChanged)
            onMappingsChanged();
    }

    void MappedControlsPanel::timerCallback()
    {
        for (auto& k : knobs)
            k->sync();

        if (midiLearn.isLearning())
            statusLabel.setText (midiLearn.getLearnStatusText(), juce::dontSendNotification);
    }

    void MappedControlsPanel::paint (juce::Graphics&) {}

    void MappedControlsPanel::resized()
    {
        auto b = getLocalBounds();
        auto top = b.removeFromTop (26);
        learnButton.setBounds (top.removeFromLeft (100));
        top.removeFromLeft (6);
        parameterBox.setBounds (top.removeFromLeft (220));
        top.removeFromLeft (6);
        addButton.setBounds (top.removeFromLeft (120));
        top.removeFromLeft (6);
        clearButton.setBounds (top.removeFromLeft (90));
        top.removeFromLeft (10);
        statusLabel.setBounds (top);

        b.removeFromTop (6);
        viewport.setBounds (b);

        const int knobW = 104, gap = 6;
        const int h = juce::jmax (40, b.getHeight() - (int) ((int) knobs.size() * (knobW + gap) > b.getWidth() ? 10 : 0));
        knobHolder.setSize (juce::jmax (b.getWidth(), (int) knobs.size() * (knobW + gap)), h);
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds ((int) i * (knobW + gap), 0, knobW, h);
    }
}
