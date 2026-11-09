#include "MappedControlsPanel.h"
#include "WavethingsLookAndFeel.h"
#include "../Core/Text.h"

namespace wt
{
    //==============================================================================
    /** Search field + filtered list of the plug-in parameters, shown in a call-out box. */
    class ParameterSearchList : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::TextEditor::Listener,
                                private juce::KeyListener
    {
    public:
        ParameterSearchList (const juce::StringArray& names, std::function<void (int)> onChosen)
            : allNames (names), chosen (std::move (onChosen))
        {
            search.setTextToShowWhenEmpty ("Buscar parámetro..."_u8, Colours::textDim);
            search.addListener (this);
            search.addKeyListener (this);
            addAndMakeVisible (search);

            list.setModel (this);
            list.setRowHeight (22);
            list.setColour (juce::ListBox::backgroundColourId, Colours::panel);
            addAndMakeVisible (list);

            countLabel.setFont (juce::FontOptions (11.0f));
            countLabel.setColour (juce::Label::textColourId, Colours::textDim);
            addAndMakeVisible (countLabel);

            filter();
            setSize (360, 420);
        }

        void visibilityChanged() override
        {
            if (isShowing())
                search.grabKeyboardFocus();
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (6);
            search.setBounds (b.removeFromTop (26));
            b.removeFromTop (4);
            countLabel.setBounds (b.removeFromBottom (16));
            list.setBounds (b);
        }

    private:
        static bool isHelperParameter (const juce::String& name) { return MidiLearn::isHelperParameterName (name); }

        void filter()
        {
            visible.clear();
            const auto words = juce::StringArray::fromTokens (search.getText().trim(), " ", "");

            for (int i = 0; i < allNames.size(); ++i)
            {
                if (isHelperParameter (allNames[i]))
                    continue;

                bool matches = true;
                for (auto& w : words)
                    matches = matches && (allNames[i].containsIgnoreCase (w) || juce::String (i + 1) == w);

                if (matches)
                    visible.push_back (i);
            }

            list.updateContent();
            list.selectRow (visible.empty() ? -1 : 0);
            countLabel.setText (juce::String ((int) visible.size()) + " parámetros"_u8, juce::dontSendNotification);
        }

        void choose (int row)
        {
            if (juce::isPositiveAndBelow (row, (int) visible.size()))
            {
                const int index = visible[(size_t) row];
                auto callback = chosen;
                if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                    box->dismiss();
                if (callback)
                    callback (index);
            }
        }

        int getNumRows() override { return (int) visible.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (! juce::isPositiveAndBelow (row, (int) visible.size()))
                return;
            const int index = visible[(size_t) row];
            if (selected)
            {
                g.setColour (Colours::accent.withAlpha (0.85f));
                g.fillRect (0, 0, width, height);
            }
            g.setColour (selected ? juce::Colours::black : Colours::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (index + 1), 4, 0, 36, height, juce::Justification::centredRight);
            g.setColour (selected ? juce::Colours::black : Colours::text);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (allNames[index], 48, 0, width - 52, height, juce::Justification::centredLeft);
        }

        void listBoxItemClicked (int row, const juce::MouseEvent&) override { choose (row); }
        void returnKeyPressed (int row) override { choose (row); }

        void textEditorTextChanged (juce::TextEditor&) override { filter(); }
        void textEditorReturnKeyPressed (juce::TextEditor&) override { choose (list.getSelectedRow()); }

        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            // arrow keys move through the list while typing
            const int row = list.getSelectedRow();
            if (key == juce::KeyPress::downKey) { list.selectRow (juce::jmin (row + 1, (int) visible.size() - 1)); return true; }
            if (key == juce::KeyPress::upKey)   { list.selectRow (juce::jmax (row - 1, 0)); return true; }
            return false;
        }

