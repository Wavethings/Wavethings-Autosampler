#include "MainComponent.h"
#include "Core/Text.h"
#include "Core/NoteUtils.h"
#include "Export/Exporter.h"
#include "Export/Exporters.h"
#include "Core/PluginScanner.h"

namespace wt
{
    //==============================================================================
    void RangeKeyboard::setHighlight (std::vector<int> activeKeys, std::vector<int> r)
    {
        active.fill (false);
        for (int n : activeKeys)
            if (n >= 0 && n < 128)
                active[(size_t) n] = true;
        roots = std::move (r);
        repaint();
    }

    //==============================================================================
    juce::Range<float> RangeBar::getNoteSpan (int note) const
    {
        auto centre = [this] (int n) { return keyboard.getRectangleForKey (juce::jlimit (21, 108, n)).getCentreX(); };
        const float c = centre (note);
        const float left = note <= 21 ? keyboard.getRectangleForKey (21).getX() : (centre (note - 1) + c) * 0.5f;
        const float right = note >= 108 ? keyboard.getRectangleForKey (108).getRight() : (centre (note + 1) + c) * 0.5f;
        return { left, right };
    }

    int RangeBar::noteAtX (float x) const
    {
        for (int n = 21; n <= 108; ++n)
            if (x < getNoteSpan (n).getEnd())
                return n;
        return 108;
    }

    juce::Rectangle<float> RangeBar::getBarArea() const
    {
        const auto lowRect = keyboard.getRectangleForKey (juce::jlimit (21, 108, lowNote));
        const auto highRect = keyboard.getRectangleForKey (juce::jlimit (21, 108, highNote));
        return { lowRect.getX(), 2.0f, juce::jmax (6.0f, highRect.getRight() - lowRect.getX()), (float) getHeight() - 4.0f };
    }

    RangeBar::DragMode RangeBar::hitTestMode (float x) const
    {
        const auto bar = getBarArea();
        const float grab = juce::jmin (10.0f, bar.getWidth() / 3.0f);
        if (std::abs (x - bar.getX()) <= grab) return DragMode::low;
        if (std::abs (x - bar.getRight()) <= grab) return DragMode::high;
        if (bar.getHorizontalRange().contains (x)) return DragMode::move;
        return x < bar.getX() ? DragMode::low : DragMode::high;
    }

    void RangeBar::paint (juce::Graphics& g)
    {
        auto track = getLocalBounds().toFloat().reduced (0.0f, 5.0f);
        g.setColour (Colours::panelLight);
        g.fillRoundedRectangle (track, 3.0f);

        if (listMode)
        {
            // One Shot: just the notes that will be recorded
            g.setColour (Colours::accent);
            for (auto n : markers)
            {
                const auto r = keyboard.getRectangleForKey (juce::jlimit (21, 108, n));
                g.fillRoundedRectangle (r.getCentreX() - 3.0f, 3.0f, 6.0f, (float) getHeight() - 6.0f, 2.0f);
            }
            return;
        }

        const auto bar = getBarArea();
        const bool over = isMouseOverOrDragging();
        g.setColour (Colours::accent.withAlpha (isEnabled() ? (over ? 0.95f : 0.8f) : 0.35f));
        g.fillRoundedRectangle (bar, 4.0f);

        // the notes that will be recorded, as small marks
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        for (auto n : markers)
        {
            const auto r = keyboard.getRectangleForKey (juce::jlimit (21, 108, n));
            g.fillRect (r.getCentreX() - 1.0f, bar.getY() + 4.0f, 2.0f, bar.getHeight() - 8.0f);
        }

        // grips at both ends
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        for (float x : { bar.getX() + 4.0f, bar.getRight() - 6.0f })
        {
            g.fillRect (x, bar.getCentreY() - 4.0f, 1.0f, 8.0f);
            g.fillRect (x + 2.0f, bar.getCentreY() - 4.0f, 1.0f, 8.0f);
        }

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        const auto text = NoteUtils::getNoteName (lowNote, 3).trim() + "  -  " + NoteUtils::getNoteName (highNote, 3).trim();
        if (bar.getWidth() > 80.0f)
        {
            const auto pill = bar.withSizeKeepingCentre (juce::jmin (bar.getWidth() - 16.0f, 74.0f), bar.getHeight());
            g.setColour (Colours::accent);
            g.fillRect (pill);
            g.setColour (juce::Colours::black);
            g.drawText (text, pill.toNearestInt(), juce::Justification::centred);
        }
        else
        {
            g.setColour (Colours::text);
            const bool roomRight = bar.getRight() + 90.0f < (float) getWidth();
            g.drawText (text, roomRight ? juce::Rectangle<float> (bar.getRight() + 6.0f, 0.0f, 90.0f, (float) getHeight()).toNearestInt()
                                        : juce::Rectangle<float> (bar.getX() - 96.0f, 0.0f, 90.0f, (float) getHeight()).toNearestInt(),
                        roomRight ? juce::Justification::centredLeft : juce::Justification::centredRight);
        }
    }

    void RangeBar::mouseMove (const juce::MouseEvent& e)
    {
        if (listMode) return;
        const auto mode = hitTestMode (e.position.x);
        setMouseCursor (mode == DragMode::move ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor);
    }

    void RangeBar::mouseDown (const juce::MouseEvent& e)
    {
        if (listMode || ! isEnabled()) return;
        dragMode = hitTestMode (e.position.x);
        dragStartLow = lowNote;
        dragStartHigh = highNote;
        dragStartNote = noteAtX (e.position.x);
        if (dragMode != DragMode::move)
            mouseDrag (e);   // a click outside the bar moves the nearest end there
    }

    void RangeBar::mouseDrag (const juce::MouseEvent& e)
    {
        if (listMode || dragMode == DragMode::none) return;
        const int note = noteAtX (e.position.x);
        int low = lowNote, high = highNote;

        if (dragMode == DragMode::low)       low = juce::jmin (note, highNote);
        else if (dragMode == DragMode::high) high = juce::jmax (note, lowNote);
        else
        {
            const int delta = juce::jlimit (21 - dragStartLow, 108 - dragStartHigh, note - dragStartNote);
            low = dragStartLow + delta;
            high = dragStartHigh + delta;
        }

        if (low != lowNote || high != highNote)
        {
            setRange (low, high);
            if (onRangeChanged) onRangeChanged (low, high);
        }
    }

