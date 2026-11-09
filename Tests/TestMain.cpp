// Console test runner: drives the real AudioEngine + SamplingJob offline with a built-in test
// instrument, then checks the processed samples and writes every export format.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Core/AudioEngine.h"
#include "Core/SamplingJob.h"
#include "Core/SampleProcessing.h"
#include "Core/WavFile.h"
#include "Core/PluginScanner.h"
#include <set>
#include "Export/Exporter.h"
#include "Export/Exporters.h"

using namespace wt;

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what);
    void check (bool condition, const char* what) { check (condition, juce::String::fromUTF8 (what)); }

    void check (bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  [ok]   " : "  [FAIL] ") << what << std::endl;
        if (! condition)
            ++failures;
    }

    /** A small polyphonic additive synth with a release tail, used as the "plug-in". */
    class TestSynth : public juce::AudioPluginInstance
    {
    public:
        TestSynth() : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true))
        {
            // parameters for the MIDI Learn tests (they don't change the sound)
            static_cast<juce::AudioProcessor&> (*this).addParameter (new juce::AudioParameterFloat (juce::ParameterID { "cutoff", 1 }, "Cutoff", 0.0f, 1.0f, 0.5f));
            static_cast<juce::AudioProcessor&> (*this).addParameter (new juce::AudioParameterFloat (juce::ParameterID { "lfo", 1 }, "LFO Out", 0.0f, 1.0f, 0.0f));
            static_cast<juce::AudioProcessor&> (*this).addParameter (new juce::AudioParameterBool (juce::ParameterID { "chorus", 1 }, "Chorus", false));
        }

        void fillInPluginDescription (juce::PluginDescription& d) const override
        {
            d.name = "TestSynth"; d.pluginFormatName = "Internal"; d.isInstrument = true; d.uniqueId = 1234;
        }

        const juce::String getName() const override { return "TestSynth"; }
        void prepareToPlay (double sr, int) override { sampleRate = sr; voices.clear(); }
        void releaseResources() override {}
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.5; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        // 4 programs: 0-2 sound with 2, 4 and 6 harmonics, program 3 is silent
        int getNumPrograms() override { return 4; }
        int getCurrentProgram() override { return program; }
        void setCurrentProgram (int p) override { program = juce::jlimit (0, 3, p); }
        const juce::String getProgramName (int p) override { return juce::StringArray { "Soft", "Warm", "Bright", "Mute" }[p]; }
        int program = 2;
        std::vector<int> receivedProgramChanges, receivedBankMsb, receivedBankLsb;
        std::vector<std::pair<int, int>> receivedEvents;   // (program change = -1 - program, CC74 = value) in order
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            buffer.clear();
            int pos = 0;
            for (const auto meta : midi)
            {
                render (buffer, pos, meta.samplePosition);
                pos = meta.samplePosition;
                const auto m = meta.getMessage();
                if (m.isProgramChange()) receivedEvents.push_back ({ -1 - m.getProgramChangeNumber(), m.getChannel() });
                if (m.isController() && m.getControllerNumber() == 74) receivedEvents.push_back ({ m.getControllerValue(), m.getChannel() });
                if (m.isProgramChange()) { program = juce::jlimit (0, 3, m.getProgramChangeNumber()); receivedProgramChanges.push_back (m.getProgramChangeNumber()); }
                if (m.isController() && m.getControllerNumber() == 0) receivedBankMsb.push_back (m.getControllerValue());
                if (m.isController() && m.getControllerNumber() == 32) receivedBankLsb.push_back (m.getControllerValue());
                if (m.isNoteOn() && program == 3) continue;
                if (m.isNoteOn())
                    voices.push_back ({ m.getNoteNumber(), m.getFloatVelocity(), 0.0, 0.0, false, 0.0 });
                else if (m.isNoteOff() || m.isAllNotesOff())
                    for (auto& v : voices)
                        if (m.isAllNotesOff() || v.note == m.getNoteNumber())
                            v.released = true;
            }
            render (buffer, pos, buffer.getNumSamples());
            voices.erase (std::remove_if (voices.begin(), voices.end(), [] (auto& v) { return v.released && v.env < 1.0e-6; }), voices.end());
        }

    private:
        struct Voice { int note; float velocity; double phase, env; bool released; double age; };

        void render (juce::AudioBuffer<float>& buffer, int start, int end)
        {
            const double attack = 1.0 / (0.005 * sampleRate);
            const double releaseCoeff = std::exp (-1.0 / (0.15 * sampleRate));
            for (auto& v : voices)
            {
                const double freq = 440.0 * std::pow (2.0, (v.note - 69) / 12.0);
                const double inc = freq / sampleRate;
                for (int i = start; i < end; ++i)
                {
                    if (v.released) v.env *= releaseCoeff;
                    else v.env = juce::jmin (1.0, v.env + attack);

                    double s = 0.0;
                    for (int h = 1; h <= 2 * (program + 1); ++h)
                        s += std::sin (juce::MathConstants<double>::twoPi * v.phase * h) / h;
                    v.phase += inc;
                    if (v.phase >= 1.0) v.phase -= 1.0;

                    const float out = (float) (s * 0.2 * v.env * v.velocity);
                    buffer.addSample (0, i, out);
                    buffer.addSample (1, i, out * 0.9f);
                }
            }
        }

        double sampleRate = 48000.0;
        std::vector<Voice> voices;
    };

    double junctionJump (const juce::AudioBuffer<float>& a, const LoopInfo& loop)
    {
        // value jump when playback wraps from loop end to loop start, compared to the typical slope
        double jump = 0.0, slope = 1.0e-9;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
        {
            auto* d = a.getReadPointer (ch);
            jump = juce::jmax (jump, (double) std::abs (d[loop.start] - d[loop.end]));
            for (juce::int64 i = loop.start + 1; i <= loop.end; ++i)
                slope = juce::jmax (slope, (double) std::abs (d[i] - d[i - 1]));
        }
        return jump / slope;
    }

    void testNoteUtils()
    {
        std::cout << "NoteUtils" << std::endl;
        auto layers = computeVelocityLayers (4, VelocityPlayPoint::top);
        check (layers.size() == 4 && layers[0].low == 1 && layers[3].high == 127, "4 capas cubren 1-127");
        check (layers[1].low == layers[0].high + 1, "capas contiguas");
        auto roots = computeRootNotes (36, 50, 3);
        check (roots.front() == 36 && roots.back() == 50, "incluye nota más baja y más alta");
        auto ranges = computeKeyRanges (roots, 3, true);
        bool contiguous = ranges.front().low == 0 && ranges.back().high == 127;
        for (size_t i = 1; i < ranges.size(); ++i)
            contiguous = contiguous && ranges[i].low == ranges[i - 1].high + 1;
        check (contiguous, "rangos de teclas contiguos 0-127");
        check (NoteUtils::getFileSafeNoteName (60) == "C3", "60 = C3");
    }

    bool runSession (const juce::String& name, SamplingSettings settings, Instrument& result,
                     std::unique_ptr<juce::AudioPluginInstance> plugin = nullptr,
                     std::function<void (AudioEngine&)> setupEngine = nullptr, juce::File* outputFolder = nullptr)
    {
        AudioEngine engine;
        engine.setPlugin (plugin != nullptr ? std::move (plugin) : std::make_unique<TestSynth>());
        engine.prepareOffline (48000.0, 256);
        if (setupEngine != nullptr)
            setupEngine (engine);

        juce::AudioBuffer<float> out (2, 256);
        juce::MidiBuffer none;

        SamplingJob job (engine, settings);
        job.synchronousCallbacks = true;
        job.offlineRenderer = [&] (int frames)
        {
            for (int done = 0; done < frames; done += 256)
                engine.processOffline (out, none);
        };

        bool success = false;
        job.onLog = [&] (const juce::String& line) { std::cout << "    " << line << std::endl; };
        job.onFinished = [&] (bool ok, const Instrument& inst) { success = ok; result = inst; };
        std::cout << name << std::endl;
        job.run();
        if (outputFolder != nullptr)
            *outputFolder = job.getOutputFolder();
        return success;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    // started by the out-of-process scanner test as the helper process
    {
        juce::StringArray args;
        for (int i = 1; i < argc; ++i) args.add (argv[i]);
        if (auto worker = PluginScanner::startWorkerIfRequested (args.joinIntoString (" ")))
        {
            juce::MessageManager::getInstance()->runDispatchLoop();
            return 0;
        }
    }

    // out-of-process scan: --scan-test <good.vst3> <crashing.vst3> <hanging.vst3>
    if (argc > 4 && juce::String (argv[1]) == "--scan-test")
    {
        juce::VST3PluginFormat vst3;
        juce::KnownPluginList list;
        PluginScanner::install (list, juce::File::getSpecialLocation (juce::File::currentExecutableFile), 8);

        juce::OwnedArray<juce::PluginDescription> found;
        list.scanAndAddFile (argv[2], false, found, vst3);
        check (found.size() == 1, "plugin correcto encontrado fuera de proceso");

        found.clear();
        list.scanAndAddFile (argv[3], false, found, vst3);
        check (found.isEmpty() && list.getBlacklistedFiles().contains (argv[3]), "plugin que se cierra al cargar -> lista negra, la app sigue viva");

        const auto t0 = juce::Time::getMillisecondCounter();
        found.clear();
        list.scanAndAddFile (argv[4], false, found, vst3);
        check (found.isEmpty() && list.getBlacklistedFiles().contains (argv[4]), "plugin bloqueado -> tiempo agotado y lista negra");
        std::cout << "  (tiempo de espera: " << (juce::Time::getMillisecondCounter() - t0) / 1000 << " s)" << std::endl;

        found.clear();
        list.scanAndAddFile (argv[2], false, found, vst3);
        check (found.size() == 1, "el escaneo sigue funcionando después");
        list.setCustomScanner (nullptr);

        std::cout << std::endl << (failures == 0 ? "TODOS LOS TESTS OK" : juce::String (failures) + " FALLOS") << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // helper for manual testing: print a KnownPluginList with the given plug-in
    if (argc > 2 && juce::String (argv[1]) == "--plugin-list-xml")
    {
        juce::VST3PluginFormat vst3;
        juce::OwnedArray<juce::PluginDescription> found;
        vst3.findAllTypesForFile (found, argv[2]);
        juce::KnownPluginList list;
        for (auto* d : found) list.addType (*d);
        if (auto xml = list.createXml())
            std::cout << xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()) << std::endl;
        if (! found.isEmpty())
            std::cout << found[0]->createIdentifierString() << std::endl;
        return 0;
    }
    const auto outRoot = juce::File (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("test-output").getFullPathName());
    outRoot.deleteRecursively();
    outRoot.createDirectory();

    testNoteUtils();

    // Optional: real VST3 plug-in -------------------------------------------------------------
    if (argc > 3 && juce::String (argv[2]) == "--vst3")
    {
        std::cout << "VST3: " << argv[3] << std::endl;
        juce::VST3PluginFormat vst3;
        juce::OwnedArray<juce::PluginDescription> found;
        vst3.findAllTypesForFile (found, argv[3]);
        check (found.size() > 0, "plugin VST3 encontrado");
        if (found.isEmpty()) return 1;

        juce::String error;
        auto instance = vst3.createInstanceFromDescription (*found[0], 48000.0, 256, error);
        check (instance != nullptr, "plugin VST3 instanciado " + error);
        if (instance == nullptr) return 1;
        std::cout << "  " << instance->getName() << ", " << instance->getParameters().size() << " parametros" << std::endl;

        // MIDI learn: CC 74 -> parameter 0
        {
            AudioEngine e;
            e.setPlugin (vst3.createInstanceFromDescription (*found[0], 48000.0, 256, error));
            auto& ml = e.getMidiLearn();
            ml.setLearning (true);
            ml.setLearnParameter (0);
            ml.handleIncomingMidi (juce::MidiMessage::controllerEvent (1, 74, 10));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            check (ml.getMappings().size() == 1 && ml.getMappings()[0].ccNumber == 74, "MIDI Learn asigna CC 74");
            ml.handleIncomingMidi (juce::MidiMessage::controllerEvent (1, 74, 127));
            check (std::abs (ml.getParameter (0)->getValue() - 1.0f) < 0.01f, "el CC mueve el parámetro");
            ml.handleIncomingMidi (juce::MidiMessage::controllerEvent (1, 74, 0));
            check (ml.getParameter (0)->getValue() < 0.01f, "CC 0 -> parámetro al mínimo");
        }

        SamplingSettings s;
        s.lowNote = 48; s.highNote = 72; s.stepSemitones = 6; s.numVelocityLayers = 3;
        s.noteHoldSeconds = 2.0; s.maxReleaseSeconds = 4.0; s.gapMs = 50;
        s.loopMode = argc > 4 ? (LoopMode) juce::String (argv[4]).getIntValue() : LoopMode::penrose; s.crossfadeMs = 120;
        s.crossfadeEnabled = true;
        s.noteHoldSeconds = 4.0;
        s.outputFolder = outRoot; s.instrumentName = "VST3 " + instance->getName();
        s.exportFormats = { "sfz", "decentsampler", "bitwig", "ableton", "mpc", "exs", "kontakt", "sf2" };
        Instrument inst;
        check (runSession ("Sesión VST3 (Penrose, 3 capas)", s, inst, std::move (instance)), "sesión VST3 completada");
        check (inst.zones.size() == 5 * 3, "5 notas x 3 capas = " + juce::String ((int) inst.zones.size()));
        std::cout << std::endl << (failures == 0 ? "TODOS LOS TESTS OK" : juce::String (failures) + " FALLOS") << std::endl;
        return failures == 0 ? 0 : 1;
    }

    SamplingSettings base;
    base.lowNote = 48;
    base.highNote = 60;
    base.stepSemitones = 6;
    base.numVelocityLayers = 2;
    base.noteHoldSeconds = 1.5;
    base.maxReleaseSeconds = 3.0;
    base.thresholdDb = -60.0;
    base.gapMs = 50.0;
    base.outputFolder = outRoot;
    base.creator = "Tests";
    base.category = "Synth";

    const std::pair<LoopMode, const char*> modes[] = {
        { LoopMode::forward, "Forward" }, { LoopMode::backward, "Backward" }, { LoopMode::pingPong, "PingPong" },
        { LoopMode::forwardCrossfade, "ForwardXfade" }, { LoopMode::backwardCrossfade, "BackwardXfade" }, { LoopMode::penrose, "Penrose" }
    };

    for (auto& [mode, label] : modes)
    {
        auto s = base;
        s.loopMode = mode;
        s.crossfadeEnabled = mode == LoopMode::forwardCrossfade || mode == LoopMode::backwardCrossfade;
        if (mode == LoopMode::forwardCrossfade) s.loopMode = LoopMode::forward;
        if (mode == LoopMode::backwardCrossfade) s.loopMode = LoopMode::backward;
        s.instrumentName = juce::String ("Loop ") + label;
        s.exportFormats = { "sfz" };
        Instrument inst;
        const bool ok = runSession (s.instrumentName, s, inst);
        check (ok, "sesión completada");
        check (inst.zones.size() == 3 * 2, "3 notas x 2 capas = 6 zonas (" + juce::String ((int) inst.zones.size()) + ")");

        double worst = 0.0;
        bool loopsValid = true;
        for (auto& z : inst.zones)
        {
            juce::AudioBuffer<float> a;
            WavFile::Info info;
            loopsValid = loopsValid && WavFile::read (inst.getSampleFile (z), a, info) && z.loop.enabled
                         && z.loop.start > 0 && z.loop.end < info.numFrames && z.loop.getLength() > 1000;
            if (loopsValid)
                worst = juce::jmax (worst, junctionJump (a, z.loop));
        }
        check (loopsValid, "puntos de loop válidos");
        {
            // the loop must span most of the chosen region (30 %-95 % of the 1.5 s held part)
            double shortest = 1.0e9;
            for (auto& z : inst.zones) shortest = juce::jmin (shortest, (double) z.loop.getLength() / 48000.0);
            const double expected = (s.loopEndPercent - s.loopStartPercent) / 100.0 * s.noteHoldSeconds;
            const bool penrose = mode == LoopMode::penrose;
            check (penrose || shortest > expected * 0.6, "loop largo: " + juce::String (shortest, 2) + " s (región " + juce::String (expected, 2) + " s)");
        }
        check (worst < 1.5, "sin salto en el punto de loop (peor = " + juce::String (worst, 2) + " x pendiente máxima)");
        check (inst.releaseSeconds > 0.3 && inst.releaseSeconds < 3.0, "release medido: " + juce::String (inst.releaseSeconds, 2) + " s");
    }

    // MPC 3.x program from a (synthetic) template ---------------------------------------------
    if (argc > 2 && juce::File (argv[2]).existsAsFile())
    {
        juce::MemoryBlock tpl;
        juce::File (argv[2]).loadFileAsData (tpl);
        MpcModernExporter::setTemplateForTests (tpl);
        auto s = base;
        s.loopMode = LoopMode::forward;
        s.crossfadeEnabled = true;
        s.renderCrossfade = false;
        s.instrumentName = "MPC moderno";
        s.exportFormats = { "mpc" };
        Instrument inst;
        check (runSession ("MPC 3.x", s, inst), "sesión completada");
        check (inst.folder.getChildFile ("MPC/MPC moderno.xpm").existsAsFile(), "MPC/MPC moderno.xpm");
        check (inst.folder.getChildFile ("MPC/MPC moderno_[ProgramData]").getNumberOfChildFiles (juce::File::findFiles) == 6, "6 WAV en _[ProgramData]");
        MpcModernExporter::setTemplateForTests ({});
    }

    // Crossfade applied by the sampler ----------------------------------------------------------
    {
        auto s = base;
        s.loopMode = LoopMode::forward;
        s.crossfadeEnabled = true;
        s.renderCrossfade = false;
        s.crossfadeMs = 100.0;
        s.instrumentName = "Crossfade en el sampler";
        s.exportFormats = { "sfz", "mpc", "decentsampler" };
        Instrument inst;
        check (runSession ("Crossfade en el sampler", s, inst), "sesión completada");
        check (! inst.zones.empty() && inst.zones[0].loop.crossfadeFrames == 4800, "crossfade de 100 ms = 4800 muestras para el sampler");
        const auto sfz = inst.folder.getChildFile ("Crossfade en el sampler.sfz").loadFileAsString();
        check (sfz.contains ("loop_crossfade=0.1"), "SFZ lleva loop_crossfade");
        {
            juce::MemoryBlock wav;
            inst.folder.getChildFile ("MPC").findChildFiles (juce::File::findFiles, true, "*.wav")[0].loadFileAsData (wav);
            juce::String text;
            for (size_t i = 0; i < juce::jmin<size_t> (wav.getSize(), 2000); ++i)
                text << juce::String::charToString ((juce::juce_wchar) (juce::uint8) wav[i]);
            check (text.contains ("atem") && text.contains ("\"LoopCrossfadeLength\": 4800"), "WAV de la MPC con chunk 'atem' y crossfade de 4800");
        }
        {
            // MPC browser preview: [Previews]/<name>.xpm.ogg, readable Ogg Vorbis
            const auto ogg = inst.folder.getChildFile ("MPC/[Previews]/Crossfade en el sampler.xpm.ogg");
            check (ogg.existsAsFile(), "preescucha MPC en [Previews]/<nombre>.xpm.ogg");
            juce::OggVorbisAudioFormat format;
            std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (ogg.createInputStream().release(), true));
            check (reader != nullptr && reader->lengthInSamples > 1000 && reader->lengthInSamples <= (juce::int64) (6.0 * reader->sampleRate) + 4,
                   "la preescucha es Ogg Vorbis y dura como mucho 6 s");
            if (reader != nullptr)
            {
                juce::AudioBuffer<float> b (2, (int) reader->lengthInSamples);
                reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                check (b.getMagnitude (0, b.getNumSamples() / 2) > 0.05f, "la preescucha tiene sonido");
                std::printf ("  preescucha: %.2f s, %d bytes\n", reader->lengthInSamples / reader->sampleRate, (int) ogg.getSize());
            }
        }
        {
            // the size estimate is in the right ballpark of what was written
            const auto estimate = s.estimateSize (48000.0);
            const auto real = inst.getTotalSampleBytes();
            std::printf ("  tamaño estimado %lld bytes, real %lld bytes\n", (long long) estimate.sampleBytes, (long long) real);
            check (estimate.sampleBytes > real / 3 && estimate.sampleBytes < real * 3, "estimación de tamaño razonable");
            check (estimate.totalBytes > estimate.sampleBytes, "la estimación total suma la copia de la MPC");
        }
    }

    // Sampling presets ----------------------------------------------------------------------------
    {
        const auto presets = getBuiltInSamplingPresets();
        check (presets.size() >= 8, "presets de sampleo incluidos");
        for (auto& p : presets)
        {
            SamplingSettings s;
            s.thresholdDb = -55.0;
            s.exportFormats = { "sfz" };
            s.copyPresetFieldsFrom (p.settings);
            check (! s.getRootNotes().empty() && s.thresholdDb == -55.0 && s.exportFormats.size() == 1, "preset '" + p.name + "' válido y sin tocar umbral/formatos");
        }
    }

    // Learn from the plug-in's interface + CC assignment ------------------------------------------
    {
        std::cout << "Aprender del plugin" << std::endl;
        AudioEngine engine;
        engine.setPlugin (std::make_unique<TestSynth>());
        auto& ml = engine.getMidiLearn();
        auto* plugin = engine.getPlugin();
        auto& params = plugin->getParameters();
        auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };

        ml.setCapturing (true);
        // an "LFO" that moves by itself from the start must not be captured
        for (int i = 0; i < 5; ++i) { params[1]->setValueNotifyingHost ((float) i / 10.0f); pump (40); }
        pump (500);
        params[1]->setValueNotifyingHost (0.9f);
        // the user touches Cutoff (with a gesture) and Chorus (a plug-in without gestures: value change only)
        params[0]->beginChangeGesture(); params[0]->setValueNotifyingHost (0.7f); params[0]->endChangeGesture();
        params[2]->setValueNotifyingHost (1.0f);
        pump (100);
        ml.setCapturing (false);

        auto mappings = ml.getMappings();
        check (mappings.size() == 2, "dos controles añadidos al panel moviéndolos en el plugin");
        bool cutoff = false, chorusIsButton = false, lfo = false;
        for (auto& m : mappings)
        {
            cutoff = cutoff || (m.parameterIndex == 0 && m.ccNumber < 0 && m.type == MidiLearn::ControlType::knob);
            chorusIsButton = chorusIsButton || (m.parameterIndex == 2 && m.type == MidiLearn::ControlType::button);
            lfo = lfo || m.parameterIndex == 1;
        }
        check (cutoff && chorusIsButton, "Cutoff como perilla y Chorus (on/off) como botón, sin CC");
        check (! lfo, "un parámetro que se mueve solo (LFO) no se añade");

        // assign a CC by learning, keeping the type the user chose
        ml.setControlType (0, MidiLearn::ControlType::button);
        ml.learnCcFor (0);
        ml.handleIncomingMidi (juce::MidiMessage::controllerEvent (1, 21, 100));
        pump (50);
        mappings = ml.getMappings();
        auto find = [&] (int index) { for (auto& m : mappings) if (m.parameterIndex == index) return m; return MidiLearn::Mapping(); };
        check (find (0).ccNumber == 21 && find (0).type == MidiLearn::ControlType::button, "aprender CC asigna CC 21 y respeta el tipo botón");

        // CC and channel by hand; the CC is taken from any other control
        ml.setCc (2, 21, 5);
        mappings = ml.getMappings();
        check (find (2).ccNumber == 21 && find (2).midiChannel == 5 && find (0).ccNumber == -1, "CC y canal a mano; un CC mueve un solo control");
        ml.handleIncomingMidi (juce::MidiMessage::controllerEvent (5, 21, 0));
        check (params[2]->getValue() < 0.5f, "el CC 21 por el canal 5 apaga Chorus");
        ml.handleIncomingMidi (juce::MidiMessage::controllerEvent (2, 21, 127));
        check (params[2]->getValue() < 0.5f, "por otro canal no hace nada");
    }

    // Hardware instrument (simulated): MIDI out -> synth with latency and noise -> audio in -----------
    {
        struct Hardware
        {
            TestSynth synth;
            std::vector<float> delayLine = std::vector<float> (2 * 300, 0.0f);   // 300 frames of round trip
            size_t delayPos = 0;
            juce::Random random { 7 };
            float lastLevel = 0.0f;
            int notesStartedWhileSounding = 0, noteOns = 0;
            juce::AudioBuffer<float> temp { 2, 4096 };
        };
        auto hw = std::make_shared<Hardware>();
        hw->synth.prepareToPlay (48000.0, 256);

        auto setup = [hw] (AudioEngine& engine)
        {
            engine.setSoundSource (SoundSource::hardware);
            engine.setExtraLatencyMs (300.0 * 1000.0 / 48000.0);
            engine.hardwareSimulator = [hw] (juce::MidiBuffer& midi, juce::AudioBuffer<float>& in)
            {
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                    {
                        ++hw->noteOns;
                        if (hw->lastLevel > juce::Decibels::decibelsToGain (-60.0f))
                            ++hw->notesStartedWhileSounding;
                    }

                const int n = in.getNumSamples();
                juce::AudioBuffer<float> out (hw->temp.getArrayOfWritePointers(), 2, n);
                juce::MidiBuffer copy (midi);
                hw->synth.processBlock (out, copy);

                float level = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto& slot = hw->delayLine[hw->delayPos * 2 + (size_t) ch];
                        const float noise = (hw->random.nextFloat() * 2.0f - 1.0f) * 0.0001f;   // -80 dB noise floor
                        in.setSample (ch, i, slot + noise);
                        slot = out.getSample (ch, i);
                        level = juce::jmax (level, std::abs (slot));
                    }
                    hw->delayPos = (hw->delayPos + 1) % (hw->delayLine.size() / 2);
                }
                hw->lastLevel = level;
            };
        };

        auto s = base;
        s.source = SoundSource::hardware;
        s.midiChannel = 3;
        s.loopMode = LoopMode::forward;
        s.trimLeadingSilence = false;      // the latency compensation alone must align the samples
        s.maxReleaseSeconds = 0.25;        // tails get cut: the next note must still wait for silence
        s.instrumentName = "Hardware";
        s.exportFormats = { "sfz" };
        Instrument inst;
        check (runSession ("Hardware simulado", s, inst, nullptr, setup), "sesión hardware completada");
        check (inst.zones.size() == 6, "6 muestras grabadas de la entrada de audio");
        check (hw->noteOns >= 6 && hw->notesStartedWhileSounding == 0, "ninguna nota empieza mientras suena la anterior");
        bool aligned = ! inst.zones.empty();
        for (auto& z : inst.zones)
        {
            juce::AudioBuffer<float> a; WavFile::Info info;
            WavFile::read (inst.getSampleFile (z), a, info);
            int onset = 0;
            while (onset < a.getNumSamples() && std::abs (a.getSample (0, onset)) < 0.01f) ++onset;
            aligned = aligned && onset < 480;   // < 10 ms after compensating 300 frames of latency
        }
        check (aligned, "latencia compensada: el ataque empieza al principio de la muestra");

        // batch on the hardware: banks + programs, names, a silent program is skipped
        auto b = s;
        b.instrumentName = "Lote HW";
        b.batchEnabled = true;
        b.bankMode = BankSelectMode::msbLsb;
        b.bankMsbFrom = 1; b.bankMsbTo = 1;
        b.bankLsbFrom = 0; b.bankLsbTo = 1;
        b.programFrom = 2; b.programTo = 3;    // program 3 doesn't sound
        b.programNames = { "Brillante A" };
        b.programChangeWaitMs = 100.0;
        b.hardwareControlValues = { { 0, 74, 100 } };   // Cutoff from the controls panel, instrument channel
        b.lowNote = 48; b.highNote = 60; b.stepSemitones = 12;
        check (b.getBatchItems().size() == 4, "lote: 1 MSB x 2 LSB x 2 programas = 4");
        hw->synth.receivedProgramChanges.clear();
        juce::File batchFolder;
        check (runSession ("Lote hardware", b, inst, nullptr, setup, &batchFolder), "lote hardware completado");
        check (hw->synth.receivedProgramChanges == std::vector<int> ({ 2, 3, 2, 3 }), "Program Change 3,4,3,4 enviados");
        {
            // after every program change, the panel's CC 74 = 100 on channel 3
            auto& ev = hw->synth.receivedEvents;
            int pcFollowedByCc = 0;
            for (size_t i = 0; i + 1 < ev.size(); ++i)
                if (ev[i].first < 0 && ev[i + 1].first == 100 && ev[i + 1].second == 3)
                    ++pcFollowedByCc;
            check (pcFollowedByCc == 4, "los controles MIDI del panel se envían tras cada cambio de programa");
        }
        check (hw->synth.receivedBankMsb.size() >= 4 && hw->synth.receivedBankMsb.back() == 1, "Bank Select MSB (CC0) enviado");
        check (hw->synth.receivedBankLsb.size() >= 4 && hw->synth.receivedBankLsb.back() == 1, "Bank Select LSB (CC32) enviado");
        const auto folders = batchFolder.findChildFiles (juce::File::findDirectories, false);
        check (folders.size() == 2, "2 instrumentos; los 2 programas mudos se omiten");
        check (batchFolder.getChildFile ("Brillante A").isDirectory() && batchFolder.getChildFile ("Brillante A/Brillante A.sfz").existsAsFile(),
               "el primer programa usa el nombre de la lista");
        check (batchFolder.getChildFile ("Lote HW B1.1 P003").isDirectory(), "nombre automático con banco y programa");
    }

    // Batch with plug-in programs and with MIDI Program Change -----------------------------------
    {
        auto s = base;
        s.loopMode = LoopMode::forward;
        s.lowNote = 48; s.highNote = 60; s.stepSemitones = 12;
        s.batchEnabled = true;
        s.programMethod = ProgramChangeMethod::pluginPrograms;
        s.programFrom = 0; s.programTo = 5;     // 6 asked, the plug-in has 4 (one silent)
        s.programChangeWaitMs = 50.0;
        s.instrumentName = "Lote plugin";
        s.exportFormats = { "sfz" };
        Instrument inst;
        juce::File folder;
        check (runSession ("Lote con programas del plugin", s, inst, nullptr, nullptr, &folder), "lote de programas del plugin completado");
        check (folder.findChildFiles (juce::File::findDirectories, false).size() == 3, "3 instrumentos (Mute y los que no existen se omiten)");
        check (folder.getChildFile ("Soft/Soft.sfz").existsAsFile() && folder.getChildFile ("Bright").isDirectory(), "nombres de los programas del plugin");

        s.programMethod = ProgramChangeMethod::midi;
        s.programFrom = 0; s.programTo = 1;
        s.instrumentName = "Lote PC";
        check (runSession ("Lote con Program Change al plugin", s, inst, nullptr, nullptr, &folder), "lote con Program Change MIDI al plugin");
        check (folder.getChildFile ("Lote PC P001").isDirectory() && folder.getChildFile ("Lote PC P002").isDirectory(), "un instrumento por programa");
        juce::AudioBuffer<float> a, c; WavFile::Info info;
        WavFile::read (folder.getChildFile ("Lote PC P001/Samples").findChildFiles (juce::File::findFiles, false, "*.wav")[0], a, info);
        WavFile::read (folder.getChildFile ("Lote PC P002/Samples").findChildFiles (juce::File::findFiles, false, "*.wav")[0], c, info);
        check (a.getNumSamples() > 1000 && c.getNumSamples() > 1000 && std::abs (a.getSample (0, 500) - c.getSample (0, 500)) > 1.0e-4f,
               "el Program Change cambia el sonido");
    }

    // Silent mode, faster than real time (the job renders on its own) ----------------------------
    {
        auto s = base;
        s.silentMode = true;
        s.fastRender = true;
        s.loopMode = LoopMode::forward;
        s.crossfadeEnabled = true;
        s.instrumentName = "Silencio rapido";
        s.exportFormats = { "sfz" };

        AudioEngine engine;
        engine.setPlugin (std::make_unique<TestSynth>());
        engine.prepareOffline (48000.0, 256);
        SamplingJob job (engine, s);
        job.synchronousCallbacks = true;
        bool ok = false;
        Instrument inst;
        job.onFinished = [&] (bool success, const Instrument& i) { ok = success; inst = i; };
        std::cout << "Modo silencio rápido" << std::endl;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        job.run();
        const auto seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        check (ok && inst.zones.size() == 6, "6 muestras renderizadas sin tarjeta de sonido");
        check (seconds < 10.0, "más rápido que en tiempo real (" + juce::String (seconds, 1) + " s para ~20 s de audio)");
        check (! engine.isOffline() && ! engine.isOutputMuted(), "al terminar vuelve al modo normal");
    }

    // One shot with individual notes ----------------------------------------------------------
    {
        auto s = base;
        s.oneShot = true;
        s.useNoteList = true;
        s.noteList = { 31, 24, 28, 26, 24 };   // G1, C1, E1, D1 (+ duplicate)
        s.instrumentName = "Notas sueltas";
        s.exportFormats = { "sfz", "mpc" };
        Instrument inst;
        check (runSession ("One shot con notas sueltas", s, inst), "sesión completada");
        std::set<int> roots;
        bool singleKeys = true;
        for (auto& z : inst.zones) { roots.insert (z.rootNote); singleKeys = singleKeys && z.lowKey == z.rootNote && z.highKey == z.rootNote; }
        check (roots == std::set<int> { 24, 26, 28, 31 } && inst.zones.size() == 4 * 2, "solo C1, D1, E1 y G1 (x 2 capas)");
        check (singleKeys, "cada muestra ocupa solo su tecla");

        auto r = s; r.useNoteList = false;
        check (r.getRootNotes().size() == computeRootNotes (r.lowNote, r.highNote, r.stepSemitones).size(), "en modo Rango se usa el rango");

        // individual notes with a loop (e.g. a single synth note)
        auto l = s; l.oneShot = false; l.loopEnabled = true; l.noteList = { 60 }; l.instrumentName = "Nota con loop";
        Instrument li;
        check (runSession ("Nota suelta con loop", l, li), "sesión completada");
        check (li.zones.size() == 2 && li.zones[0].loop.enabled && li.zones[0].rootNote == 60, "una nota (2 capas) con loop");
    }

    // One shot + every exporter ---------------------------------------------------------------
    {
        auto s = base;
        s.oneShot = true;
        s.instrumentName = "OneShot Test";
        s.exportFormats.clear();
        Instrument inst;
        check (runSession ("One shot", s, inst), "sesión one-shot completada");
        bool noLoops = true;
        for (auto& z : inst.zones) noLoops = noLoops && ! z.loop.enabled && z.oneShot;
        check (noLoops, "one-shot anula los loops");
    }

    {
        auto s = base;
        s.loopMode = LoopMode::forward;
        s.crossfadeEnabled = true;
        s.instrumentName = "All Formats";
        s.exportFormats.clear();
        for (auto& e : getAvailableExporters())
            if (! e.viaConvertWithMoss)
                s.exportFormats.add (e.id);
        s.exportFormats.add ("moss:Maschine");   // must fail gracefully without ConvertWithMoss
        Instrument inst;
        check (runSession ("Todos los formatos", s, inst), "exportación completa");

        const auto folder = inst.folder;
        check (folder.getChildFile ("All Formats.sfz").existsAsFile(), "SFZ");
        check (folder.getChildFile ("All Formats.dspreset").existsAsFile(), "Decent Sampler");
        check (folder.getChildFile ("All Formats.multisample").existsAsFile(), "Bitwig / Studio One");
        check (folder.getChildFile ("Ableton/Presets/Instruments/Sampler/All Formats.adv").existsAsFile(), "Ableton");
        check (folder.getChildFile ("MPC/All Formats/All Formats.xpm").existsAsFile(), "MPC");
        check (folder.getChildFile ("Logic/All Formats/All Formats.exs").existsAsFile(), "Logic EXS");
        check (folder.getChildFile ("Kontakt/All Formats.nki").existsAsFile(), "Kontakt");
        check (folder.getChildFile ("All Formats.sf2").existsAsFile(), "SF2");
    }

    std::cout << std::endl << (failures == 0 ? "TODOS LOS TESTS OK" : juce::String (failures) + " FALLOS") << std::endl;
    return failures == 0 ? 0 : 1;
}