        juce::StringArray allNames;
        std::vector<int> visible;
        std::function<void (int)> chosen;
        juce::TextEditor search;
        juce::ListBox list;
        juce::Label countLabel;
    };

    //==============================================================================
    class MappedControlsPanel::Knob : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Knob (MidiLearn& ml, const MidiLearn::Mapping& m) : midiLearn (ml), mapping (m)
        {
            const bool isButton = mapping.type == MidiLearn::ControlType::button;

            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange (0.0, 1.0);
            slider.setPopupDisplayEnabled (true, true, nullptr);
            slider.textFromValueFunction = [this] (double v) { return valueText ((float) v); };
            slider.onDragStart = [this] { if (auto* p = param()) p->beginChangeGesture(); };
            slider.onDragEnd = [this] { if (auto* p = param()) p->endChangeGesture(); };
            slider.onValueChange = [this]
            {
                if (auto* p = param())
                    if (std::abs (p->getValue() - (float) slider.getValue()) > 1.0e-5f)
                        p->setValueNotifyingHost ((float) slider.getValue());
                valueLabel.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
            };
            addChildComponent (slider);
            slider.setVisible (! isButton);

            toggle.setClickingTogglesState (true);
            toggle.onClick = [this]
            {
                if (auto* p = param())
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (toggle.getToggleState() ? 1.0f : 0.0f);
                    p->endChangeGesture();
                }
                sync();
            };
            addChildComponent (toggle);
            toggle.setVisible (isButton);

            name.setText (mapping.parameterName, juce::dontSendNotification);
            name.setJustificationType (juce::Justification::centred);
            name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            name.setMinimumHorizontalScale (0.6f);
            name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (name);

            valueLabel.setJustificationType (juce::Justification::centred);
            valueLabel.setFont (juce::FontOptions (11.0f));
            valueLabel.setColour (juce::Label::textColourId, Colours::textDim);
            valueLabel.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (valueLabel);

            ccLabel.setText (ccText(), juce::dontSendNotification);
            ccLabel.setJustificationType (juce::Justification::centred);
            ccLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            ccLabel.setColour (juce::Label::textColourId, Colours::accent2);
            ccLabel.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (ccLabel);

            menuButton.setButtonText ("...");
            menuButton.setTooltip ("Control MIDI (CC y canal), perilla o botón, quitar del panel"_u8);
            menuButton.onClick = [this] { showMenu(); };
            addAndMakeVisible (menuButton);

            setTooltip ("Clic derecho o «...»: asignar un control MIDI, cambiar entre perilla y botón. Doble clic: aprender CC"_u8);
            sync();
        }

        juce::AudioProcessorParameter* param() const { return midiLearn.getParameter (mapping.parameterIndex); }

        juce::String ccText() const
        {
            juce::String cc = mapping.ccNumber < 0 ? juce::String ("sin CC")
                                                   : "CC " + juce::String (mapping.ccNumber) + (mapping.midiChannel > 0 ? " · ch "_u8 + juce::String (mapping.midiChannel) : juce::String());
            if (mapping.lockedForSampling)
                return "FIJO " + valueText (mapping.lockedValue) + (mapping.ccNumber >= 0 ? " · "_u8 + cc : juce::String());
            return cc;
        }

        bool isWaitingForCc() const
        {
            return midiLearn.isLearning() && midiLearn.getLearnParameter() == mapping.parameterIndex && midiLearn.getLearnCC() < 0;
        }

        juce::String valueText (float v) const
        {
            if (auto* p = param())
                return (p->getText (v, 32) + " " + p->getLabel()).trim();
            return juce::String (v, 2);
        }

        void sync()
        {
            if (auto* p = param())
            {
                const float v = p->getValue();
                if (! slider.isMouseButtonDown())
                    slider.setValue (v, juce::dontSendNotification);
                toggle.setToggleState (v >= 0.5f, juce::dontSendNotification);
                toggle.setButtonText (v >= 0.5f ? "ON" : "OFF");
                valueLabel.setText (valueText (v), juce::dontSendNotification);
            }

            // while waiting for a CC this control blinks
            const bool waiting = isWaitingForCc();
            const bool blink = waiting && (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
            ccLabel.setText (waiting ? "mueve un CC..." : ccText(), juce::dontSendNotification);
            ccLabel.setColour (juce::Label::textColourId, waiting ? (blink ? Colours::accent : Colours::text)
                                                                  : (mapping.lockedForSampling ? Colours::accent : Colours::accent2));
            if (waiting != wasWaiting) { wasWaiting = waiting; repaint(); }
        }

        void showMenu()
        {
            juce::PopupMenu menu;
            const bool isButton = mapping.type == MidiLearn::ControlType::button;
            menu.addSectionHeader (mapping.parameterName);
            menu.addItem (10, "Asignar control MIDI: mueve un control de tu teclado...");

            juce::PopupMenu ccMenu;
            for (int group = 0; group < 8; ++group)
            {
                juce::PopupMenu sub;
                for (int cc = group * 16; cc < group * 16 + 16; ++cc)
                    sub.addItem (100 + cc, "CC " + juce::String (cc), true, cc == mapping.ccNumber);
                ccMenu.addSubMenu ("CC " + juce::String (group * 16) + " - " + juce::String (group * 16 + 15), sub, true,
                                   juce::Image(), mapping.ccNumber >= group * 16 && mapping.ccNumber < group * 16 + 16);
            }
            menu.addSubMenu ("Elegir CC", ccMenu);

            juce::PopupMenu channelMenu;
            channelMenu.addItem (300, "Cualquier canal", true, mapping.midiChannel == 0);
            for (int ch = 1; ch <= 16; ++ch)
                channelMenu.addItem (300 + ch, "Canal " + juce::String (ch), true, mapping.midiChannel == ch);
            menu.addSubMenu ("Canal MIDI", channelMenu, mapping.ccNumber >= 0);
            menu.addItem (11, "Quitar el control MIDI", mapping.ccNumber >= 0);

            menu.addSeparator();
            const float current = param() != nullptr ? param()->getValue() : 0.0f;
            menu.addItem (20, "Fijar en 0 / apagado antes de samplear", true, mapping.lockedForSampling && mapping.lockedValue <= 0.0f);
            menu.addItem (21, "Fijar el valor actual (" + valueText (current) + ") antes de samplear", true,
                          mapping.lockedForSampling && mapping.lockedValue > 0.0f && std::abs (mapping.lockedValue - current) < 1.0e-4f);
            menu.addItem (22, "No fijar", mapping.lockedForSampling);

            menu.addSeparator();
            menu.addItem (1, "Mostrar como perilla", true, ! isButton);
            menu.addItem (2, "Mostrar como botón (on/off)"_u8, true, isButton);
            menu.addSeparator();
            menu.addItem (3, "Quitar del panel");

            auto& learn = midiLearn;
            const int index = mapping.parameterIndex;
            const int cc = mapping.ccNumber, channel = mapping.midiChannel;
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton), [&learn, index, cc, channel, current] (int result)
            {
                if (result == 20) learn.setLockedForSampling (index, true, 0.0f);
                if (result == 21) learn.setLockedForSampling (index, true, current);
                if (result == 22) learn.setLockedForSampling (index, false, 0.0f);
                if (result == 1) learn.setControlType (index, MidiLearn::ControlType::knob);
                if (result == 2) learn.setControlType (index, MidiLearn::ControlType::button);
                if (result == 3) learn.removeMapping (index);
                if (result == 10) learn.learnCcFor (index);
                if (result == 11) learn.setCc (index, -1, 0);
                if (result >= 100 && result < 228) learn.setCc (index, result - 100, channel);
                if (result >= 300 && result <= 316) learn.setCc (index, cc, result - 300);
            });
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                showMenu();
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            midiLearn.learnCcFor (mapping.parameterIndex);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Colours::panelLight);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            if (isWaitingForCc())
            {
                g.setColour (Colours::accent);
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.5f);
            }
            else if (mapping.lockedForSampling)
            {
                // fixed before sampling: orange strip at the bottom
                g.setColour (Colours::accent);
                g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromBottom (3.0f).reduced (6.0f, 0.0f), 1.5f);
            }
        }

        bool wasWaiting = false;

        void resized() override
        {
            auto b = getLocalBounds().reduced (4, 3);
            auto top = b.removeFromTop (14);
            menuButton.setBounds (top.removeFromRight (20));
            ccLabel.setBounds (top);
            name.setBounds (b.removeFromBottom (14));
            valueLabel.setBounds (b.removeFromBottom (13));
            slider.setBounds (b);
            toggle.setBounds (b.withSizeKeepingCentre (juce::jmin (b.getWidth() - 8, 70), juce::jmin (b.getHeight() - 4, 34)));
        }

        MidiLearn& midiLearn;
        MidiLearn::Mapping mapping;
        juce::Slider slider;
        juce::TextButton toggle;
        juce::Label name, valueLabel, ccLabel;
        juce::TextButton menuButton;
    };

    //==============================================================================
    MappedControlsPanel::MappedControlsPanel (MidiLearn& ml) : midiLearn (ml)
    {
        captureButton.setClickingTogglesState (true);
        captureButton.setTooltip ("Abre la interfaz del plugin: cada control que muevas en ella se añade al panel. "_u8
                                  "Pulsa otra vez para terminar. Después, en «...» de cada control, eliges perilla o botón y su control MIDI"_u8);
        captureButton.onClick = [this]
        {
            const bool on = captureButton.getToggleState();
            midiLearn.setCapturing (on);
            if (on && onOpenEditorRequested)
                onOpenEditorRequested();
        };
        addAndMakeVisible (captureButton);

        learnButton.setClickingTogglesState (true);
        learnButton.setTooltip ("Activa MIDI Learn, mueve un control del plugin (o búscalo en la lista) y después un control de tu teclado MIDI"_u8);
        learnButton.onClick = [this]
        {
            midiLearn.setLearning (learnButton.getToggleState());
            if (learnButton.getToggleState() && selectedParameter >= 0)
                midiLearn.setLearnParameter (selectedParameter);
        };
        addAndMakeVisible (learnButton);

        parameterButton.setButtonText ("Buscar parámetro..."_u8);
        parameterButton.setTooltip ("Busca un parámetro del plugin por su nombre"_u8);
        parameterButton.onClick = [this] { showParameterSearch(); };
        addAndMakeVisible (parameterButton);

        typeBox.addItem ("Tipo: automático"_u8, 1);
        typeBox.addItem ("Tipo: perilla", 2);
        typeBox.addItem ("Tipo: botón"_u8, 3);
        typeBox.setSelectedId (1, juce::dontSendNotification);
        typeBox.setTooltip ("Cómo se muestran los controles nuevos. Automático: botón para parámetros on/off, perilla para el resto"_u8);
        typeBox.onChange = [this] { midiLearn.setLearnControlType (typeBox.getSelectedId() - 2); };
        addAndMakeVisible (typeBox);

        addButton.setButtonText ("Añadir"_u8);
        addButton.setTooltip ("Añade el parámetro elegido sin asignarle un CC"_u8);
        addButton.onClick = [this]
        {
            if (selectedParameter < 0)
            {
                showParameterSearch();
                return;
            }
            int cc = -1;   // keep an existing CC assignment
            for (auto& m : midiLearn.getMappings())
                if (m.parameterIndex == selectedParameter) cc = m.ccNumber;
            midiLearn.addMapping (selectedParameter, cc, 0, typeBox.getSelectedId() - 2);
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

    void MappedControlsPanel::showParameterSearch()
    {
        const auto names = midiLearn.getParameterNames();
        if (names.isEmpty())
            return;

        juce::Component::SafePointer<MappedControlsPanel> self (this);
        auto content = std::make_unique<ParameterSearchList> (names, [self, names] (int index)
        {
            if (self == nullptr)
                return;
            self->selectedParameter = index;
            self->parameterButton.setButtonText (juce::String (index + 1) + ". " + names[index]);
            if (self->midiLearn.isLearning())
                self->midiLearn.setLearnParameter (index);
        });

        juce::CallOutBox::launchAsynchronously (std::move (content), parameterButton.getScreenBounds(), nullptr);
    }

    void MappedControlsPanel::refresh()
    {
        const auto names = midiLearn.getParameterNames();
        if (names != cachedParameterNames)
        {
            cachedParameterNames = names;
            selectedParameter = -1;
            parameterButton.setButtonText (names.isEmpty() ? "Carga un plugin"_u8 : "Buscar parámetro..."_u8);
        }
        parameterButton.setEnabled (! names.isEmpty());
        captureButton.setEnabled (! names.isEmpty());
        captureButton.setToggleState (midiLearn.isCapturing(), juce::dontSendNotification);

        knobs.clear();
        for (auto& m : midiLearn.getMappings())
        {
            auto knob = std::make_unique<Knob> (midiLearn, m);
            knobHolder.addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        }

        learnButton.setToggleState (midiLearn.isLearning(), juce::dontSendNotification);
        updateStatus();
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

        if (midiLearn.isLearning() || midiLearn.isCapturing())
            updateStatus();
    }

    void MappedControlsPanel::paint (juce::Graphics&) {}

    void MappedControlsPanel::updateStatus()
    {
        juce::String text;
        if (midiLearn.isCapturing())
        {
            const auto last = midiLearn.getLastCapturedName();
            text = "Aprendiendo del plugin: mueve los controles que quieras en su interfaz"_u8
                   + (last.isNotEmpty() ? "  ·  añadido: "_u8 + last : juce::String())
                   + "  ·  pulsa «Aprender del plugin» para terminar"_u8;
        }
        else if (midiLearn.isLearning())
            text = midiLearn.getLearnStatusText();
        else if (knobs.empty())
            text = "Pulsa «Aprender del plugin» y mueve controles en su interfaz, o asígnalos con MIDI Learn"_u8;
        else
        {
            const int locked = (int) midiLearn.getLockedValues().size();
            text = juce::String (knobs.size()) + " controles  ·  «...»: control MIDI, perilla o botón, fijar al samplear"_u8
                   + (locked > 0 ? "  ·  "_u8 + juce::String (locked) + (locked == 1 ? " fijo antes de samplear" : " fijos antes de samplear")
                                 : juce::String ("  ·  consejo: fija el Delay en 0"_u8));
        }
        statusLabel.setText (text, juce::dontSendNotification);
    }

    void MappedControlsPanel::resized()
    {
        auto b = getLocalBounds();
        auto top = b.removeFromTop (26);
        captureButton.setBounds (top.removeFromLeft (140));
        top.removeFromLeft (6);
        learnButton.setBounds (top.removeFromLeft (92));
        top.removeFromLeft (6);
        clearButton.setBounds (top.removeFromRight (86));
        top.removeFromRight (6);
        addButton.setBounds (top.removeFromRight (70));
        top.removeFromRight (6);
        typeBox.setBounds (top.removeFromRight (juce::jmin (140, juce::jmax (100, top.getWidth() / 3))));
        top.removeFromRight (6);
        parameterButton.setBounds (top);

        b.removeFromTop (2);
        statusLabel.setBounds (b.removeFromTop (18));
        b.removeFromTop (2);
        viewport.setBounds (b);

        const int knobW = 104, gap = 6;
        const int h = juce::jmax (40, b.getHeight() - (int) ((int) knobs.size() * (knobW + gap) > b.getWidth() ? 10 : 0));
        knobHolder.setSize (juce::jmax (b.getWidth(), (int) knobs.size() * (knobW + gap)), h);
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds ((int) i * (knobW + gap), 0, knobW, h);
    }
}