    void RangeBar::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (listMode || ! isEnabled()) return;
        setRange (24, 96);
        if (onRangeChanged) onRangeChanged (24, 96);
    }

    //==============================================================================
    void RangeKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                       juce::Colour lineColour, juce::Colour textColour)
    {
        const bool inRange = isActive (note);
        g.setColour (inRange ? juce::Colour (0xfff2e6da) : juce::Colour (0xffb9bcc2));
        g.fillRect (area);

        if (isRoot (note))
        {
            g.setColour (Colours::accent.withAlpha (0.85f));
            g.fillRect (area.withTop (area.getBottom() - 8.0f).reduced (2.0f, 1.0f));
        }
        if (note == recordingNote)
        {
            g.setColour (Colours::danger.withAlpha (0.7f));
            g.fillRect (area);
        }
        if (isDown)
        {
            g.setColour (findColour (keyDownOverlayColourId));
            g.fillRect (area);
        }
        else if (isOver)
        {
            g.setColour (findColour (mouseOverKeyOverlayColourId));
            g.fillRect (area);
        }

        g.setColour (lineColour);
        g.fillRect (area.withWidth (1.0f));

        if (note % 12 == 0)
        {
            g.setColour (textColour);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (NoteUtils::getNoteName (note, 3), area.withTrimmedBottom (9.0f), juce::Justification::centredBottom);
        }
    }

    void RangeKeyboard::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                       juce::Colour)
    {
        const bool inRange = isActive (note);
        g.setColour (inRange ? juce::Colour (0xff2a2622) : juce::Colour (0xff4a4d52));
        g.fillRect (area);

        if (isRoot (note))
        {
            g.setColour (Colours::accent);
            g.fillRect (area.withTop (area.getBottom() - 6.0f).reduced (2.0f, 1.0f));
        }
        if (note == recordingNote)
        {
            g.setColour (Colours::danger.withAlpha (0.8f));
            g.fillRect (area);
        }
        if (isDown)
        {
            g.setColour (findColour (keyDownOverlayColourId));
            g.fillRect (area);
        }
        else if (isOver)
        {
            g.setColour (findColour (mouseOverKeyOverlayColourId));
            g.fillRect (area);
        }
    }

    //==============================================================================
    namespace
    {
        void setupLabel (juce::Component& parent, juce::Label& label, const juce::String& text, float size = 12.5f)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::FontOptions (size));
            label.setColour (juce::Label::textColourId, Colours::textDim);
            parent.addAndMakeVisible (label);
        }

        void setupSlider (juce::Component& parent, juce::Slider& slider, double min, double max, double step,
                          const juce::String& suffix = {})
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
            slider.setRange (min, max, step);
            if (suffix.isNotEmpty())
                slider.setTextValueSuffix (suffix);
            parent.addAndMakeVisible (slider);
        }

        void setupNoteSlider (juce::Component& parent, juce::Slider& slider)
        {
            slider.setSliderStyle (juce::Slider::IncDecButtons);
            slider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
            slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 22);
            slider.setRange (0, 127, 1);
            slider.textFromValueFunction = [] (double v) { return NoteUtils::getNoteName ((int) v, 3); };
            slider.valueFromTextFunction = [] (const juce::String& t)
            {
                for (int n = 0; n < 128; ++n)
                    if (NoteUtils::getNoteName (n, 3).equalsIgnoreCase (t.trim()))
                        return (double) n;
                return t.getDoubleValue();
            };
            parent.addAndMakeVisible (slider);
        }

        void row (juce::Rectangle<int>& area, juce::Component& label, juce::Component& control, int labelWidth, int h = 24)
        {
            auto r = area.removeFromTop (h);
            label.setBounds (r.removeFromLeft (labelWidth));
            control.setBounds (r);
            area.removeFromTop (4);
        }
    }

    //==============================================================================
    MainComponent::MainComponent (juce::ApplicationProperties& props) : properties (props)
    {
        setLookAndFeel (&lookAndFeel);
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        auto* settingsFile = properties.getUserSettings();

        // Audio device + MIDI ----------------------------------------------------------
        std::unique_ptr<juce::XmlElement> deviceState (settingsFile->getXmlValue ("audioDeviceState"));
        const auto error = engine.initialiseDevices (deviceState.get());
        engine.onMidiMessage = [this] (const juce::MidiMessage& m) { handleMidi (m); };

        // Known plug-ins
        if (auto xml = settingsFile->getXmlValue ("pluginList"))
            engine.getKnownPluginList().recreateFromXml (*xml);
        engine.getKnownPluginList().addChangeListener (this);
        PluginScanner::install (engine.getKnownPluginList());

        createPluginSection();
        createRangeSection();
        createThresholdSection();
        createLoopSection();
        createOutputSection();
        createBatchSection();
        engine.getDeviceManager().addChangeListener (this);

        mappedControls = std::make_unique<MappedControlsPanel> (engine.getMidiLearn());
        mappedControls->onMappingsChanged = [this] { saveMappings(); };
        mappedControls->onOpenEditorRequested = [this] { showPluginEditor(); };

        hardwareControls.sendCc = [this] (int channel, int cc, int value)
        {
            engine.queueMidi (juce::MidiMessage::controllerEvent (channel > 0 ? channel : engine.getMidiChannel(), cc, value));
        };
        hardwareControls.onChanged = [this] { saveHardwareControls(); };
        controlsSection.addChildComponent (hardwareControls);
        addAndMakeVisible (controlsSection);
        controlsSection.addAndMakeVisible (*mappedControls);

        keyboard.setAvailableRange (21, 108);
        keyboard.setScrollButtonsVisible (false);
        keyboard.onKeyClicked = [this] (int note) { if (pickingNotes && useNoteList) toggleListNote (note); };
        addAndMakeVisible (keyboard);

        rangeBar.onRangeChanged = [this] (int low, int high)
        {
            lowSlider.setValue (low, juce::dontSendNotification);
            highSlider.setValue (high, juce::dontSendNotification);
            updateDerivedDisplays();
        };
        addAndMakeVisible (rangeBar);
        rangeLeftButton.setTooltip ("Mueve el rango (o las notas One Shot) un semitono abajo. Mayús: una octava"_u8);
        rangeRightButton.setTooltip ("Mueve el rango (o las notas One Shot) un semitono arriba. Mayús: una octava"_u8);
        rangeLeftButton.onClick = [this] { shiftRange (juce::ModifierKeys::currentModifiers.isShiftDown() ? -12 : -1); };
        rangeRightButton.onClick = [this] { shiftRange (juce::ModifierKeys::currentModifiers.isShiftDown() ? 12 : 1); };
        rangeLeftButton.setRepeatSpeed (400, 90);
        rangeRightButton.setRepeatSpeed (400, 90);
        addAndMakeVisible (rangeLeftButton);
        addAndMakeVisible (rangeRightButton);

        // Sampling presets (top bar)
        presetLabel.setText ("Preset de sampleo", juce::dontSendNotification);
        presetLabel.setColour (juce::Label::textColourId, Colours::textDim);
        presetLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (presetLabel);
        presetBox.setTextWhenNothingSelected ("Elige un tipo de sonido...");
        presetBox.setTooltip ("Carga rango, paso, capas, tiempos y loop típicos para ese tipo de sonido. No cambia el plugin, el umbral ni la exportación"_u8);
        presetBox.onChange = [this] { presetSelected(); };
        addAndMakeVisible (presetBox);
        presetMenuButton.setButtonText ("Guardar / borrar");
        presetMenuButton.setTooltip ("Guarda los ajustes actuales como preset propio, o borra uno tuyo");
        presetMenuButton.onClick = [this] { showPresetMenu(); };
        addAndMakeVisible (presetMenuButton);
        builtInPresets = getBuiltInSamplingPresets();
        updatePresetBox();

        createTransport();

        // Restore settings
        SamplingSettings s;
        if (auto xml = settingsFile->getXmlValue ("samplingSettings"))
            s.fromXml (*xml);
        applySettings (s);

        updatePluginList();
        if (error.isNotEmpty())
            appendLog ("Audio: " + error);

        appendLog ("Wavethings Autosampler " WT_VERSION_STRING " listo. Carga un instrumento, define el rango y pulsa Empezar."_u8);

        // Re-open the last plug-in, unless it was being loaded when the app closed unexpectedly
        const auto crashed = settingsFile->getValue ("pluginLoadInProgress");
        const auto last = settingsFile->getValue ("lastPlugin");
        if (crashed.isNotEmpty())
        {
            settingsFile->removeValue ("pluginLoadInProgress");
            settingsFile->setValue ("lastPlugin", juce::String());
            settingsFile->saveIfNeeded();
            appendLog ("La app se cerró mientras cargaba un plugin; esta vez no se vuelve a abrir automáticamente."_u8);
        }
        else if (last.isNotEmpty())
        {
            if (auto desc = engine.getKnownPluginList().getTypeForIdentifierString (last))
                loadPluginDescription (*desc);
        }

        scaleBox.setTooltip ("Tamaño de la interfaz (también con Cmd/Ctrl + y Cmd/Ctrl -)"_u8);
        for (int percent : { 75, 90, 100, 110, 125, 150, 175, 200 })
            scaleBox.addItem ("Tamaño "_u8 + juce::String (percent) + " %", percent);
        scaleBox.onChange = [this] { setUiScale ((float) scaleBox.getSelectedId() / 100.0f); };
        addAndMakeVisible (scaleBox);
        uiScale = juce::jlimit (0.75f, 2.0f, (float) settingsFile->getDoubleValue ("uiScale", 1.0));
        scaleBox.setSelectedId (juce::roundToInt (uiScale * 100.0f), juce::dontSendNotification);
        setWantsKeyboardFocus (true);

        setSize (baseWidth, baseHeight);
        startTimerHz (4);
    }

    MainComponent::~MainComponent()
    {
        if (job != nullptr)
        {
            job->onFinished = nullptr;
            job->onLog = nullptr;
            job->onProgress = nullptr;
            job->onSampleCaptured = nullptr;
            job->stopThread (5000);
            job.reset();
        }

        saveState();
        engine.getKnownPluginList().removeChangeListener (this);
        engine.getDeviceManager().removeChangeListener (this);
        engine.onMidiMessage = nullptr;
        pluginWindow.reset();
        scannerWindow.reset();
        mappedControls.reset();
        engine.shutdownDevices();
        engine.unloadPlugin();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    //==============================================================================
    void MainComponent::createPluginSection()
    {
        addAndMakeVisible (pluginSection);

        setupLabel (pluginSection, pluginLabel, "Plugin");
        pluginBox.setTextWhenNothingSelected ("Elige un instrumento...");
        pluginBox.setTextWhenNoChoicesAvailable ("No hay plugins: pulsa \"Escanear plugins\"");
        pluginBox.onChange = [this] { loadSelectedPlugin(); };
        pluginSection.addAndMakeVisible (pluginBox);

        formatFilterBox.setTooltip ("Muestra solo los plugins de un tipo (VST3, AU, LV2...)");
        formatFilterBox.onChange = [this]
        {
            const int index = formatFilterBox.getSelectedId() - 2;
            juce::StringArray formats;
            for (auto& d : engine.getKnownPluginList().getTypes())
                formats.addIfNotAlreadyThere (d.pluginFormatName);
            formats.sort (true);
            properties.getUserSettings()->setValue ("pluginFormatFilter", juce::isPositiveAndBelow (index, formats.size()) ? formats[index] : juce::String());
            updatePluginList();
        };
        pluginSection.addAndMakeVisible (formatFilterBox);

        favouriteButton.onClick = [this] { toggleFavourite(); };
        pluginSection.addAndMakeVisible (favouriteButton);

        scanButton.setButtonText ("Escanear...");
        scanButton.setTooltip ("Busca plugins VST3 / AU instalados (cada uno se analiza en un proceso aparte)");
        scanButton.onClick = [this] { showPluginScanner(); };
        pluginSection.addAndMakeVisible (scanButton);

        editorButton.setButtonText ("Interfaz del plugin");
        editorButton.onClick = [this] { showPluginEditor(); };
        pluginSection.addAndMakeVisible (editorButton);

        unloadButton.setButtonText ("Descargar");
        unloadButton.onClick = [this]
        {
            pluginWindow.reset();
            engine.unloadPlugin();
            pluginBox.setSelectedId (0, juce::dontSendNotification);
            properties.getUserSettings()->setValue ("lastPlugin", juce::String());
            updateProgramList();
            mappedControls->refresh();
            updateFavouriteButton();
        };
        pluginSection.addAndMakeVisible (unloadButton);

        setupLabel (pluginSection, programLabel, "Programa");
        programBox.setTextWhenNoChoicesAvailable ("(sin programas)");
        programBox.onChange = [this]
        {
            if (auto* p = engine.getPlugin())
                if (programBox.getSelectedItemIndex() >= 0)
                    p->setCurrentProgram (programBox.getSelectedItemIndex());
        };
        pluginSection.addAndMakeVisible (programBox);

        audioButton.setButtonText ("Audio / MIDI...");
        audioButton.onClick = [this] { showAudioSettings(); };
        pluginSection.addAndMakeVisible (audioButton);

        // Source: plug-in or hardware
        sourcePluginButton.setButtonText ("Plugin");
        sourceHardwareButton.setButtonText ("Hardware");
        sourcePluginButton.setTooltip ("Samplea un instrumento VST3 / AU cargado en la app");
        sourceHardwareButton.setTooltip ("Samplea un sinte o módulo externo: las notas salen por MIDI y se graba la entrada de audio"_u8);
        for (auto* b : { &sourcePluginButton, &sourceHardwareButton })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (4713);
            b->onClick = [this] { updateSourceControls(); applyHardwareSettingsToEngine(); updateBatchControls(); updateDerivedDisplays(); };
            pluginSection.addAndMakeVisible (*b);
        }
        sourcePluginButton.setToggleState (true, juce::dontSendNotification);

        setupLabel (pluginSection, midiOutLabel, "Salida MIDI");
        midiOutBox.setTextWhenNothingSelected ("Elige la salida MIDI...");
        midiOutBox.setTextWhenNoChoicesAvailable ("No hay salidas MIDI");
        midiOutBox.onChange = [this]
        {
            if (midiOutBox.getSelectedId() == 9999) { updateMidiOutputList(); return; }
            applyHardwareSettingsToEngine();
            loadHardwareControls();
            settingsDirty = true;
        };
        pluginSection.addAndMakeVisible (midiOutBox);

        setupLabel (pluginSection, channelLabel, "Canal");
        for (int ch = 1; ch <= 16; ++ch)
            channelBox.addItem (juce::String (ch), ch);
        channelBox.setSelectedId (1, juce::dontSendNotification);
        channelBox.setTooltip ("Canal MIDI de las notas y de los cambios de programa. Tu teclado MIDI también toca el instrumento por este canal");
        channelBox.onChange = [this] { applyHardwareSettingsToEngine(); settingsDirty = true; };
        pluginSection.addAndMakeVisible (channelBox);

        setupLabel (pluginSection, inputLabel, "Entrada");
        inputBox.setTextWhenNoChoicesAvailable ("Sin entradas de audio");
        inputBox.setTooltip ("Entradas de la interfaz de audio donde está conectado el instrumento");
        inputBox.onChange = [this] { applyHardwareSettingsToEngine(); settingsDirty = true; };
        pluginSection.addAndMakeVisible (inputBox);

        monitorToggle.setButtonText ("Escuchar entrada");
        monitorToggle.setTooltip ("Envía la entrada a la salida de la app. Desactívalo si ya oyes el instrumento por la mesa o por la interfaz (monitorización directa)");
        monitorToggle.onClick = [this] { applyHardwareSettingsToEngine(); settingsDirty = true; };
        pluginSection.addAndMakeVisible (monitorToggle);

        setupLabel (pluginSection, latencyLabel, "Latencia +");
        latencySlider.setSliderStyle (juce::Slider::IncDecButtons);
        latencySlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        latencySlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 22);
        latencySlider.setRange (-50.0, 200.0, 0.5);
        latencySlider.setTextValueSuffix (" ms");
        latencySlider.setTooltip ("Latencia extra a compensar, además de la que indica la interfaz de audio (MIDI, el propio sinte...). "
                                  "Si las muestras empiezan con un trozo de silencio, súbela; si se come el ataque, bájala"_u8);
        latencySlider.onValueChange = [this] { applyHardwareSettingsToEngine(); settingsDirty = true; };
        pluginSection.addAndMakeVisible (latencySlider);

        updateMidiOutputList();
        updateInputList();
    }

    void MainComponent::createBatchSection()
    {
        addAndMakeVisible (batchSection);
        auto& p = batchSection;

        batchToggle.setButtonText ("Samplear varios programas");
        batchToggle.setTooltip ("Crea un instrumento por cada programa (y banco) del rango, uno detrás de otro, cada uno en su carpeta");
        batchToggle.onClick = [this] { updateBatchControls(); updateDerivedDisplays(); };
        p.addAndMakeVisible (batchToggle);

        methodBox.addItem ("MIDI Program Change", 1);
        methodBox.addItem ("Programas del plugin", 2);
        methodBox.setSelectedId (1, juce::dontSendNotification);
        methodBox.setTooltip ("Program Change: funciona con hardware y con plugins que lo admiten. "
                              "Programas del plugin: la lista de programas que muestra el plugin (con sus nombres)");
        methodBox.onChange = [this] { updateBatchControls(); updateDerivedDisplays(); };
        p.addAndMakeVisible (methodBox);

        auto setupNumber = [&p] (juce::Slider& sl, double lo, double hi, bool oneBased)
        {
            sl.setSliderStyle (juce::Slider::IncDecButtons);
            sl.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
            sl.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 22);
            sl.setRange (lo, hi, 1.0);
            if (oneBased)
            {
                sl.textFromValueFunction = [] (double v) { return juce::String ((int) v + 1); };
                sl.valueFromTextFunction = [] (const juce::String& t) { return (double) (t.getIntValue() - 1); };
            }
            sl.updateText();
            p.addAndMakeVisible (sl);
        };

        setupLabel (p, programLabelB, "Programa");
        setupLabel (p, programToLabel, "a");
        programToLabel.setJustificationType (juce::Justification::centred);
        setupNumber (programFromSlider, 0, 127, true);
        setupNumber (programToSlider, 0, 127, true);
        programFromSlider.setTooltip ("Primer programa (1-128)");
        programToSlider.setTooltip ("Último programa (1-128)"_u8);
        programFromSlider.onValueChange = [this] { if (programToSlider.getValue() < programFromSlider.getValue()) programToSlider.setValue (programFromSlider.getValue()); updateDerivedDisplays(); };
        programToSlider.onValueChange = [this] { if (programFromSlider.getValue() > programToSlider.getValue()) programFromSlider.setValue (programToSlider.getValue()); updateDerivedDisplays(); };

        setupLabel (p, waitLabel, "Espera");
        waitSlider.setSliderStyle (juce::Slider::IncDecButtons);
        waitSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        waitSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 22);
        waitSlider.setRange (0.0, 10000.0, 50.0);
        waitSlider.setTextValueSuffix (" ms");
        waitSlider.setTooltip ("Tiempo tras cada cambio de programa para que el instrumento cargue el sonido");
        waitSlider.onValueChange = [this] { updateDerivedDisplays(); };
        p.addAndMakeVisible (waitSlider);

        bankModeBox.addItem ("Sin banco", 1);
        bankModeBox.addItem ("Banco CC0 (MSB)", 2);
        bankModeBox.addItem ("Banco CC32 (LSB)", 3);
        bankModeBox.addItem ("Banco CC0 + CC32", 4);
        bankModeBox.setSelectedId (1, juce::dontSendNotification);
        bankModeBox.setTooltip ("Bank Select que se envía antes de cada Program Change. Cada fabricante usa uno: mira el manual de tu sinte (implementación MIDI)");
        bankModeBox.onChange = [this] { updateBatchControls(); updateDerivedDisplays(); };
        p.addAndMakeVisible (bankModeBox);

        setupLabel (p, msbLabel, "MSB");
        setupLabel (p, msbToLabel, "a");
        setupLabel (p, lsbLabel, "LSB");
        setupLabel (p, lsbToLabel, "a");
        msbToLabel.setJustificationType (juce::Justification::centred);
        lsbToLabel.setJustificationType (juce::Justification::centred);
        for (auto* sl : { &msbFromSlider, &msbToSlider, &lsbFromSlider, &lsbToSlider })
        {
            setupNumber (*sl, 0, 127, false);
            sl->onValueChange = [this] { updateDerivedDisplays(); };
        }

        sendNowButton.setButtonText ("Enviar ahora");
        sendNowButton.setTooltip ("Envía el banco y el primer programa para escuchar cómo suena antes de empezar");
        sendNowButton.onClick = [this] { sendProgramNow(); };
        p.addAndMakeVisible (sendNowButton);

        namesButton.setButtonText ("Nombres...");
        namesButton.setTooltip ("Escribe los nombres de los programas, uno por línea, en el orden del lote. Si no, se usa el nombre del instrumento + banco y programa (o el nombre del programa del plugin)");
        namesButton.onClick = [this] { editProgramNames(); };
        p.addAndMakeVisible (namesButton);

        skipSilentToggle.setButtonText ("Omitir programas mudos");
        skipSilentToggle.setTooltip ("Si las primeras muestras de un programa no suenan, se salta el programa entero");
        skipSilentToggle.setToggleState (true, juce::dontSendNotification);
        skipSilentToggle.onClick = [this] { settingsDirty = true; };
        p.addAndMakeVisible (skipSilentToggle);

        batchInfo.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        batchInfo.setColour (juce::Label::textColourId, Colours::accent);
        p.addAndMakeVisible (batchInfo);
    }

    //==============================================================================
    void MainComponent::updateSourceControls()
    {
        const bool hw = isHardwareMode();
        for (auto* c : { (juce::Component*) &pluginBox, (juce::Component*) &scanButton, (juce::Component*) &editorButton,
                         (juce::Component*) &formatFilterBox, (juce::Component*) &favouriteButton,
                         (juce::Component*) &unloadButton, (juce::Component*) &programBox, (juce::Component*) &programLabel })
            c->setVisible (! hw);
        for (auto* c : { (juce::Component*) &midiOutLabel, (juce::Component*) &midiOutBox, (juce::Component*) &inputLabel,
                         (juce::Component*) &inputBox, (juce::Component*) &monitorToggle, (juce::Component*) &latencyLabel,
                         (juce::Component*) &latencySlider })
            c->setVisible (hw);
        pluginLabel.setVisible (false);

        // fast offline rendering needs a plug-in
        fastToggle.setEnabled (! hw && silentToggle.getToggleState() && job == nullptr);
        silentToggle.setTooltip (hw ? "Hardware: no se envía la entrada a los altavoces mientras se graba"_u8
                                    : "Graba sin sonido en los altavoces"_u8);
        mappedControls->setVisible (! hw);
        hardwareControls.setVisible (hw);
        controlsSection.setTitle (hw ? "Controles MIDI del instrumento" : "Controles del plugin (MIDI Learn)");
        resized();
    }

    void MainComponent::loadHardwareControls()
    {
        // one set of controls per MIDI output (each synth has its own CCs)
        const auto device = midiOutBox.getSelectedId() > 0 && midiOutBox.getSelectedId() != 9999 ? midiOutBox.getText() : juce::String ("default");
        const auto key = "hwcontrols_" + juce::String::toHexString (device.hashCode64());
        if (key == hardwareControlsKey)
            return;
        hardwareControlsKey = key;
        auto xml = properties.getUserSettings()->getXmlValue (key);
        hardwareControls.fromXml (xml.get());
    }

    void MainComponent::saveHardwareControls()
    {
        if (hardwareControlsKey.isEmpty())
            loadHardwareControls();
        if (auto xml = hardwareControls.toXml())
            properties.getUserSettings()->setValue (hardwareControlsKey, xml.get());
        settingsDirty = true;
    }

    void MainComponent::updateMidiOutputList()
    {
        const auto current = engine.getMidiOutputIdentifier().isNotEmpty() ? engine.getMidiOutputIdentifier() : wantedMidiOutputId;
        midiOutBox.clear (juce::dontSendNotification);
        midiOutputIds.clear();
        int id = 1;
        for (auto& d : juce::MidiOutput::getAvailableDevices())
        {
            midiOutBox.addItem (d.name, id++);
            midiOutputIds.add (d.identifier);
        }
        midiOutBox.addSeparator();
        midiOutBox.addItem ("Actualizar la lista", 9999);
        const int index = midiOutputIds.indexOf (current);
        midiOutBox.setSelectedId (index >= 0 ? index + 1 : 0, juce::dontSendNotification);
    }

    void MainComponent::updateInputList()
    {
        const int previous = inputBox.getSelectedId() > 0 ? inputBox.getSelectedId() : wantedInputId;
        const auto names = engine.getInputChannelNames();
        inputBox.clear (juce::dontSendNotification);
        for (int ch = 0; ch + 1 < names.size(); ch += 2)
            inputBox.addItem (juce::String (ch + 1) + "+" + juce::String (ch + 2) + "  (" + names[ch] + " / " + names[ch + 1] + ")"_u8, 1000 + ch);
        if (names.size() > 0)
            inputBox.addSeparator();
        for (int ch = 0; ch < names.size(); ++ch)
            inputBox.addItem (juce::String (ch + 1) + " mono  (" + names[ch] + ")", 2000 + ch);

        if (inputBox.indexOfItemId (previous) >= 0)
            inputBox.setSelectedId (previous, juce::dontSendNotification);
        else if (inputBox.getNumItems() > 0)
            inputBox.setSelectedItemIndex (0, juce::dontSendNotification);
    }

    void MainComponent::applyHardwareSettingsToEngine()
    {
        const bool hw = isHardwareMode();
        engine.setSoundSource (hw ? SoundSource::hardware : SoundSource::plugin);
        engine.setMidiChannel (channelBox.getSelectedId() > 0 ? channelBox.getSelectedId() : 1);
        engine.setMonitorInput (monitorToggle.getToggleState());
        engine.setExtraLatencyMs (latencySlider.getValue());

        const int outIndex = midiOutBox.getSelectedId() - 1;
        const auto outId = juce::isPositiveAndBelow (outIndex, midiOutputIds.size()) ? midiOutputIds[outIndex] : juce::String();
        if (outId.isNotEmpty()) wantedMidiOutputId = outId;
        if (hw && outId != engine.getMidiOutputIdentifier())
            if (! engine.setMidiOutput (outId))
                appendLog ("No se pudo abrir la salida MIDI " + midiOutBox.getText());

        const int inputId = inputBox.getSelectedId();
        if (inputId > 0)
        {
            wantedInputId = inputId;
            if (hw)
                engine.setInputChannels (inputId % 1000, inputId < 2000);
        }
    }

    void MainComponent::updateBatchControls()
    {
        const bool on = batchToggle.getToggleState();
        const bool hw = isHardwareMode();
        if (hw && methodBox.getSelectedId() == 2)
            methodBox.setSelectedId (1, juce::dontSendNotification);
        methodBox.setItemEnabled (2, ! hw);
        const bool midi = methodBox.getSelectedId() != 2;
        const int bank = bankModeBox.getSelectedId();

        for (auto* c : { (juce::Component*) &methodBox, (juce::Component*) &programFromSlider, (juce::Component*) &programToSlider,
                         (juce::Component*) &waitSlider, (juce::Component*) &namesButton, (juce::Component*) &skipSilentToggle,
                         (juce::Component*) &programLabelB, (juce::Component*) &programToLabel, (juce::Component*) &waitLabel })
            c->setEnabled (on);
        bankModeBox.setEnabled (on && midi);
        msbFromSlider.setEnabled (on && midi && (bank == 2 || bank == 4));
        msbToSlider.setEnabled (msbFromSlider.isEnabled());
        lsbFromSlider.setEnabled (on && midi && (bank == 3 || bank == 4));
        lsbToSlider.setEnabled (lsbFromSlider.isEnabled());
        for (auto* l : { &msbLabel, &msbToLabel }) l->setAlpha (msbFromSlider.isEnabled() ? 1.0f : 0.4f);
        for (auto* l : { &lsbLabel, &lsbToLabel }) l->setAlpha (lsbFromSlider.isEnabled() ? 1.0f : 0.4f);
        for (auto* l : { &programLabelB, &programToLabel, &waitLabel }) l->setAlpha (on ? 1.0f : 0.4f);
        sendNowButton.setEnabled (job == nullptr);
        namesButton.setButtonText (programNames.isEmpty() ? "Nombres..." : "Nombres (" + juce::String (programNames.size()) + ")");
        settingsDirty = true;
    }

    void MainComponent::sendProgramNow()
    {
        const auto s = collectSettings();
        const int program = (int) programFromSlider.getValue();
        if (s.programMethod == ProgramChangeMethod::pluginPrograms && ! isHardwareMode())
        {
            engine.setPluginProgram (program);
            updateProgramList();
            appendLog ("Programa del plugin "_u8 + juce::String (program + 1));
            return;
        }

        if (isHardwareMode() && engine.getMidiOutputIdentifier().isEmpty())
        {
            appendLog ("Elige primero la salida MIDI");
            return;
        }

        const int ch = s.midiChannel;
        juce::String text;
        if (s.bankMode == BankSelectMode::msb || s.bankMode == BankSelectMode::msbLsb)
        {
            engine.queueMidi (juce::MidiMessage::controllerEvent (ch, 0, s.bankMsbFrom));
            text << "CC0=" << s.bankMsbFrom << " ";
        }
        if (s.bankMode == BankSelectMode::lsb || s.bankMode == BankSelectMode::msbLsb)
        {
            engine.queueMidi (juce::MidiMessage::controllerEvent (ch, 32, s.bankLsbFrom));
            text << "CC32=" << s.bankLsbFrom << " ";
        }
        engine.queueMidi (juce::MidiMessage::programChange (ch, program));
        appendLog ("Enviado: " + text + "Program Change " + juce::String (program + 1) + " (canal " + juce::String (ch) + ")");
    }

    void MainComponent::editProgramNames()
    {
        struct NamesEditor : public juce::Component
        {
            juce::Label title;
            juce::TextEditor editor;
            std::function<void (const juce::StringArray&)> onChanged;

            NamesEditor (const juce::StringArray& names)
            {
                title.setText ("Un nombre por línea, en el orden del lote (vacío = automático)"_u8, juce::dontSendNotification);
                title.setFont (juce::FontOptions (12.5f));
                title.setColour (juce::Label::textColourId, Colours::textDim);
                addAndMakeVisible (title);
                editor.setMultiLine (true, false);
                editor.setReturnKeyStartsNewLine (true);
                editor.setScrollbarsShown (true);
                editor.setText (names.joinIntoString ("\n"), false);
                editor.onTextChange = [this]
                {
                    auto lines = juce::StringArray::fromLines (editor.getText());
                    while (lines.size() > 0 && lines[lines.size() - 1].trim().isEmpty())
                        lines.remove (lines.size() - 1);
                    if (onChanged) onChanged (lines);
                };
                addAndMakeVisible (editor);
                setSize (360, 300);
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (8);
                title.setBounds (r.removeFromTop (20));
                editor.setBounds (r);
            }
        };

        auto names = std::make_unique<NamesEditor> (programNames);
        names->onChanged = [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::StringArray& lines)
        {
            if (safe != nullptr)
            {
                safe->programNames = lines;
                safe->updateBatchControls();
            }
        };
        juce::CallOutBox::launchAsynchronously (std::move (names), namesButton.getScreenBounds(), nullptr);
    }

    void MainComponent::measureNoiseFloor()
    {
        noiseButton.setEnabled (false);
        noiseButton.setButtonText ("Midiendo...");
        engine.getNoisePeakAndReset();
        juce::Timer::callAfterDelay (2500, [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr) return;
            auto& self = *safe;
            const float peak = self.engine.getNoisePeakAndReset();
            const float db = juce::Decibels::gainToDecibels (peak, -120.0f);
            self.noiseButton.setEnabled (self.job == nullptr);
            self.noiseButton.setButtonText ("Medir ruido");

            if (db > -30.0f)
            {
                self.appendLog ("Medir ruido: hay sonido (" + juce::String (db, 1) + " dB). Deja el instrumento en silencio, sin tocar, y vuelve a medir.");
                return;
            }
            const double threshold = juce::jlimit (-96.0, -12.0, (double) std::ceil (db + 6.0f));
            self.thresholdSlider.setValue (threshold, juce::sendNotificationSync);
            self.appendLog ("Ruido de fondo: " + (peak > 0.0f ? juce::String (db, 1) + " dB" : juce::String ("silencio digital"))
                            + "  ->  umbral " + juce::String ((int) threshold) + " dB");
        });
    }

    void MainComponent::createRangeSection()
    {
        addAndMakeVisible (rangeSection);
        auto& p = rangeSection;

        setupLabel (p, notesModeLabel, "Notas");
        rangeModeButton.setButtonText ("Rango");
        listModeButton.setButtonText ("One Shot");
        listModeButton.setTooltip ("Elige en el piano exactamente las notas que quieres samplear (p. ej. C1, D1, E1 y G1); cada muestra ocupa solo su tecla. "
                                   "Sin loop, la muestra suena entera aunque sueltes la tecla. El loop lo activas tú si lo quieres."_u8);
        for (auto* b : { &rangeModeButton, &listModeButton })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (4711);
            p.addAndMakeVisible (*b);
        }

        rangeModeButton.onClick = [this] { if (rangeModeButton.getToggleState()) { useNoteList = false; pickingNotes = false; updateNoteModeControls(); updateLoopControls(); updateDerivedDisplays(); } };
        listModeButton.onClick = [this]
        {
            if (! listModeButton.getToggleState())
                return;
            useNoteList = true;
            pickingNotes = true;     // start picking right away
            setLearnMode (LearnMode::none);
            updateNoteModeControls();
            updateLoopControls();
            updateDerivedDisplays();
        };

        pickNotesButton.setButtonText ("Elegir notas en el piano");
        pickNotesButton.setClickingTogglesState (true);
        pickNotesButton.setTooltip ("Activado: cada nota que pulses en el piano de abajo o en tu teclado MIDI se añade o se quita de la lista"_u8);
        pickNotesButton.onClick = [this] { pickingNotes = pickNotesButton.getToggleState(); updateNoteModeControls(); };
        p.addAndMakeVisible (pickNotesButton);

        clearNotesButton.setButtonText ("Borrar notas");
        clearNotesButton.onClick = [this] { noteList.clear(); updateNoteModeControls(); updateDerivedDisplays(); };
        p.addAndMakeVisible (clearNotesButton);

        setupLabel (p, lowLabel, "Nota más baja"_u8);
        setupNoteSlider (p, lowSlider);
        setupLabel (p, highLabel, "Nota más alta"_u8);
        setupNoteSlider (p, highSlider);
        lowSlider.onValueChange = [this] { if (lowSlider.getValue() > highSlider.getValue()) highSlider.setValue (lowSlider.getValue()); updateDerivedDisplays(); };
        highSlider.onValueChange = [this] { if (highSlider.getValue() < lowSlider.getValue()) lowSlider.setValue (highSlider.getValue()); updateDerivedDisplays(); };

        learnChordButton.setButtonText ("Aprender (acorde)");
        learnChordButton.setTooltip ("Toca a la vez la nota más baja y la más alta en tu teclado MIDI"_u8);
        learnChordButton.onClick = [this] { setLearnMode (learnMode == LearnMode::chord ? LearnMode::none : LearnMode::chord); };
        p.addAndMakeVisible (learnChordButton);

        learnLowButton.setButtonText ("Aprender baja");
        learnLowButton.setTooltip ("Pulsa y toca la nota más baja (teclados de pocas teclas: cambia de octava entre una y otra)"_u8);
        learnLowButton.onClick = [this] { setLearnMode (learnMode == LearnMode::low ? LearnMode::none : LearnMode::low); };
        p.addAndMakeVisible (learnLowButton);

        learnHighButton.setButtonText ("Aprender alta");
        learnHighButton.setTooltip ("Pulsa y toca la nota más alta"_u8);
        learnHighButton.onClick = [this] { setLearnMode (learnMode == LearnMode::high ? LearnMode::none : LearnMode::high); };
        p.addAndMakeVisible (learnHighButton);

        wizardButton.setButtonText ("Asistente...");
        wizardButton.setTooltip ("Ventana que te pide primero la nota más baja y luego la más alta"_u8);
        wizardButton.onClick = [this]
        {
            setLearnMode (LearnMode::none);
            RangeWizard::show (this, 3, [this] (int lo, int hi)
            {
                lowSlider.setValue (lo, juce::dontSendNotification);
                highSlider.setValue (hi, juce::dontSendNotification);
                updateDerivedDisplays();
                appendLog ("Rango: " + NoteUtils::getNoteName (lo, 3) + " - " + NoteUtils::getNoteName (hi, 3));
            }, rangeWizard);
        };
        p.addAndMakeVisible (wizardButton);

        learnStatus.setFont (juce::FontOptions (12.0f));
        learnStatus.setColour (juce::Label::textColourId, Colours::accent2);
        p.addAndMakeVisible (learnStatus);

        setupLabel (p, stepLabel, "Grabar cada");
        stepSlider.setSliderStyle (juce::Slider::IncDecButtons);
        stepSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 22);
        stepSlider.setRange (1, 24, 1);
        stepSlider.textFromValueFunction = [] (double v) { return juce::String ((int) v) + ((int) v == 1 ? " semitono" : " semitonos"); };
        stepSlider.valueFromTextFunction = [] (const juce::String& t) { return (double) t.getIntValue(); };
        stepSlider.onValueChange = [this] { updateDerivedDisplays(); };
        p.addAndMakeVisible (stepSlider);

        extendToggle.setButtonText ("Extender las zonas a todo el teclado");
        extendToggle.onClick = [this] { updateDerivedDisplays(); };
        p.addAndMakeVisible (extendToggle);

        setupLabel (p, layersLabel, "Capas de velocidad");
        layersSlider.setSliderStyle (juce::Slider::IncDecButtons);
        layersSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 22);
        layersSlider.setRange (1, 16, 1);
        layersSlider.textFromValueFunction = [] (double v) { return juce::String ((int) v) + ((int) v == 1 ? " capa" : " capas"); };
        layersSlider.valueFromTextFunction = [] (const juce::String& t) { return (double) t.getIntValue(); };
        layersSlider.onValueChange = [this] { updateDerivedDisplays(); };
        p.addAndMakeVisible (layersSlider);

        setupLabel (p, playPointLabel, "Velocidad tocada");
        playPointBox.addItem ("Máxima de cada capa"_u8, 1);
        playPointBox.addItem ("Centro de cada capa", 2);
        playPointBox.addItem ("Mínima de cada capa"_u8, 3);
        playPointBox.onChange = [this] { updateDerivedDisplays(); };
        p.addAndMakeVisible (playPointBox);

        layersInfo.setFont (juce::FontOptions (11.5f));
        layersInfo.setMinimumHorizontalScale (1.0f);
        layersInfo.setColour (juce::Label::textColourId, Colours::text);
        layersInfo.setJustificationType (juce::Justification::topLeft);
        p.addAndMakeVisible (layersInfo);

        setupLabel (p, holdLabel, "Duración nota"_u8);
        setupSlider (p, holdSlider, 0.1, 30.0, 0.1, " s");
        holdSlider.setSkewFactorFromMidPoint (4.0);
        holdSlider.onValueChange = [this] { updateDerivedDisplays(); };

        setupLabel (p, releaseLabel, "Release máximo"_u8);
        setupSlider (p, releaseSlider, 0.2, 60.0, 0.1, " s");
        releaseSlider.setSkewFactorFromMidPoint (6.0);
        releaseSlider.setTooltip ("Límite de seguridad para la cola. El sampler pasa a la siguiente nota en cuanto el sonido baja del umbral."_u8);
        releaseSlider.onValueChange = [this] { updateDerivedDisplays(); };

        setupLabel (p, gapLabel, "Pausa entre notas");
        setupSlider (p, gapSlider, 0.0, 3000.0, 10.0, " ms");
        gapSlider.onValueChange = [this] { updateDerivedDisplays(); };

        summaryLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        summaryLabel.setColour (juce::Label::textColourId, Colours::accent);
        p.addAndMakeVisible (summaryLabel);
        sizeLabel.setFont (juce::FontOptions (12.0f));
        sizeLabel.setColour (juce::Label::textColourId, Colours::textDim);
        p.addAndMakeVisible (sizeLabel);
    }

    void MainComponent::createThresholdSection()
    {
        addAndMakeVisible (thresholdSection);
        auto& p = thresholdSection;

        meter.getLevel = [this] { return engine.getMeterPeakAndReset(); };
        meter.onThresholdChanged = [this] (double db) { thresholdSlider.setValue (db, juce::dontSendNotification); settingsDirty = true; };
        meter.setTooltip ("Arrastra la línea naranja para ajustar el umbral. Cuando el nivel queda por debajo, el sampler pasa a la siguiente nota."_u8);
        p.addAndMakeVisible (meter);

        setupLabel (p, thresholdLabel, "Umbral");
        thresholdSlider.setSliderStyle (juce::Slider::IncDecButtons);
        thresholdSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 22);
        thresholdSlider.setRange (-96.0, -12.0, 1.0);
        thresholdSlider.setTextValueSuffix (" dB");
        thresholdSlider.onValueChange = [this] { meter.setThresholdDb (thresholdSlider.getValue()); settingsDirty = true; };
        p.addAndMakeVisible (thresholdSlider);

        setupLabel (p, silenceLabel, "Tiempo bajo umbral");
        silenceSlider.setSliderStyle (juce::Slider::IncDecButtons);
        silenceSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 22);
        silenceSlider.setRange (10.0, 2000.0, 10.0);
        silenceSlider.setTextValueSuffix (" ms");
        silenceSlider.setTooltip ("El sonido debe permanecer bajo el umbral este tiempo para considerarse terminado"_u8);
        p.addAndMakeVisible (silenceSlider);

        setupLabel (p, maxWaitLabel, "Espera máxima de silencio"_u8);
        maxWaitSlider.setSliderStyle (juce::Slider::IncDecButtons);
        maxWaitSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 22);
        maxWaitSlider.setRange (1.0, 120.0, 1.0);
        maxWaitSlider.setTextValueSuffix (" s");
        maxWaitSlider.setTooltip ("Antes de cada nota se espera a que el sonido anterior baje del umbral, como mucho este tiempo. "
                                  "Si no baja, se envía All Notes Off y se vuelve a esperar"_u8);
        maxWaitSlider.onValueChange = [this] { settingsDirty = true; };
        p.addAndMakeVisible (maxWaitSlider);

        noiseButton.setButtonText ("Medir ruido");
        noiseButton.setTooltip ("Con el instrumento en silencio, mide el ruido de fondo durante 2,5 s y pone el umbral 6 dB por encima. "
                                "Imprescindible con instrumentos hardware"_u8);
        noiseButton.onClick = [this] { measureNoiseFloor(); };
        p.addAndMakeVisible (noiseButton);
    }

    void MainComponent::createLoopSection()
    {
        addAndMakeVisible (loopSection);
        auto& p = loopSection;

        loopToggle.setButtonText ("Loop");
        loopToggle.setClickingTogglesState (true);
        loopToggle.setTooltip ("Activa o desactiva los loops"_u8);
        loopToggle.onClick = [this] { updateLoopControls(); updateDerivedDisplays(); };
        p.addAndMakeVisible (loopToggle);

        crossfadeToggle.setButtonText ("Crossfade");
        crossfadeToggle.setClickingTogglesState (true);
        crossfadeToggle.setTooltip ("Funde el final del loop con el audio anterior a su inicio para que la repetición no se note"_u8);
        crossfadeToggle.onClick = [this] { updateLoopControls(); settingsDirty = true; };
        p.addAndMakeVisible (crossfadeToggle);

        linearShapeButton.setTooltip ("Crossfade lineal: el mejor para sonidos sostenidos y estables");
        curvedShapeButton.setTooltip ("Crossfade curvado (potencia constante): para sonidos que cambian dentro del loop");
        for (auto* b : { (juce::Button*) &linearShapeButton, (juce::Button*) &curvedShapeButton })
        {
            b->setRadioGroupId (4712);
            b->onClick = [this] { settingsDirty = true; };
            p.addAndMakeVisible (*b);
        }

        setupLabel (p, loopModeLabel, "Modo");
        loopModeBox.addItemList (getLoopModeNames(), 1);
        loopModeBox.onChange = [this] { updateLoopControls(); updateDerivedDisplays(); };
        p.addAndMakeVisible (loopModeBox);

        setupLabel (p, loopStartLabel, "Inicio loop");
        setupSlider (p, loopStartSlider, 0.0, 95.0, 1.0, " %");
        loopStartSlider.setTooltip ("Dónde empieza el loop, en % de la parte sostenida de la nota. Se busca el mejor cruce por cero alrededor de este punto."_u8);
        setupLabel (p, loopEndLabel, "Final loop");
        setupSlider (p, loopEndSlider, 5.0, 100.0, 1.0, " %");
        loopEndSlider.setTooltip ("Dónde termina el loop, en % de la parte sostenida. Cuanto más largo el loop, más natural suena."_u8);
        loopStartSlider.onValueChange = [this] { if (loopEndSlider.getValue() <= loopStartSlider.getValue() + 4) loopEndSlider.setValue (loopStartSlider.getValue() + 5); updateDerivedDisplays(); };
        loopEndSlider.onValueChange = [this] { if (loopStartSlider.getValue() >= loopEndSlider.getValue() - 4) loopStartSlider.setValue (loopEndSlider.getValue() - 5); updateDerivedDisplays(); };

        setupLabel (p, crossfadeLabel, "Crossfade");
        setupSlider (p, crossfadeSlider, 1.0, 2000.0, 1.0, " ms");
        crossfadeSlider.setSkewFactorFromMidPoint (150.0);
        crossfadeSlider.onValueChange = [this] { settingsDirty = true; };

        setupLabel (p, crossfadeWhereLabel, "Aplicar en");
        crossfadeWhereBox.addItem ("El audio (todos los samplers)", 1);
        crossfadeWhereBox.addItem ("El sampler (MPC, Kontakt, Logic...)", 2);
        crossfadeWhereBox.setTooltip ("El audio: el crossfade queda grabado en el WAV. "
                                      "El sampler: el WAV queda sin fundido y el valor se escribe en el parámetro de crossfade de cada formato (MPC, Kontakt, Logic, Ableton, Bitwig, SFZ, Decent Sampler). SF2 no tiene crossfade."_u8);
        crossfadeWhereBox.onChange = [this] { updateLoopControls(); settingsDirty = true; };
        p.addAndMakeVisible (crossfadeWhereBox);

        equaliseToggle.setButtonText ("Igualar el nivel dentro del loop");
        equaliseToggle.setTooltip ("Si el sonido sigue bajando de volumen mientras se mantiene la nota, compensa esa caída para que no haya un salto de volumen al repetir el loop (máx. +12 dB)"_u8);
        equaliseToggle.onClick = [this] { settingsDirty = true; };
        p.addAndMakeVisible (equaliseToggle);

        loopHelp.setFont (juce::FontOptions (12.0f));
        loopHelp.setMinimumHorizontalScale (1.0f);
        loopHelp.setColour (juce::Label::textColourId, Colours::textDim);
        loopHelp.setJustificationType (juce::Justification::topLeft);
        p.addAndMakeVisible (loopHelp);

        p.addAndMakeVisible (waveform);
    }

    void MainComponent::createOutputSection()
    {
        addAndMakeVisible (outputSection);
        auto& p = outputSection;

        setupLabel (p, nameLabel, "Nombre");
        nameEditor.onTextChange = [this] { settingsDirty = true; };
        p.addAndMakeVisible (nameEditor);
        setupLabel (p, creatorLabel, "Autor");
        p.addAndMakeVisible (creatorEditor);
        setupLabel (p, categoryLabel, "Categoría"_u8);
        categoryEditor.setTextToShowWhenEmpty ("Piano, Pad, Bass, Lead, Drums...", Colours::textDim);
        p.addAndMakeVisible (categoryEditor);

        folderButton.setButtonText ("Carpeta...");
        folderButton.onClick = [this] { chooseOutputFolder(); };
        p.addAndMakeVisible (folderButton);
        folderLabel.setFont (juce::FontOptions (11.5f));
        folderLabel.setColour (juce::Label::textColourId, Colours::text);
        folderLabel.setMinimumHorizontalScale (0.5f);
        p.addAndMakeVisible (folderLabel);

        setupLabel (p, bitDepthLabel, "Formato WAV");
        bitDepthBox.addItem ("16 bit", 16);
        bitDepthBox.addItem ("24 bit", 24);
        bitDepthBox.addItem ("32 bit float", 32);
        bitDepthBox.onChange = [this] { updateDerivedDisplays(); };
        p.addAndMakeVisible (bitDepthBox);

        stereoToggle.setButtonText ("Estéreo"_u8);
        normalizeToggle.setButtonText ("Normalizar");
        normalizeToggle.setTooltip ("Una sola ganancia para todo el instrumento (mantiene las diferencias entre capas)"_u8);
        trimToggle.setButtonText ("Recortar silencio inicial");
        for (auto* t : { &stereoToggle, &normalizeToggle, &trimToggle })
            p.addAndMakeVisible (*t);
        stereoToggle.onClick = [this] { updateDerivedDisplays(); };

        setupLabel (p, formatsLabel, "Formatos de salida");
        for (auto& info : getAvailableExporters())
        {
            auto* toggle = formatToggles.add (new juce::ToggleButton (info.name));
            toggle->setTooltip (info.description);
            toggle->onClick = [this] { updateDerivedDisplays(); };
            if (info.viaConvertWithMoss)
                toggle->setColour (juce::ToggleButton::textColourId, Colours::textDim);
            formatsHolder.addAndMakeVisible (toggle);
            formatIds.add (info.id);
        }
        formatsViewport.setViewedComponent (&formatsHolder, false);
        formatsViewport.setScrollBarsShown (true, false);
        p.addAndMakeVisible (formatsViewport);

        mossButton.setButtonText ("ConvertWithMoss...");
        mossButton.setTooltip ("Los formatos en gris se crean con ConvertWithMoss (gratuito). Selecciona aquí su ejecutable si no se detecta solo."_u8);
        mossButton.onClick = [this] { chooseConvertWithMoss(); };
        p.addAndMakeVisible (mossButton);
        mossLabel.setFont (juce::FontOptions (11.0f));
        mossLabel.setMinimumHorizontalScale (0.5f);
        p.addAndMakeVisible (mossLabel);

        logEditor.setMultiLine (true);
        logEditor.setReadOnly (true);
        logEditor.setScrollbarsShown (true);
        logEditor.setCaretVisible (false);
        logEditor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
        logEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.4f));
        p.addAndMakeVisible (logEditor);
    }

    void MainComponent::createTransport()
    {
        startButton.setButtonText ("EMPEZAR A SAMPLEAR");
        startButton.setColour (juce::TextButton::buttonColourId, Colours::accent);
        startButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
        startButton.onClick = [this] { startOrStopSampling(); };
        addAndMakeVisible (startButton);

        silentToggle.setButtonText ("Modo silencio");
        silentToggle.setTooltip ("Graba sin que el instrumento suene por los altavoces: puedes seguir trabajando o escuchando otra cosa mientras samplea"_u8);
        silentToggle.onClick = [this] { fastToggle.setEnabled (silentToggle.getToggleState() && ! isHardwareMode()); updateDerivedDisplays(); };
        addAndMakeVisible (silentToggle);

        fastToggle.setButtonText ("Rápido"_u8);
        fastToggle.setTooltip ("Con el modo silencio: renderiza el plugin más rápido que en tiempo real (modo offline). "
                               "Si algún plugin suena distinto o le faltan notas (p. ej. librerías que cargan muestras desde disco), desactívalo."_u8);
        fastToggle.onClick = [this] { updateDerivedDisplays(); };
        addAndMakeVisible (fastToggle);

        addAndMakeVisible (progressBar);
        statusLabel.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (statusLabel);

        openFolderButton.setButtonText ("Abrir carpeta");
        openFolderButton.onClick = [this]
        {
            auto f = lastInstrumentFolder.isDirectory() ? lastInstrumentFolder : outputFolder;
            if (f.isDirectory()) f.startAsProcess();
        };
        addAndMakeVisible (openFolderButton);
    }

    //==============================================================================
    void MainComponent::setUiScale (float newScale)
    {
        newScale = juce::jlimit (0.75f, 2.0f, newScale);
        uiScale = newScale;
        if (scaleBox.indexOfItemId (juce::roundToInt (newScale * 100.0f)) >= 0)
            scaleBox.setSelectedId (juce::roundToInt (newScale * 100.0f), juce::dontSendNotification);
        properties.getUserSettings()->setValue ("uiScale", (double) newScale);
        if (onScaleChanged)
            onScaleChanged (newScale);
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        const int steps[] = { 75, 90, 100, 110, 125, 150, 175, 200 };
        const int current = juce::roundToInt (uiScale * 100.0f);

        if (key.getModifiers().isCommandDown())
        {
            const auto c = key.getTextCharacter();
            if (c == '+' || c == '=' || key.getKeyCode() == juce::KeyPress::numberPadAdd)
            {
                for (int s : steps) if (s > current) { setUiScale ((float) s / 100.0f); break; }
                return true;
            }
            if (c == '-' || key.getKeyCode() == juce::KeyPress::numberPadSubtract)
            {
                for (int i = (int) std::size (steps) - 1; i >= 0; --i) if (steps[i] < current) { setUiScale ((float) steps[i] / 100.0f); break; }
                return true;
            }
            if (c == '0')
            {
                setUiScale (1.0f);
                return true;
            }
        }
        return false;
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (Colours::background);

        // header
        auto header = getLocalBounds().removeFromTop (36).reduced (14, 0);
        g.setColour (Colours::accent);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("WAVETHINGS", header, juce::Justification::centredLeft);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (20.0f));
        g.drawText ("AUTOSAMPLER", header.withTrimmedLeft (138), juce::Justification::centredLeft);
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("v" WT_VERSION_STRING, header, juce::Justification::centredRight);
        if (engine.isOutputMuted() && job != nullptr)
        {
            g.setColour (Colours::accent2);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (engine.isOffline() ? "MODO SILENCIO · RÁPIDO"_u8 : "MODO SILENCIO"_u8, header.withTrimmedRight (210), juce::Justification::centredRight);
        }
    }

    void MainComponent::resized()
    {
        scaleBox.setBounds (getWidth() - 190, 7, 130, 22);
        {
            auto top = juce::Rectangle<int> (330, 7, scaleBox.getX() - 20 - 330, 22);
            presetMenuButton.setBounds (top.removeFromRight (120));
            top.removeFromRight (6);
            presetBox.setBounds (top.removeFromRight (juce::jmin (330, top.getWidth() - 130)));
            presetLabel.setBounds (top.removeFromRight (130));
        }

        auto area = getLocalBounds().reduced (10);
        area.removeFromTop (28);
        const int gap = 8;

        // Transport (bottom)
        auto transport = area.removeFromBottom (40);
        startButton.setBounds (transport.removeFromLeft (230));
        transport.removeFromLeft (gap + 4);
        silentToggle.setBounds (transport.removeFromLeft (128));
        fastToggle.setBounds (transport.removeFromLeft (84));
        transport.removeFromLeft (gap);
        openFolderButton.setBounds (transport.removeFromRight (120));
        transport.removeFromRight (gap);
        progressBar.setBounds (transport.removeFromLeft (juce::jmin (300, transport.getWidth() / 2)).reduced (0, 8));
        transport.removeFromLeft (gap);
        statusLabel.setBounds (transport);
        area.removeFromBottom (gap);

        {
            auto keys = area.removeFromBottom (76 + 22);
            rangeLeftButton.setBounds (keys.removeFromLeft (20).reduced (2, 30));
            rangeRightButton.setBounds (keys.removeFromRight (20).reduced (2, 30));
            keys.removeFromLeft (4);
            keys.removeFromRight (4);
            rangeBar.setBounds (keys.removeFromTop (22));
            keyboard.setBounds (keys);
        }
        {
            // 88 keys (A0..C8) = 52 white keys, always filling the width
            keyboard.setKeyWidth (juce::jmax (8.0f, (float) (keyboard.getWidth() - 4) / 52.0f));
            keyboard.setLowestVisibleKey (21);
        }
        area.removeFromBottom (gap);

        {
            auto bottomRow = area.removeFromBottom (172);
            batchSection.setBounds (bottomRow.removeFromRight (600));
            bottomRow.removeFromRight (gap);
            controlsSection.setBounds (bottomRow);
            mappedControls->setBounds (controlsSection.getContentArea());
            hardwareControls.setBounds (controlsSection.getContentArea());

            auto r = batchSection.getContentArea();
            auto line = [&r] { auto l = r.removeFromTop (24); r.removeFromTop (4); return l; };
            {
                auto l = line();
                batchToggle.setBounds (l.removeFromLeft (220));
                l.removeFromLeft (8);
                methodBox.setBounds (l.removeFromLeft (200));
            }
            {
                auto l = line();
                programLabelB.setBounds (l.removeFromLeft (64));
                programFromSlider.setBounds (l.removeFromLeft (96));
                programToLabel.setBounds (l.removeFromLeft (22));
                programToSlider.setBounds (l.removeFromLeft (96));
                l.removeFromLeft (16);
                waitLabel.setBounds (l.removeFromLeft (52));
                waitSlider.setBounds (l.removeFromLeft (140));
            }
            {
                auto l = line();
                bankModeBox.setBounds (l.removeFromLeft (136));
                l.removeFromLeft (6);
                msbLabel.setBounds (l.removeFromLeft (34));
                msbFromSlider.setBounds (l.removeFromLeft (78));
                msbToLabel.setBounds (l.removeFromLeft (16));
                msbToSlider.setBounds (l.removeFromLeft (78));
                l.removeFromLeft (8);
                lsbLabel.setBounds (l.removeFromLeft (30));
                lsbFromSlider.setBounds (l.removeFromLeft (78));
                lsbToLabel.setBounds (l.removeFromLeft (16));
                lsbToSlider.setBounds (l.removeFromLeft (78));
            }
            {
                auto l = line();
                sendNowButton.setBounds (l.removeFromLeft (110));
                l.removeFromLeft (6);
                namesButton.setBounds (l.removeFromLeft (110));
                l.removeFromLeft (10);
                skipSilentToggle.setBounds (l.removeFromLeft (200));
            }
            batchInfo.setBounds (r.removeFromTop (20));
        }
        area.removeFromBottom (gap);

        // Plug-in row
        pluginSection.setBounds (area.removeFromTop (66));
        {
            auto r = pluginSection.getContentArea().withSizeKeepingCentre (pluginSection.getContentArea().getWidth(), 26);
            sourcePluginButton.setBounds (r.removeFromLeft (76));
            sourceHardwareButton.setBounds (r.removeFromLeft (86));
            r.removeFromLeft (12);
            {
                // hardware row (shown instead of the plug-in controls)
                auto h = r;
                audioButton.setBounds (h.removeFromRight (120));
                midiOutLabel.setBounds (h.removeFromLeft (76));
                midiOutBox.setBounds (h.removeFromLeft (230));
                h.removeFromLeft (10);
                channelLabel.setBounds (h.removeFromLeft (44));
                channelBox.setBounds (h.removeFromLeft (62));
                h.removeFromLeft (10);
                inputLabel.setBounds (h.removeFromLeft (54));
                inputBox.setBounds (h.removeFromLeft (juce::jmin (230, juce::jmax (120, h.getWidth() - 400))));
                h.removeFromLeft (10);
                monitorToggle.setBounds (h.removeFromLeft (140));
                h.removeFromLeft (6);
                latencyLabel.setBounds (h.removeFromLeft (70));
                latencySlider.setBounds (h.removeFromLeft (120));
                if (! isHardwareMode())
                {
                    // the channel is only shown in hardware mode
                    channelLabel.setVisible (false);
                    channelBox.setVisible (false);
                }
                else
                {
                    channelLabel.setVisible (true);
                    channelBox.setVisible (true);
                }
            }
            formatFilterBox.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (6);
            pluginBox.setBounds (r.removeFromLeft (juce::jmin (330, r.getWidth() / 3)));
            favouriteButton.setBounds (r.removeFromLeft (28));
            r.removeFromLeft (4);
            scanButton.setBounds (r.removeFromLeft (96));
            r.removeFromLeft (6);
            editorButton.setBounds (r.removeFromLeft (150));
            r.removeFromLeft (6);
            unloadButton.setBounds (r.removeFromLeft (90));
            audioButton.setBounds (r.removeFromRight (120));
            r.removeFromRight (12);
            programBox.setBounds (r.removeFromRight (juce::jmin (240, juce::jmax (120, r.getWidth() - 80))));
            programLabel.setBounds (r.removeFromRight (70));
        }
        area.removeFromTop (gap);

        // Main row: range | threshold | loop | export
        auto rangeArea = area.removeFromLeft (370);
        area.removeFromLeft (gap);
        auto thresholdArea = area.removeFromLeft (200);
        area.removeFromLeft (gap);
        auto loopArea = area.removeFromLeft (juce::jmax (330, area.getWidth() / 2 - 20));
        area.removeFromLeft (gap);

        rangeSection.setBounds (rangeArea);
        {
            auto r = rangeSection.getContentArea();
            const int lw = 118;
            {
                auto b = r.removeFromTop (24);
                notesModeLabel.setBounds (b.removeFromLeft (52));
                rangeModeButton.setBounds (b.removeFromLeft (76));
                b.removeFromLeft (4);
                listModeButton.setBounds (b);
                r.removeFromTop (4);
            }
            row (r, lowLabel, lowSlider, lw);
            row (r, highLabel, highSlider, lw);
            {
                auto b = r.removeFromTop (24);
                const int w = (b.getWidth() - 12) / 4;
                learnChordButton.setBounds (b.removeFromLeft (w + 10)); b.removeFromLeft (4);
                learnLowButton.setBounds (b.removeFromLeft (w - 2)); b.removeFromLeft (4);
                learnHighButton.setBounds (b.removeFromLeft (w - 2)); b.removeFromLeft (4);
                wizardButton.setBounds (b);
                r.removeFromTop (2);
            }
            if (useNoteList)
            {
                // in note-list mode the learn buttons row shows the note picking tools instead
                auto b = learnChordButton.getBounds().getUnion (wizardButton.getBounds());
                pickNotesButton.setBounds (b.removeFromLeft (b.getWidth() * 2 / 3 - 2));
                b.removeFromLeft (4);
                clearNotesButton.setBounds (b);
            }
            else
            {
                pickNotesButton.setBounds ({});
                clearNotesButton.setBounds ({});
            }
            learnStatus.setBounds (r.removeFromTop (18));
            row (r, stepLabel, stepSlider, lw);
            extendToggle.setBounds (r.removeFromTop (22));
            r.removeFromTop (4);
            row (r, layersLabel, layersSlider, lw);
            row (r, playPointLabel, playPointBox, lw);
            layersInfo.setBounds (r.removeFromTop (34));
            r.removeFromTop (4);
            row (r, holdLabel, holdSlider, lw);
            row (r, releaseLabel, releaseSlider, lw);
            row (r, gapLabel, gapSlider, lw);
            summaryLabel.setBounds (r.removeFromTop (18));
            sizeLabel.setBounds (r.removeFromTop (18));
        }

        thresholdSection.setBounds (thresholdArea);
        {
            auto r = thresholdSection.getContentArea();
            auto bottom = r.removeFromBottom (182);
            meter.setBounds (r);
            {
                auto l = bottom.removeFromTop (22);
                noiseButton.setBounds (l.removeFromRight (86).withTrimmedBottom (2));
                thresholdLabel.setBounds (l);
            }
            thresholdSlider.setBounds (bottom.removeFromTop (24));
            bottom.removeFromTop (4);
            silenceLabel.setBounds (bottom.removeFromTop (18));
            silenceSlider.setBounds (bottom.removeFromTop (24));
            bottom.removeFromTop (4);
            maxWaitLabel.setBounds (bottom.removeFromTop (18));
            maxWaitSlider.setBounds (bottom.removeFromTop (24));
        }

        loopSection.setBounds (loopArea);
        {
            auto r = loopSection.getContentArea();
            const int lw = 96;
            {
                auto b = r.removeFromTop (24);
                loopToggle.setBounds (b.removeFromLeft (lw - 6));
                b.removeFromLeft (6);
                loopModeBox.setBounds (b);
                loopModeLabel.setVisible (false);
                r.removeFromTop (6);
            }
            row (r, loopStartLabel, loopStartSlider, lw);
            row (r, loopEndLabel, loopEndSlider, lw);
            {
                auto b = r.removeFromTop (24);
                crossfadeToggle.setBounds (b.removeFromLeft (lw - 6));
                b.removeFromLeft (6);
                linearShapeButton.setBounds (b.removeFromLeft (40));
                b.removeFromLeft (4);
                curvedShapeButton.setBounds (b.removeFromLeft (40));
                r.removeFromTop (4);
            }
            row (r, crossfadeLabel, crossfadeSlider, lw);
            row (r, crossfadeWhereLabel, crossfadeWhereBox, lw);
            equaliseToggle.setBounds (r.removeFromTop (22));
            r.removeFromTop (2);
            loopHelp.setBounds (r.removeFromTop (46));
            r.removeFromTop (6);
            waveform.setBounds (r);
        }

        outputSection.setBounds (area);
        {
            auto r = outputSection.getContentArea();
            const int lw = 78;
            row (r, nameLabel, nameEditor, lw);
            {
                auto b = r.removeFromTop (24);
                creatorLabel.setBounds (b.removeFromLeft (lw));
                creatorEditor.setBounds (b.removeFromLeft (b.getWidth() / 2 - 4));
                b.removeFromLeft (8);
                categoryLabel.setBounds (b.removeFromLeft (72));
                categoryEditor.setBounds (b);
                r.removeFromTop (4);
            }
            {
                auto b = r.removeFromTop (24);
                folderButton.setBounds (b.removeFromLeft (lw + 20));
                b.removeFromLeft (6);
                folderLabel.setBounds (b);
                r.removeFromTop (4);
            }
            {
                auto b = r.removeFromTop (24);
                bitDepthLabel.setBounds (b.removeFromLeft (lw + 10));
                bitDepthBox.setBounds (b.removeFromLeft (120));
                b.removeFromLeft (10);
                stereoToggle.setBounds (b);
                r.removeFromTop (2);
            }
            {
                auto b = r.removeFromTop (22);
                trimToggle.setBounds (b.removeFromLeft (b.getWidth() / 2 + 20));
                normalizeToggle.setBounds (b);
            }
            r.removeFromTop (4);
            formatsLabel.setBounds (r.removeFromTop (18));

            auto logArea = r.removeFromBottom (juce::jmax (90, r.getHeight() / 3));
            auto mossRow = r.removeFromBottom (24);
            mossButton.setBounds (mossRow.removeFromLeft (150));
            mossRow.removeFromLeft (6);
            mossLabel.setBounds (mossRow);
            r.removeFromBottom (4);

            formatsViewport.setBounds (r);
            const int colW = (r.getWidth() - 14) / 2;
            const int rows = (formatToggles.size() + 1) / 2;
            formatsHolder.setSize (r.getWidth() - 12, rows * 22);
            for (int i = 0; i < formatToggles.size(); ++i)
                formatToggles[i]->setBounds ((i % 2) * colW, (i / 2) * 22, colW, 22);

            logArea.removeFromTop (6);
            logEditor.setBounds (logArea);
        }
    }

    //==============================================================================
    void MainComponent::updatePluginList()
    {
        auto* settingsFile = properties.getUserSettings();
        favouritePlugins = juce::StringArray::fromLines (settingsFile->getValue ("favouritePlugins"));
        favouritePlugins.removeEmptyStrings();

        auto all = engine.getKnownPluginList().getTypes();
        std::sort (all.begin(), all.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
        {
            if (a.isInstrument != b.isInstrument) return a.isInstrument;
            return a.name.compareIgnoreCase (b.name) < 0;
        });

        // format filter: the formats that were found
        auto formatLabel = [] (const juce::String& f) { return f == "AudioUnit" ? juce::String ("AU") : f; };
        const auto wantedFormat = settingsFile->getValue ("pluginFormatFilter");
        juce::StringArray formats;
        for (auto& d : all)
            formats.addIfNotAlreadyThere (d.pluginFormatName);
        formats.sort (true);
        formatFilterBox.clear (juce::dontSendNotification);
        formatFilterBox.addItem ("Todos", 1);
        for (int i = 0; i < formats.size(); ++i)
            formatFilterBox.addItem ("Solo " + formatLabel (formats[i]), 2 + i);
        const int formatIndex = formats.indexOf (wantedFormat);
        formatFilterBox.setSelectedId (formatIndex >= 0 ? formatIndex + 2 : 1, juce::dontSendNotification);
        const auto filter = formatIndex >= 0 ? formats[formatIndex] : juce::String();

        const auto current = engine.getPluginDescription().createIdentifierString();
        pluginTypes.clear();
        pluginBox.clear (juce::dontSendNotification);

        auto add = [&] (const juce::PluginDescription& d)
        {
            pluginTypes.add (d);
            pluginBox.addItem (filter.isNotEmpty() ? d.name : d.name + "  (" + formatLabel (d.pluginFormatName) + ")", pluginTypes.size());
            if (d.createIdentifierString() == current && engine.getPlugin() != nullptr)
                pluginBox.setSelectedId (pluginTypes.size(), juce::dontSendNotification);
        };
        auto passes = [&] (const juce::PluginDescription& d) { return filter.isEmpty() || d.pluginFormatName == filter; };
        auto isFavourite = [this] (const juce::PluginDescription& d) { return favouritePlugins.contains (d.createIdentifierString()); };

        bool header = false;
        for (auto& d : all)
            if (passes (d) && isFavourite (d))
            {
                if (! header) { pluginBox.addSectionHeading ("Favoritos"); header = true; }
                add (d);
            }

        header = false;
        for (auto& d : all)
            if (passes (d) && ! isFavourite (d) && d.isInstrument)
            {
                if (! header) { pluginBox.addSectionHeading ("Instrumentos"); header = true; }
                add (d);
            }

        header = false;
        for (auto& d : all)
            if (passes (d) && ! isFavourite (d) && ! d.isInstrument)
            {
                if (! header) { pluginBox.addSectionHeading ("Otros (efectos)"); header = true; }
                add (d);
            }

        pluginBox.setTextWhenNothingSelected (pluginTypes.isEmpty() && ! all.isEmpty() ? "No hay plugins " + formatLabel (filter)
                                                                                       : juce::String ("Elige un instrumento..."));
        updateFavouriteButton();
    }

    void MainComponent::toggleFavourite()
    {
        if (engine.getPlugin() == nullptr)
            return;
        const auto id = engine.getPluginDescription().createIdentifierString();
        if (favouritePlugins.contains (id)) favouritePlugins.removeString (id);
        else                                favouritePlugins.add (id);
        properties.getUserSettings()->setValue ("favouritePlugins", favouritePlugins.joinIntoString ("\n"));
        properties.saveIfNeeded();
        updatePluginList();
    }

    void MainComponent::updateFavouriteButton()
    {
        const bool loaded = engine.getPlugin() != nullptr;
        favouriteButton.setEnabled (loaded && ! isHardwareMode());
        const bool fav = loaded && favouritePlugins.contains (engine.getPluginDescription().createIdentifierString());
        favouriteButton.setToggleState (fav, juce::dontSendNotification);
        favouriteButton.setTooltip (! loaded ? "Carga un plugin para marcarlo como favorito"
                                             : (fav ? "Quitar de favoritos" : "Añadir a favoritos: aparecerá al principio de la lista"_u8));
    }

    void MainComponent::loadSelectedPlugin()
    {
        const int index = pluginBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (index, pluginTypes.size()))
            loadPluginDescription (pluginTypes[index]);
    }

    void MainComponent::loadPluginDescription (const juce::PluginDescription& description)
    {
        pluginWindow.reset();
        statusLabel.setText ("Cargando " + description.name + "...", juce::dontSendNotification);

        // written to disk before loading: if the plug-in takes the app down, the next start knows it
        auto* settingsFile = properties.getUserSettings();
        settingsFile->setValue ("pluginLoadInProgress", description.createIdentifierString());
        settingsFile->saveIfNeeded();

        juce::Component::SafePointer<MainComponent> self (this);
        engine.loadPlugin (description, [self, description] (bool ok, const juce::String& error)
        {
            if (self == nullptr)
                return;

            self->properties.getUserSettings()->removeValue ("pluginLoadInProgress");

            if (! ok)
            {
                self->appendLog ("Error al cargar " + description.name + ": " + error);
                self->statusLabel.setText ({}, juce::dontSendNotification);
                return;
            }

            self->properties.getUserSettings()->setValue ("lastPlugin", description.createIdentifierString());
            self->pluginLoaded();
        });
    }

    void MainComponent::pluginLoaded()
    {
        auto* plugin = engine.getPlugin();
        if (plugin == nullptr)
            return;

        appendLog ("Plugin cargado: " + plugin->getName()
                   + (plugin->getLatencySamples() > 0 ? " (latencia " + juce::String (plugin->getLatencySamples()) + " muestras)" : juce::String()));
        statusLabel.setText (plugin->getName(), juce::dontSendNotification);

        for (int i = 0; i < pluginTypes.size(); ++i)
            if (pluginTypes[i].createIdentifierString() == engine.getPluginDescription().createIdentifierString())
                pluginBox.setSelectedId (i + 1, juce::dontSendNotification);

        // name the instrument after the plug-in if the user hasn't chosen one
        if (nameEditor.getText().isEmpty() || nameEditor.getText() == "Wavethings Instrument")
            nameEditor.setText (plugin->getName(), false);

        // restore MIDI mappings of this plug-in
        if (auto xml = properties.getUserSettings()->getXmlValue (mappingKeyForCurrentPlugin()))
            engine.getMidiLearn().fromXml (*xml);

        updateProgramList();
        mappedControls->refresh();
        updateFavouriteButton();
        showPluginEditor();
    }

    void MainComponent::updateProgramList()
    {
        programBox.clear (juce::dontSendNotification);
        if (auto* p = engine.getPlugin())
        {
            for (int i = 0; i < p->getNumPrograms(); ++i)
            {
                auto name = p->getProgramName (i).trim();
                programBox.addItem (juce::String (i + 1) + ". " + (name.isEmpty() ? juce::String ("Programa") : name), i + 1);
            }
            if (p->getNumPrograms() > 0)
                programBox.setSelectedItemIndex (juce::jmax (0, p->getCurrentProgram()), juce::dontSendNotification);
        }
    }

    void MainComponent::showPluginEditor()
    {
        if (auto* p = engine.getPlugin())
        {
            if (pluginWindow != nullptr)
            {
                pluginWindow->toFront (true);
                return;
            }
            pluginWindow = std::make_unique<PluginWindow> (*p, [this]
            {
                // closing the interface ends "learn from the plug-in"
                juce::MessageManager::callAsync ([this] { pluginWindow.reset(); engine.getMidiLearn().setCapturing (false); });
            });
        }
    }

    void MainComponent::showPluginScanner()
    {
        if (scannerWindow != nullptr)
        {
            scannerWindow->toFront (true);
            return;
        }

        struct ScannerWindow : public juce::DocumentWindow
        {
            ScannerWindow (std::function<void()> onClose)
                : DocumentWindow ("Plugins", Colours::panel, DocumentWindow::closeButton), closeCallback (std::move (onClose)) {}
            void closeButtonPressed() override { closeCallback(); }
            std::function<void()> closeCallback;
        };

        auto deadMansPedal = properties.getUserSettings()->getFile().getSiblingFile ("RecentlyCrashedPluginsList");
        auto* list = new juce::PluginListComponent (engine.getFormatManager(), engine.getKnownPluginList(), deadMansPedal,
                                                    properties.getUserSettings(), true);
        list->setSize (720, 520);

        auto window = std::make_unique<ScannerWindow> ([this] { juce::MessageManager::callAsync ([this] { scannerWindow.reset(); }); });
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (list, true);
        window->setResizable (true, false);
        window->centreWithSize (720, 520);
        window->setVisible (true);
        scannerWindow = std::move (window);

        appendLog ("Pulsa \"Options... > Scan for new or updated VST3 plug-ins\" en la ventana de plugins. "
                   "Cada plugin se analiza en un proceso aparte: si alguno falla o tarda más de "_u8
                   + juce::String (PluginScanner::timeoutSeconds) + " s, se aparta a la lista negra y el escaneo sigue.");
    }

    void MainComponent::showAudioSettings()
    {
        auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 64, 2, 2, true, false, true, false);
        selector->setSize (520, 460);

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (selector);
        o.dialogTitle = "Audio y MIDI";
        o.dialogBackgroundColour = Colours::panel;
        o.componentToCentreAround = this;
        o.useNativeTitleBar = true;
        o.resizable = true;
        o.launchAsync();
    }

    juce::String MainComponent::mappingKeyForCurrentPlugin() const
    {
        return "mappings_" + juce::String::toHexString (engine.getPluginDescription().createIdentifierString().hashCode64());
    }

    void MainComponent::saveMappings()
    {
        if (engine.getPlugin() == nullptr)
            return;
        if (auto xml = engine.getMidiLearn().toXml())
            properties.getUserSettings()->setValue (mappingKeyForCurrentPlugin(), xml.get());
    }

    //==============================================================================
    void MainComponent::handleMidi (const juce::MidiMessage& m)
    {
        if (rangeWizard != nullptr)
        {
            rangeWizard->handleMidi (m);
            return;
        }

        if (m.isController() && isHardwareMode())
        {
            hardwareControls.handleIncomingCc (m.getChannel(), m.getControllerNumber(), m.getControllerValue());
            return;
        }

        if (! m.isNoteOnOrOff())
            return;

        const int note = m.getNoteNumber();
        if (m.isNoteOn()) heldNotes.insert (note);
        else heldNotes.erase (note);

        if (m.isNoteOn() && pickingNotes && useNoteList && job == nullptr)
        {
            toggleListNote (note);
            return;
        }

        if (! m.isNoteOn() || learnMode == LearnMode::none)
            return;

        switch (learnMode)
        {
            case LearnMode::chord:
                if (heldNotes.size() >= 2)
                {
                    lowSlider.setValue (*heldNotes.begin(), juce::dontSendNotification);
                    highSlider.setValue (*heldNotes.rbegin(), juce::dontSendNotification);
                    setLearnMode (LearnMode::none);
                }
                else
                {
                    learnStatus.setText ("Nota " + NoteUtils::getNoteName (note, 3) + ": mantenla y toca la otra"_u8, juce::dontSendNotification);
                }
                break;
            case LearnMode::low:
                lowSlider.setValue (note, juce::dontSendNotification);
                if (highSlider.getValue() < note) highSlider.setValue (note, juce::dontSendNotification);
                setLearnMode (LearnMode::none);
                break;
            case LearnMode::high:
                highSlider.setValue (note, juce::dontSendNotification);
                if (lowSlider.getValue() > note) lowSlider.setValue (note, juce::dontSendNotification);
                setLearnMode (LearnMode::none);
                break;
            case LearnMode::none:
                break;
        }

        updateDerivedDisplays();
    }

    void MainComponent::setLearnMode (LearnMode mode)
    {
        learnMode = mode;
        learnChordButton.setToggleState (mode == LearnMode::chord, juce::dontSendNotification);
        learnLowButton.setToggleState (mode == LearnMode::low, juce::dontSendNotification);
        learnHighButton.setToggleState (mode == LearnMode::high, juce::dontSendNotification);

        switch (mode)
        {
            case LearnMode::chord: learnStatus.setText ("Toca a la vez la nota más baja y la más alta..."_u8, juce::dontSendNotification); break;
            case LearnMode::low:   learnStatus.setText ("Toca la nota más baja..."_u8, juce::dontSendNotification); break;
            case LearnMode::high:  learnStatus.setText ("Toca la nota más alta..."_u8, juce::dontSendNotification); break;
            case LearnMode::none:
                if (useNoteList)
                    break;   // the label shows the chosen notes
                learnStatus.setText ("Rango: " + NoteUtils::getNoteName ((int) lowSlider.getValue(), 3) + " - "
                                     + NoteUtils::getNoteName ((int) highSlider.getValue(), 3), juce::dontSendNotification);
                break;
        }
    }

    //==============================================================================
    SamplingSettings MainComponent::collectSettings() const
    {
        SamplingSettings s;
        s.lowNote = (int) lowSlider.getValue();
        s.highNote = (int) highSlider.getValue();
        s.stepSemitones = (int) stepSlider.getValue();
        s.extendToFullKeyboard = extendToggle.getToggleState();
        s.useNoteList = useNoteList;
        s.noteList = noteList;
        s.numVelocityLayers = (int) layersSlider.getValue();
        s.velocityPlayPoint = (VelocityPlayPoint) juce::jmax (0, playPointBox.getSelectedItemIndex());
        s.noteHoldSeconds = holdSlider.getValue();
        s.maxReleaseSeconds = releaseSlider.getValue();
        s.gapMs = gapSlider.getValue();
        s.thresholdDb = thresholdSlider.getValue();
        s.silenceHoldMs = silenceSlider.getValue();
        // One Shot notes mode without loop: the sample plays to the end (trigger "one shot" in the samplers)
        s.oneShot = useNoteList && ! loopToggle.getToggleState();
        s.loopEnabled = loopToggle.getToggleState();
        s.loopMode = loopModeFromMenuIndex (loopModeBox.getSelectedItemIndex());
        s.crossfadeEnabled = crossfadeToggle.getToggleState();
        s.crossfadeLinear = ! curvedShapeButton.getToggleState();
        s.loopStartPercent = loopStartSlider.getValue();
        s.loopEndPercent = loopEndSlider.getValue();
        s.crossfadeMs = crossfadeSlider.getValue();
        s.renderCrossfade = crossfadeWhereBox.getSelectedId() != 2;
        s.equaliseLoopLevel = equaliseToggle.getToggleState();
        s.trimLeadingSilence = trimToggle.getToggleState();
        s.normalize = normalizeToggle.getToggleState();
        s.stereo = stereoToggle.getToggleState();
        s.silentMode = silentToggle.getToggleState();
        s.fastRender = fastToggle.getToggleState();
        const int bits = bitDepthBox.getSelectedId();
        s.bitDepth = bits == 16 ? SampleBitDepth::pcm16 : (bits == 32 ? SampleBitDepth::float32 : SampleBitDepth::pcm24);
        s.instrumentName = nameEditor.getText().trim().isEmpty() ? juce::String ("Wavethings Instrument") : nameEditor.getText().trim();
        s.creator = creatorEditor.getText().trim();
        s.category = categoryEditor.getText().trim();
        s.outputFolder = outputFolder;
        s.convertWithMossExecutable = mossExecutable;
        s.exportFormats.clear();
        for (int i = 0; i < formatToggles.size(); ++i)
            if (formatToggles[i]->getToggleState())
                s.exportFormats.add (formatIds[i]);

        s.maxWaitForSilenceSeconds = maxWaitSlider.getValue();
        s.source = isHardwareMode() ? SoundSource::hardware : SoundSource::plugin;
        s.midiChannel = channelBox.getSelectedId() > 0 ? channelBox.getSelectedId() : 1;
        s.hardwareControlValues = hardwareControls.getValuesToSend();
        {
            const int outIndex = midiOutBox.getSelectedId() - 1;
            s.midiOutputId = juce::isPositiveAndBelow (outIndex, midiOutputIds.size()) ? midiOutputIds[outIndex] : wantedMidiOutputId;
        }
        const int inputId = inputBox.getSelectedId() > 0 ? inputBox.getSelectedId() : wantedInputId;
        s.inputChannel = inputId % 1000;
        s.inputStereo = inputId < 2000;
        s.monitorInput = monitorToggle.getToggleState();
        s.extraLatencyMs = latencySlider.getValue();
        if (s.source == SoundSource::hardware)
            s.fastRender = false;

        s.batchEnabled = batchToggle.getToggleState();
        s.programMethod = (methodBox.getSelectedId() == 2 && s.source == SoundSource::plugin) ? ProgramChangeMethod::pluginPrograms : ProgramChangeMethod::midi;
        s.bankMode = (BankSelectMode) juce::jlimit (0, 3, bankModeBox.getSelectedId() - 1);
        s.bankMsbFrom = (int) msbFromSlider.getValue();
        s.bankMsbTo = (int) msbToSlider.getValue();
        s.bankLsbFrom = (int) lsbFromSlider.getValue();
        s.bankLsbTo = (int) lsbToSlider.getValue();
        s.programFrom = (int) programFromSlider.getValue();
        s.programTo = (int) programToSlider.getValue();
        s.programChangeWaitMs = waitSlider.getValue();
        s.skipSilentPrograms = skipSilentToggle.getToggleState();
        s.programNames = programNames;
        return s;
    }

    void MainComponent::applySettings (const SamplingSettings& s)
    {
        lowSlider.setValue (s.lowNote, juce::dontSendNotification);
        highSlider.setValue (s.highNote, juce::dontSendNotification);

        maxWaitSlider.setValue (s.maxWaitForSilenceSeconds, juce::dontSendNotification);
        (s.source == SoundSource::hardware ? sourceHardwareButton : sourcePluginButton).setToggleState (true, juce::dontSendNotification);
        channelBox.setSelectedId (s.midiChannel, juce::dontSendNotification);
        wantedMidiOutputId = s.midiOutputId;
        wantedInputId = (s.inputStereo ? 1000 : 2000) + s.inputChannel;
        updateMidiOutputList();
        updateInputList();
        monitorToggle.setToggleState (s.monitorInput, juce::dontSendNotification);
        latencySlider.setValue (s.extraLatencyMs, juce::dontSendNotification);
        batchToggle.setToggleState (s.batchEnabled, juce::dontSendNotification);
        methodBox.setSelectedId (s.programMethod == ProgramChangeMethod::pluginPrograms ? 2 : 1, juce::dontSendNotification);
        bankModeBox.setSelectedId ((int) s.bankMode + 1, juce::dontSendNotification);
        msbFromSlider.setValue (s.bankMsbFrom, juce::dontSendNotification);
        msbToSlider.setValue (s.bankMsbTo, juce::dontSendNotification);
        lsbFromSlider.setValue (s.bankLsbFrom, juce::dontSendNotification);
        lsbToSlider.setValue (s.bankLsbTo, juce::dontSendNotification);
        programFromSlider.setValue (s.programFrom, juce::dontSendNotification);
        programToSlider.setValue (s.programTo, juce::dontSendNotification);
        waitSlider.setValue (s.programChangeWaitMs, juce::dontSendNotification);
        skipSilentToggle.setToggleState (s.skipSilentPrograms, juce::dontSendNotification);
        programNames = s.programNames;

        stepSlider.setValue (s.stepSemitones, juce::dontSendNotification);
        extendToggle.setToggleState (s.extendToFullKeyboard, juce::dontSendNotification);
        useNoteList = s.useNoteList;
        noteList = s.noteList;
        pickingNotes = false;
        layersSlider.setValue (s.numVelocityLayers, juce::dontSendNotification);
        playPointBox.setSelectedItemIndex ((int) s.velocityPlayPoint, juce::dontSendNotification);
        holdSlider.setValue (s.noteHoldSeconds, juce::dontSendNotification);
        releaseSlider.setValue (s.maxReleaseSeconds, juce::dontSendNotification);
        gapSlider.setValue (s.gapMs, juce::dontSendNotification);
        thresholdSlider.setValue (s.thresholdDb, juce::dontSendNotification);
        meter.setThresholdDb (s.thresholdDb);
        silenceSlider.setValue (s.silenceHoldMs, juce::dontSendNotification);
        loopToggle.setToggleState (s.loopEnabled, juce::dontSendNotification);
        loopModeBox.setSelectedItemIndex (menuIndexFromLoopMode (s.loopMode), juce::dontSendNotification);
        crossfadeToggle.setToggleState (s.crossfadeEnabled, juce::dontSendNotification);
        (s.crossfadeLinear ? (juce::Button&) linearShapeButton : (juce::Button&) curvedShapeButton).setToggleState (true, juce::dontSendNotification);
        loopStartSlider.setValue (s.loopStartPercent, juce::dontSendNotification);
        loopEndSlider.setValue (s.loopEndPercent, juce::dontSendNotification);
        crossfadeSlider.setValue (s.crossfadeMs, juce::dontSendNotification);
        crossfadeWhereBox.setSelectedId (s.renderCrossfade ? 1 : 2, juce::dontSendNotification);
        equaliseToggle.setToggleState (s.equaliseLoopLevel, juce::dontSendNotification);
        trimToggle.setToggleState (s.trimLeadingSilence, juce::dontSendNotification);
        normalizeToggle.setToggleState (s.normalize, juce::dontSendNotification);
        stereoToggle.setToggleState (s.stereo, juce::dontSendNotification);
        silentToggle.setToggleState (s.silentMode, juce::dontSendNotification);
        fastToggle.setToggleState (s.fastRender, juce::dontSendNotification);
        fastToggle.setEnabled (s.silentMode);
        bitDepthBox.setSelectedId ((int) s.bitDepth, juce::dontSendNotification);
        nameEditor.setText (s.instrumentName, false);
        creatorEditor.setText (s.creator, false);
        categoryEditor.setText (s.category, false);

        outputFolder = s.outputFolder;
        if (outputFolder == juce::File())
            outputFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Wavethings Autosampler");
        folderLabel.setText (outputFolder.getFullPathName(), juce::dontSendNotification);

        mossExecutable = s.convertWithMossExecutable;
        const auto moss = MossBridgeExporter::findConvertWithMoss (mossExecutable);
        mossLabel.setText (moss.existsAsFile() ? "ConvertWithMoss: " + moss.getFullPathName() : "ConvertWithMoss no instalado (formatos en gris)"_u8,
                           juce::dontSendNotification);
        mossLabel.setColour (juce::Label::textColourId, moss.existsAsFile() ? Colours::ok : Colours::textDim);

        for (int i = 0; i < formatToggles.size(); ++i)
            formatToggles[i]->setToggleState (s.exportFormats.contains (formatIds[i]), juce::dontSendNotification);

        updateLoopControls();
        updateNoteModeControls();
        updateSourceControls();
        applyHardwareSettingsToEngine();
        loadHardwareControls();
        updateBatchControls();
        updateDerivedDisplays();
        setLearnMode (LearnMode::none);
    }

    void MainComponent::toggleListNote (int note)
    {
        auto it = std::find (noteList.begin(), noteList.end(), note);
        if (it != noteList.end())
            noteList.erase (it);
        else
            noteList.push_back (note);
        std::sort (noteList.begin(), noteList.end());
        updateNoteModeControls();
        updateDerivedDisplays();
    }

    void MainComponent::updateNoteModeControls()
    {
        const bool list = useNoteList;
        rangeModeButton.setToggleState (! list, juce::dontSendNotification);
        listModeButton.setToggleState (list, juce::dontSendNotification);
        pickNotesButton.setToggleState (pickingNotes && list, juce::dontSendNotification);

        // range controls are not used in note-list mode
        for (auto* c : { (juce::Component*) &lowSlider, (juce::Component*) &highSlider, (juce::Component*) &stepSlider,
                         (juce::Component*) &extendToggle, (juce::Component*) &lowLabel, (juce::Component*) &highLabel,
                         (juce::Component*) &stepLabel })
            c->setAlpha (list ? 0.35f : 1.0f);
        for (auto* c : { (juce::Component*) &lowSlider, (juce::Component*) &highSlider, (juce::Component*) &stepSlider,
                         (juce::Component*) &extendToggle })
            c->setEnabled (! list);

        for (auto* b : { &learnChordButton, &learnLowButton, &learnHighButton, &wizardButton })
            b->setVisible (! list);
        pickNotesButton.setVisible (list);
        clearNotesButton.setVisible (list);

        if (list)
        {
            juce::StringArray names;
            for (int n : noteList)
                names.add (NoteUtils::getNoteName (n, 3));
            learnStatus.setText (noteList.empty() ? (pickingNotes ? "Pulsa en el piano (o en tu teclado MIDI) las notas a samplear"_u8
                                                                  : "Ninguna nota elegida: activa \"Elegir notas en el piano\""_u8)
                                                  : juce::String ((int) noteList.size()) + " notas: " + names.joinIntoString (", "),
                                 juce::dontSendNotification);
        }

        resized();
    }

    void MainComponent::updateDerivedDisplays()
    {
        settingsDirty = true;
        const auto s = collectSettings();
        const auto roots = s.getRootNotes();
        const auto layers = computeVelocityLayers (s.numVelocityLayers, s.velocityPlayPoint);
        {
            std::vector<int> activeKeys;
            if (s.isUsingNoteList())
                activeKeys = roots;
            else
                for (int n = juce::jmin (s.lowNote, s.highNote); n <= juce::jmax (s.lowNote, s.highNote); ++n)
                    activeKeys.push_back (n);
            keyboard.setHighlight (activeKeys, roots);
        }

        juce::String info;
        for (size_t i = 0; i < layers.size(); ++i)
        {
            if (i > 0) info << "  ";
            info << layers[i].low << "-" << layers[i].high << " (" << layers[i].played << ")";
        }
        layersInfo.setText ("Rangos (velocidad grabada): "_u8 + info, juce::dontSendNotification);

        const int total = (int) (roots.size() * layers.size());
        const int programs = (int) s.getBatchItems().size();
        const double perSample = s.noteHoldSeconds + juce::jmin (2.0, s.maxReleaseSeconds * 0.5) + (s.gapMs + s.silenceHoldMs) / 1000.0;
        const bool fast = s.silentMode && s.fastRender && s.source == SoundSource::plugin;
        const double minutesPerProgram = (total * perSample + (s.batchEnabled ? s.programChangeWaitMs / 1000.0 : 0.0)) / 60.0 / (fast ? 5.0 : 1.0);
        auto timeText = [] (double minutes)
        {
            return minutes < 1.0 ? juce::String (juce::roundToInt (minutes * 60)) + " s"
                                 : (minutes < 120.0 ? juce::String (minutes, 1) + " min" : juce::String (minutes / 60.0, 1) + " h");
        };
        summaryLabel.setText (juce::String ((int) roots.size()) + " notas x " + juce::String ((int) layers.size()) + " capas = "
                              + juce::String (total) + " muestras  (~" + timeText (minutesPerProgram) + ")",
                              juce::dontSendNotification);
        batchInfo.setText (s.batchEnabled ? juce::String (programs) + (programs == 1 ? " programa" : " programas") + " x " + juce::String (total)
                                                + " muestras = " + juce::String (programs * total) + " muestras  (~" + timeText (minutesPerProgram * programs) + ")"
                                          : juce::String ("Un solo instrumento con el sonido actual"),
                           juce::dontSendNotification);
        batchInfo.setColour (juce::Label::textColourId, s.batchEnabled ? Colours::accent : Colours::textDim);

        {
            const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
            const auto size = s.estimateSize (sr);
            auto toText = [] (juce::int64 bytes)
            {
                const double mb = (double) bytes / (1024.0 * 1024.0);
                return mb >= 1024.0 ? juce::String (mb / 1024.0, 2) + " GB"
                                    : (mb >= 10.0 ? juce::String (juce::roundToInt (mb)) : juce::String (mb, 1)) + " MB";
            };
            const int programs = (int) s.getBatchItems().size();
            sizeLabel.setText ("Tamaño estimado: "_u8 + toText (size.sampleBytes * programs) + " de WAV"
                                   + (size.totalBytes > size.sampleBytes ? ",  ~" + toText (size.totalBytes * programs) + " con los formatos" : juce::String())
                                   + (programs > 1 ? "  (" + juce::String (programs) + " programas)" : juce::String()),
                               juce::dontSendNotification);
            sizeLabel.setTooltip ("Calculado con " + juce::String (juce::roundToInt (sr)) + " Hz, " + juce::String ((int) s.bitDepth) + " bits, "
                                  + (s.stereo ? "estéreo"_u8 : juce::String ("mono")) + " y ~" + juce::String (size.secondsPerSample, 1)
                                  + " s por muestra. Los formatos que copian los samples (MPC, Ableton, Kontakt...) suman su copia; "
                                    "SFZ y Decent Sampler usan los WAV directamente."_u8);
        }

        rangeBar.setRange (s.lowNote, s.highNote);
        rangeBar.setListMode (s.isUsingNoteList());
        rangeBar.setNoteMarkers (roots);

        if (learnMode == LearnMode::none && ! s.isUsingNoteList())
            learnStatus.setText ("Rango: " + NoteUtils::getNoteName (s.lowNote, 3) + " - " + NoteUtils::getNoteName (s.highNote, 3),
                                 juce::dontSendNotification);
    }

    void MainComponent::shiftRange (int semitones)
    {
        if (useNoteList)
        {
            if (noteList.empty()) return;
            const auto [lo, hi] = std::minmax_element (noteList.begin(), noteList.end());
            semitones = juce::jlimit (21 - *lo, 108 - *hi, semitones);
            for (auto& n : noteList) n += semitones;
            updateNoteModeControls();
            updateDerivedDisplays();
            return;
        }

        const int low = (int) lowSlider.getValue(), high = (int) highSlider.getValue();
        semitones = juce::jlimit (21 - low, 108 - high, semitones);
        if (semitones == 0) return;
        lowSlider.setValue (low + semitones, juce::dontSendNotification);
        highSlider.setValue (high + semitones, juce::dontSendNotification);
        updateDerivedDisplays();
    }

    //==============================================================================
    juce::File MainComponent::getUserPresetFile() const
    {
        return properties.getUserSettings()->getFile().getSiblingFile ("SamplingPresets.xml");
    }

    void MainComponent::updatePresetBox()
    {
        userPresets.clear();
        if (auto xml = juce::XmlDocument::parse (getUserPresetFile()))
            for (auto* e : xml->getChildWithTagNameIterator ("PRESET"))
                if (auto* settingsXml = e->getChildElement (0))
                {
                    SamplingPreset p;
                    p.name = e->getStringAttribute ("name");
                    p.settings.fromXml (*settingsXml);
                    userPresets.push_back (p);
                }

        presetBox.clear (juce::dontSendNotification);
        presetBox.addSectionHeading ("Incluidos");
        for (size_t i = 0; i < builtInPresets.size(); ++i)
            presetBox.addItem (builtInPresets[i].name, 1 + (int) i);
        if (! userPresets.empty())
        {
            presetBox.addSectionHeading ("Mis presets");
            for (size_t i = 0; i < userPresets.size(); ++i)
                presetBox.addItem (userPresets[i].name, 1000 + (int) i);
        }
    }

    void MainComponent::presetSelected()
    {
        const int id = presetBox.getSelectedId();
        const SamplingPreset* preset = nullptr;
        if (id >= 1000 && id - 1000 < (int) userPresets.size()) preset = &userPresets[(size_t) (id - 1000)];
        else if (id >= 1 && id - 1 < (int) builtInPresets.size()) preset = &builtInPresets[(size_t) (id - 1)];
        if (preset == nullptr) return;

        auto s = collectSettings();
        s.copyPresetFieldsFrom (preset->settings);
        applySettings (s);
        appendLog ("Preset de sampleo: " + preset->name);
    }

    void MainComponent::showPresetMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Guardar los ajustes actuales como preset...");
        const int id = presetBox.getSelectedId();
        const bool isUser = id >= 1000 && id - 1000 < (int) userPresets.size();
        if (isUser)
        {
            menu.addItem (2, "Sobrescribir \"" + userPresets[(size_t) (id - 1000)].name + "\" con los ajustes actuales");
            menu.addItem (3, "Borrar \"" + userPresets[(size_t) (id - 1000)].name + "\"");
        }
        menu.addSeparator();
        menu.addItem (4, "Abrir la carpeta de presets", getUserPresetFile().existsAsFile());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetMenuButton),
                            [this, id, isUser] (int result)
        {
            if (result == 1)
            {
                auto* w = new juce::AlertWindow ("Guardar preset de sampleo",
                                                 "Guarda rango, paso, capas, tiempos y loop. Nombre del preset:", juce::MessageBoxIconType::NoIcon, this);
                w->addTextEditor ("name", nameEditor.getText().isNotEmpty() ? nameEditor.getText() : "Mi preset");
                w->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
                {
                    const auto name = w->getTextEditorContents ("name").trim();
                    if (r == 1 && name.isNotEmpty())
                        saveUserPreset (name);
                }), true);
            }
            else if (result == 2 && isUser)
            {
                saveUserPreset (userPresets[(size_t) (id - 1000)].name);
            }
            else if (result == 3 && isUser)
            {
                userPresets.erase (userPresets.begin() + (id - 1000));
                juce::XmlElement root ("SAMPLINGPRESETS");
                for (auto& p : userPresets)
                {
                    auto* e = root.createNewChildElement ("PRESET");
                    e->setAttribute ("name", p.name);
                    e->addChildElement (p.settings.toXml().release());
                }
                root.writeTo (getUserPresetFile());
                updatePresetBox();
            }
            else if (result == 4)
            {
                getUserPresetFile().revealToUser();
            }
        });
    }

    void MainComponent::saveUserPreset (const juce::String& name)
    {
        SamplingPreset preset;
        preset.name = name;
        preset.settings.copyPresetFieldsFrom (collectSettings());

        bool replaced = false;
        for (auto& p : userPresets)
            if (p.name == name) { p = preset; replaced = true; }
        if (! replaced)
            userPresets.push_back (preset);

        juce::XmlElement root ("SAMPLINGPRESETS");
        for (auto& p : userPresets)
        {
            auto* e = root.createNewChildElement ("PRESET");
            e->setAttribute ("name", p.name);
            e->addChildElement (p.settings.toXml().release());
        }
        getUserPresetFile().getParentDirectory().createDirectory();
        root.writeTo (getUserPresetFile());

        updatePresetBox();
        for (size_t i = 0; i < userPresets.size(); ++i)
            if (userPresets[i].name == name)
                presetBox.setSelectedId (1000 + (int) i, juce::dontSendNotification);
        appendLog ("Preset guardado: " + name);
    }

    void MainComponent::updateLoopControls()
    {
        const bool oneShot = useNoteList && ! loopToggle.getToggleState();
        const auto mode = loopModeFromMenuIndex (loopModeBox.getSelectedItemIndex());
        const bool looping = loopToggle.getToggleState();
        const bool penrose = mode == LoopMode::penrose;
        const bool crossfade = looping && (crossfadeToggle.getToggleState() || penrose);

        loopModeBox.setEnabled (looping);
        loopStartSlider.setEnabled (looping);
        loopEndSlider.setEnabled (looping);
        crossfadeToggle.setEnabled (looping && ! penrose);
        linearShapeButton.setEnabled (crossfade && ! penrose);
        curvedShapeButton.setEnabled (crossfade && ! penrose);
        crossfadeSlider.setEnabled (crossfade);
        crossfadeWhereBox.setEnabled (crossfade && ! penrose);
        equaliseToggle.setEnabled (looping && ! penrose);

        for (auto* l : { &loopStartLabel, &loopEndLabel })
            l->setAlpha (looping ? 1.0f : 0.4f);
        crossfadeLabel.setAlpha (crossfade ? 1.0f : 0.4f);
        crossfadeWhereLabel.setAlpha (crossfadeWhereBox.isEnabled() ? 1.0f : 0.4f);
        equaliseToggle.setAlpha (equaliseToggle.isEnabled() ? 1.0f : 0.4f);

        juce::String help;
        if (oneShot)
            help = "One Shot sin loop: cada muestra se reproduce entera aunque sueltes la tecla. Activa Loop si quieres que se repita."_u8;
        else if (! looping)
            help = "Sin loop: la muestra se reproduce hasta el final o hasta soltar la tecla."_u8;
        else switch (mode)
        {
            case LoopMode::backward: help = "Backward: la zona del loop se repite al revés (renderizado en el audio, compatible con todos los samplers)."_u8; break;
            case LoopMode::pingPong: help = "Ping-Pong: el loop va hacia delante y hacia atrás (renderizado, compatible con todos los samplers)."_u8; break;
            case LoopMode::penrose:  help = "Penrose: analiza el timbre de la zona y resintetiza un loop perfecto con el mismo carácter, fundido con el original."_u8; break;
            default:                 help = "Forward: loop hacia delante entre dos cruces por cero con la mejor coincidencia de forma de onda."_u8; break;
        }
        if (looping && crossfade && ! penrose)
            help << (crossfadeWhereBox.getSelectedId() == 2 ? " Crossfade aplicado por el sampler."_u8 : " Crossfade grabado en el audio."_u8);
        loopHelp.setText (help, juce::dontSendNotification);
    }

    //==============================================================================
    void MainComponent::startOrStopSampling()
    {
        if (job != nullptr)
        {
            job->signalThreadShouldExit();
            startButton.setEnabled (false);
            statusLabel.setText ("Deteniendo...", juce::dontSendNotification);
            return;
        }

        if (isHardwareMode())
        {
            applyHardwareSettingsToEngine();
            if (engine.getMidiOutputIdentifier().isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Wavethings Autosampler",
                                                        "Elige la salida MIDI conectada a tu instrumento hardware."_u8);
                return;
            }
            if (engine.getInputChannelNames().isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Wavethings Autosampler",
                                                        "La interfaz de audio no tiene entradas. Elige otra en Audio / MIDI..."_u8);
                return;
            }
        }
        else if (engine.getPlugin() == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Wavethings Autosampler",
                                                    "Primero carga un instrumento (VST3/AU) o cambia a Hardware."_u8);
            return;
        }

        auto s = collectSettings();
        if (s.exportFormats.isEmpty())
            appendLog ("Ningún formato seleccionado: solo se guardarán los WAV."_u8);

        if (! outputFolder.createDirectory())
        {
            appendLog ("No se puede crear la carpeta de salida " + outputFolder.getFullPathName());
            return;
        }

        saveState();
        setLearnMode (LearnMode::none);
        engine.getMidiLearn().setLearning (false);
        waveform.clear();
        progressValue = 0.0;

        job = std::make_unique<SamplingJob> (engine, s);

        job->onLog = [this] (const juce::String& line) { appendLog (line); };
        job->onProgress = [this] (const SamplingJob::Progress& p)
        {
            const double inProgram = p.total > 0 ? (double) (p.index - 1) / p.total : 0.0;
            progressValue = (p.programIndex - 1 + inProgram) / juce::jmax (1, p.programTotal);
            keyboard.setRecordingNote (p.note);
            const auto program = p.programTotal > 1 ? "[" + juce::String (p.programIndex) + "/" + juce::String (p.programTotal) + " " + p.programName + "]  " : juce::String();
            statusLabel.setText (program + juce::String (p.index) + "/" + juce::String (p.total) + "  " + p.status, juce::dontSendNotification);
        };
        job->onSampleCaptured = [this] (std::shared_ptr<ProcessedSample> sample, int note, int velocity)
        {
            waveform.setSample (sample, NoteUtils::getNoteName (note, 3) + "  vel " + juce::String (velocity));
        };
        job->onFinished = [this] (bool success, const Instrument& instrument)
        {
            samplingFinished (success, instrument);
        };

        setControlsEnabled (false);
        startButton.setButtonText ("DETENER");
        startButton.setColour (juce::TextButton::buttonColourId, Colours::danger);
        appendLog ("-----");
        job->startThread (juce::Thread::Priority::high);
    }

    void MainComponent::samplingFinished (bool success, const Instrument& instrument)
    {
        lastInstrumentFolder = (job != nullptr && job->getOutputFolder() != juce::File()) ? job->getOutputFolder() : instrument.folder;
        progressValue = success ? 1.0 : progressValue;
        keyboard.setRecordingNote (-1);
        statusLabel.setText (success ? "Terminado: " + lastInstrumentFolder.getFullPathName()
                                     : juce::String ("Sampleo detenido"), juce::dontSendNotification);

        setControlsEnabled (true);
        startButton.setEnabled (true);
        startButton.setButtonText ("EMPEZAR A SAMPLEAR");
        startButton.setColour (juce::TextButton::buttonColourId, Colours::accent);

        // the job calls us from inside its own thread function's last message: delete it afterwards
        juce::MessageManager::callAsync ([this] { if (job != nullptr) { job->stopThread (5000); job.reset(); } });
    }

    void MainComponent::setControlsEnabled (bool enabled)
    {
        for (auto* c : { (juce::Component*) &rangeSection, (juce::Component*) &thresholdSlider, (juce::Component*) &silenceSlider,
                         (juce::Component*) &loopModeBox, (juce::Component*) &loopToggle,
                         (juce::Component*) &crossfadeToggle, (juce::Component*) &linearShapeButton, (juce::Component*) &curvedShapeButton,
                         (juce::Component*) &rangeModeButton, (juce::Component*) &listModeButton, (juce::Component*) &loopStartSlider,
                         (juce::Component*) &loopEndSlider, (juce::Component*) &crossfadeSlider, (juce::Component*) &crossfadeWhereBox,
                         (juce::Component*) &equaliseToggle, (juce::Component*) &nameEditor,
                         (juce::Component*) &creatorEditor, (juce::Component*) &categoryEditor, (juce::Component*) &folderButton,
                         (juce::Component*) &bitDepthBox, (juce::Component*) &stereoToggle, (juce::Component*) &normalizeToggle,
                         (juce::Component*) &trimToggle, (juce::Component*) &formatsViewport, (juce::Component*) &pluginBox,
                         (juce::Component*) &scanButton, (juce::Component*) &unloadButton, (juce::Component*) &audioButton,
                         (juce::Component*) &programBox, (juce::Component*) &silentToggle, (juce::Component*) &rangeBar,
                         (juce::Component*) &rangeLeftButton, (juce::Component*) &rangeRightButton, (juce::Component*) &presetBox,
                         (juce::Component*) &presetMenuButton, (juce::Component*) &sourcePluginButton, (juce::Component*) &sourceHardwareButton,
                         (juce::Component*) &midiOutBox, (juce::Component*) &channelBox, (juce::Component*) &inputBox,
                         (juce::Component*) &latencySlider, (juce::Component*) &batchSection, (juce::Component*) &maxWaitSlider,
                         (juce::Component*) &noiseButton, (juce::Component*) &hardwareControls })
            c->setEnabled (enabled);
        fastToggle.setEnabled (enabled && silentToggle.getToggleState() && ! isHardwareMode());

        meter.setInterceptsMouseClicks (enabled, enabled);
        if (enabled)
        {
            updateLoopControls();
            updateBatchControls();
        }
    }

    void MainComponent::appendLog (const juce::String& line)
    {
        logEditor.moveCaretToEnd();
        logEditor.insertTextAtCaret (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + line + "\n");
    }

    void MainComponent::chooseOutputFolder()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Carpeta de salida", outputFolder);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File())
            {
                outputFolder = fc.getResult();
                folderLabel.setText (outputFolder.getFullPathName(), juce::dontSendNotification);
                settingsDirty = true;
            }
        });
    }

    void MainComponent::chooseConvertWithMoss()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Ejecutable de ConvertWithMoss", juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory));
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File())
                return;
            mossExecutable = fc.getResult();
            const auto found = MossBridgeExporter::findConvertWithMoss (mossExecutable);
            mossLabel.setText (found.existsAsFile() ? "ConvertWithMoss: " + found.getFullPathName() : "No es un ejecutable válido"_u8, juce::dontSendNotification);
            mossLabel.setColour (juce::Label::textColourId, found.existsAsFile() ? Colours::ok : Colours::danger);
            settingsDirty = true;
        });
    }

    //==============================================================================
    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        if (source == &engine.getDeviceManager())
        {
            // audio device changed: its inputs may be different
            updateInputList();
            return;
        }

        // the list of known plug-ins changed (scan)
        updatePluginList();
        if (auto xml = engine.getKnownPluginList().createXml())
            properties.getUserSettings()->setValue ("pluginList", xml.get());
        properties.saveIfNeeded();
    }

    void MainComponent::timerCallback()
    {
        repaint (0, 0, getWidth(), 36);   // "silent mode" badge

        if (settingsDirty && job == nullptr)
        {
            settingsDirty = false;
            if (auto xml = collectSettings().toXml())
                properties.getUserSettings()->setValue ("samplingSettings", xml.get());
        }
    }

    void MainComponent::saveState()
    {
        auto* settingsFile = properties.getUserSettings();
        if (auto xml = collectSettings().toXml())
            settingsFile->setValue ("samplingSettings", xml.get());
        if (auto xml = engine.getDeviceManager().createStateXml())
            settingsFile->setValue ("audioDeviceState", xml.get());
        saveMappings();
        properties.saveIfNeeded();
    }
}
