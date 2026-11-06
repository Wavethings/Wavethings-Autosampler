// Console test runner: drives the real AudioEngine + SamplingJob offline with a built-in test
// instrument, then checks the processed samples and writes every export format.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Core/AudioEngine.h"
#include "Core/SamplingJob.h"
#include "Core/SampleProcessing.h"
#include "Core/WavFile.h"
#include "Export/Exporter.h"

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
        TestSynth() : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}

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
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return "Init"; }
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
                    for (int h = 1; h <= 6; ++h)
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
                     std::unique_ptr<juce::AudioPluginInstance> plugin = nullptr)
    {
        AudioEngine engine;
        engine.setPlugin (plugin != nullptr ? std::move (plugin) : std::make_unique<TestSynth>());
        engine.prepareOffline (48000.0, 256);

        juce::AudioBuffer<float> out (2, 256);
        juce::MidiBuffer none;

        SamplingJob job (engine, settings);
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
        return success;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

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
        s.loopMode = LoopMode::penrose; s.crossfadeMs = 80;
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
        check (worst < 1.5, "sin salto en el punto de loop (peor = " + juce::String (worst, 2) + " x pendiente máxima)");
        check (inst.releaseSeconds > 0.3 && inst.releaseSeconds < 3.0, "release medido: " + juce::String (inst.releaseSeconds, 2) + " s");
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
        s.loopMode = LoopMode::forwardCrossfade;
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
