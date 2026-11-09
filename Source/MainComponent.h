#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <set>
#include <array>
#include "Core/AudioEngine.h"
#include "Core/SamplingJob.h"
#include "Core/Settings.h"
#include "UI/WavethingsLookAndFeel.h"
#include "UI/LevelMeter.h"
#include "UI/WaveformView.h"
#include "UI/MappedControlsPanel.h"
#include "UI/HardwareControlsPanel.h"
#include "UI/PluginWindow.h"
#include "UI/RangeWizard.h"
#include "Core/Text.h"

namespace wt
{
    /** Keyboard that shows the sampling range and the notes that will be recorded. */
    class RangeKeyboard : public juce::MidiKeyboardComponent
    {
    public:
        RangeKeyboard (juce::MidiKeyboardState& s) : juce::MidiKeyboardComponent (s, horizontalKeyboard) {}
        /** Keys drawn as "active" and the notes marked as recorded (orange bar). */
        void setHighlight (std::vector<int> activeKeys, std::vector<int> roots);
        void setRecordingNote (int note) { recordingNote = note; repaint(); }

        /** When set, a click on a key calls this (the key still plays). */
        std::function<void (int note)> onKeyClicked;

    protected:
        bool mouseDownOnKey (int note, const juce::MouseEvent& e) override
        {
            if (onKeyClicked != nullptr)
                onKeyClicked (note);
            return juce::MidiKeyboardComponent::mouseDownOnKey (note, e);
        }

        void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour lineColour, juce::Colour textColour) override;
        void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour noteFillColour) override;

    private:
        bool isRoot (int note) const { return std::find (roots.begin(), roots.end(), note) != roots.end(); }
        bool isActive (int note) const { return note >= 0 && note < 128 && active[(size_t) note]; }
        int recordingNote = -1;
        std::vector<int> roots;
        std::array<bool, 128> active {};
    };

    /** Bar above the keyboard with the sampling range: drag the ends to change the lowest/highest
        note, drag the middle to move the whole range. It has the same x and width as the keyboard. */
    class RangeBar : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit RangeBar (RangeKeyboard& k) : keyboard (k)
        {
            setTooltip ("Arrastra los extremos para cambiar el rango, o el centro para moverlo entero. Doble clic: C1-C7"_u8);
            setRepaintsOnMouseActivity (true);
        }

        void setRange (int low, int high) { lowNote = low; highNote = high; repaint(); }
        void setNoteMarkers (std::vector<int> notes) { markers = std::move (notes); repaint(); }
        void setListMode (bool list) { listMode = list; setMouseCursor (list ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor); repaint(); }
        std::function<void (int low, int high)> onRangeChanged;

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragMode = DragMode::none; }
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        enum class DragMode { none, low, high, move };
        juce::Range<float> getNoteSpan (int note) const;
        int noteAtX (float x) const;
        juce::Rectangle<float> getBarArea() const;
        DragMode hitTestMode (float x) const;

        RangeKeyboard& keyboard;
        int lowNote = 48, highNote = 72;
        int dragStartLow = 0, dragStartHigh = 0, dragStartNote = 0;
        DragMode dragMode = DragMode::none;
        bool listMode = false;
        std::vector<int> markers;
    };

    /** Star that marks the loaded plug-in as a favourite. */
    class StarButton : public juce::Button
    {
    public:
        StarButton() : juce::Button ("Favorito") {}

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto b = getLocalBounds().toFloat().reduced (3.0f);
            const float size = juce::jmin (b.getWidth(), b.getHeight());
            b = b.withSizeKeepingCentre (size, size);
            juce::Path star;
            star.addStar (b.getCentre(), 5, size * 0.21f, size * 0.5f, 0.0f);
            const bool on = getToggleState();
            if (! isEnabled()) g.setOpacity (0.35f);
            g.setColour (on ? Colours::accent : (over ? Colours::text : Colours::textDim));
            if (on) g.fillPath (star);
            else    g.strokePath (star, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };

    /** The two crossfade shapes, drawn like the icons of the MPC: straight or curved lines crossing. */
    class CrossfadeShapeButton : public juce::Button
    {
    public:
        CrossfadeShapeButton (bool isLinear) : juce::Button (isLinear ? "Lineal" : "Curvado"), linear (isLinear)
        {
            setClickingTogglesState (true);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = getToggleState();
            g.setColour (on ? Colours::accent : (over ? Colours::panelLight.brighter (0.08f) : Colours::panelLight));
            if (! isEnabled()) g.setOpacity (0.4f);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (on ? Colours::accent : Colours::outline);
            g.drawRoundedRectangle (b, 5.0f, 1.0f);

            auto a = b.reduced (b.getWidth() * 0.2f, b.getHeight() * 0.22f);
            juce::Path fadeIn, fadeOut;
            const int steps = 24;
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float) i / steps;
                const float in = linear ? t : std::sin (t * juce::MathConstants<float>::halfPi);
                const float out = linear ? 1.0f - t : std::cos (t * juce::MathConstants<float>::halfPi);
                const juce::Point<float> pIn (a.getX() + t * a.getWidth(), a.getBottom() - in * a.getHeight());
                const juce::Point<float> pOut (a.getX() + t * a.getWidth(), a.getBottom() - out * a.getHeight());
                if (i == 0) { fadeIn.startNewSubPath (pIn); fadeOut.startNewSubPath (pOut); }
                else { fadeIn.lineTo (pIn); fadeOut.lineTo (pOut); }
            }
            g.setColour (on ? juce::Colours::black : Colours::text);
            if (! isEnabled()) g.setOpacity (0.4f);
            g.strokePath (fadeIn, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.strokePath (fadeOut, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::ignoreUnused (down);
        }

    private:
        bool linear;
    };

    //==============================================================================
    class MainComponent : public juce::Component,
                          private juce::ChangeListener,
                          private juce::Timer
    {
    public:
        explicit MainComponent (juce::ApplicationProperties& properties);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isSampling() const { return job != nullptr; }
        void saveState();

        // Interface size (100 % = 1.0). The window applies it as a transform.
        float getUiScale() const { return uiScale; }
        void setUiScale (float newScale);
        std::function<void (float)> onScaleChanged;
        bool keyPressed (const juce::KeyPress& key) override;
        static constexpr int baseWidth = 1320, baseHeight = 920;

    private:
        enum class LearnMode { none, chord, low, high };

        // Setup ------------------------------------------------------------------------
        void createPluginSection();
        void createRangeSection();
        void createThresholdSection();
        void createLoopSection();
        void createOutputSection();
        void createTransport();
        void createBatchSection();

        // Hardware / batch ---------------------------------------------------------------------
        void updateSourceControls();
        void updateMidiOutputList();
        void updateInputList();
        void applyHardwareSettingsToEngine();
        void updateBatchControls();
        void sendProgramNow();
        void editProgramNames();
        void measureNoiseFloor();
        juce::StringArray programNames, midiOutputIds;
        juce::String wantedMidiOutputId;
        int wantedInputId = 1000;

        // Plug-ins -----------------------------------------------------------------------
        void updatePluginList();
        void loadSelectedPlugin();
        void loadPluginDescription (const juce::PluginDescription& description);
        void pluginLoaded();
        void updateProgramList();
        void showPluginEditor();
        void showPluginScanner();
        void showAudioSettings();
        juce::String mappingKeyForCurrentPlugin() const;
        void saveMappings();

        // MIDI ---------------------------------------------------------------------------
        void handleMidi (const juce::MidiMessage& message);
        void setLearnMode (LearnMode mode);

        // Settings ------------------------------------------------------------------------
        SamplingSettings collectSettings() const;
        void applySettings (const SamplingSettings& s);
        void updateDerivedDisplays();
        void updateLoopControls();
        void shiftRange (int semitones);

        // Sampling presets -----------------------------------------------------------------
        void updatePresetBox();
        void presetSelected();
        void showPresetMenu();
        void saveUserPreset (const juce::String& name);
        juce::File getUserPresetFile() const;
        std::vector<SamplingPreset> builtInPresets, userPresets;

        // Sampling ------------------------------------------------------------------------
        void startOrStopSampling();
        void samplingFinished (bool success, const Instrument& instrument);
        void setControlsEnabled (bool enabled);
        void appendLog (const juce::String& line);
        void chooseOutputFolder();
        void chooseConvertWithMoss();

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        // Members ---------------------------------------------------------------------------
        juce::ApplicationProperties& properties;
        WavethingsLookAndFeel lookAndFeel;
        AudioEngine engine;
        juce::Array<juce::PluginDescription> pluginTypes;
        std::unique_ptr<PluginWindow> pluginWindow;
        std::unique_ptr<juce::DocumentWindow> scannerWindow;
        std::unique_ptr<SamplingJob> job;
        juce::Component::SafePointer<RangeWizard> rangeWizard;
        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::TooltipWindow tooltips { this, 600 };

        LearnMode learnMode = LearnMode::none;
        std::set<int> heldNotes;
        double progressValue = 0.0;
        juce::File outputFolder, mossExecutable;
        bool settingsDirty = false;

        // Plug-in section
        Section pluginSection { "Instrumento" };
        juce::TextButton sourcePluginButton, sourceHardwareButton;
        juce::ComboBox formatFilterBox;
        StarButton favouriteButton;
        juce::StringArray favouritePlugins;
        void toggleFavourite();
        void updateFavouriteButton();
        juce::Label midiOutLabel, channelLabel, inputLabel, latencyLabel;
        juce::ComboBox midiOutBox, channelBox, inputBox;
        juce::ToggleButton monitorToggle;
        juce::Slider latencySlider;
        bool isHardwareMode() const { return sourceHardwareButton.getToggleState(); }
        juce::ComboBox pluginBox, programBox;
        juce::TextButton scanButton, editorButton, audioButton, unloadButton;
        juce::Label pluginLabel, programLabel;

        // Range section
        Section rangeSection { "Rango y capas" };
        juce::Label lowLabel, highLabel, stepLabel, layersLabel, playPointLabel, layersInfo, summaryLabel, sizeLabel, learnStatus;
        juce::Slider lowSlider, highSlider, stepSlider, layersSlider;
        juce::TextButton learnChordButton, learnLowButton, learnHighButton, wizardButton;
        juce::ToggleButton extendToggle;
        juce::ComboBox playPointBox;
        juce::Label holdLabel, releaseLabel, gapLabel;

        // One Shot: individual notes instead of a range
        juce::Label notesModeLabel;
        juce::TextButton rangeModeButton, listModeButton, pickNotesButton, clearNotesButton;
        std::vector<int> noteList;
        bool useNoteList = false;
        bool pickingNotes = false;
        void toggleListNote (int note);
        void updateNoteModeControls();
        juce::Slider holdSlider, releaseSlider, gapSlider;

        // Threshold section
        Section thresholdSection { "Umbral de silencio" };
        LevelMeter meter;
        juce::Label thresholdLabel, silenceLabel;
        juce::Slider thresholdSlider, silenceSlider, maxWaitSlider;
        juce::Label maxWaitLabel;
        juce::TextButton noiseButton;

        // Loop section
        Section loopSection { "Loop" };
        juce::TextButton loopToggle, crossfadeToggle;
        CrossfadeShapeButton linearShapeButton { true }, curvedShapeButton { false };
        juce::ComboBox loopModeBox;
        juce::Label loopModeLabel, loopStartLabel, loopEndLabel, crossfadeLabel, loopHelp;
        juce::Slider loopStartSlider, loopEndSlider, crossfadeSlider;
        juce::Label crossfadeWhereLabel;
        juce::ComboBox crossfadeWhereBox;
        juce::ToggleButton equaliseToggle;
        WaveformView waveform;

        // Output section
        Section outputSection { "Exportar" };
        juce::Label nameLabel, creatorLabel, categoryLabel, folderLabel, bitDepthLabel, formatsLabel, mossLabel;
        juce::TextEditor nameEditor, creatorEditor, categoryEditor;
        juce::TextButton folderButton, mossButton, openFolderButton;
        juce::ComboBox bitDepthBox;
        juce::ToggleButton stereoToggle, normalizeToggle, trimToggle;
        juce::Viewport formatsViewport;
        juce::Component formatsHolder;
        juce::OwnedArray<juce::ToggleButton> formatToggles;
        juce::StringArray formatIds;

        // Batch: programs and banks
        Section batchSection { "Programas y bancos (lotes)" };
        juce::ToggleButton batchToggle, skipSilentToggle;
        juce::ComboBox methodBox, bankModeBox;
        juce::Label programLabelB, programToLabel, waitLabel, msbLabel, msbToLabel, lsbLabel, lsbToLabel, batchInfo;
        juce::Slider programFromSlider, programToSlider, waitSlider, msbFromSlider, msbToSlider, lsbFromSlider, lsbToSlider;
        juce::TextButton sendNowButton, namesButton;

        // Mapped controls
        Section controlsSection { "Controles del plugin (MIDI Learn)" };
        std::unique_ptr<MappedControlsPanel> mappedControls;
        HardwareControlsPanel hardwareControls;
        juce::String hardwareControlsKey;        // settings key of the MIDI output whose controls are shown
        void loadHardwareControls();
        void saveHardwareControls();

        // Keyboard + transport
        RangeKeyboard keyboard { engine.getKeyboardState() };
        RangeBar rangeBar { keyboard };
        juce::ArrowButton rangeLeftButton { "rangeLeft", 0.5, Colours::text }, rangeRightButton { "rangeRight", 0.0, Colours::text };
        juce::Label presetLabel;
        juce::ComboBox presetBox;
        juce::TextButton presetMenuButton;
        juce::TextButton startButton;
        juce::ToggleButton silentToggle, fastToggle;
        juce::ComboBox scaleBox;
        float uiScale = 1.0f;
        juce::ProgressBar progressBar { progressValue };
        juce::Label statusLabel;
        juce::TextEditor logEditor;
        juce::File lastInstrumentFolder;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
