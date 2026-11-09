#include "Settings.h"
#include "Text.h"

namespace wt
{
    juce::StringArray getLoopModeNames()
    {
        return { "Forward", "Backward", "Ping-Pong", "Penrose" };
    }

    LoopMode loopModeFromMenuIndex (int index)
    {
        switch (index)
        {
            case 1:  return LoopMode::backward;
            case 2:  return LoopMode::pingPong;
            case 3:  return LoopMode::penrose;
            default: return LoopMode::forward;
        }
    }

    int menuIndexFromLoopMode (LoopMode mode)
    {
        switch (mode)
        {
            case LoopMode::backward:
            case LoopMode::backwardCrossfade: return 1;
            case LoopMode::pingPong:          return 2;
            case LoopMode::penrose:           return 3;
            default:                          return 0;
        }
    }

    bool loopModeUsesCrossfade (LoopMode mode)
    {
        return mode == LoopMode::forwardCrossfade || mode == LoopMode::backwardCrossfade || mode == LoopMode::penrose;
    }

    std::vector<int> SamplingSettings::getRootNotes() const
    {
        if (isUsingNoteList())
        {
            std::vector<int> notes;
            for (int n : noteList)
                if (n >= 0 && n <= 127 && std::find (notes.begin(), notes.end(), n) == notes.end())
                    notes.push_back (n);
            std::sort (notes.begin(), notes.end());
            return notes;
        }

        return computeRootNotes (lowNote, highNote, stepSemitones);
    }

    std::vector<KeyRange> SamplingSettings::getKeyRanges() const
    {
        const auto roots = getRootNotes();
        if (isUsingNoteList())
        {
            std::vector<KeyRange> ranges;
            for (int n : roots)
                ranges.push_back ({ n, n });
            return ranges;
        }

        return computeKeyRanges (roots, stepSemitones, extendToFullKeyboard);
    }

    std::vector<BatchItem> SamplingSettings::getBatchItems() const
    {
        std::vector<BatchItem> items;
        if (! batchEnabled)
        {
            items.push_back ({});
            return items;
        }

        const bool useMsb = programMethod == ProgramChangeMethod::midi && (bankMode == BankSelectMode::msb || bankMode == BankSelectMode::msbLsb);
        const bool useLsb = programMethod == ProgramChangeMethod::midi && (bankMode == BankSelectMode::lsb || bankMode == BankSelectMode::msbLsb);
        const int msbLo = useMsb ? juce::jlimit (0, 127, juce::jmin (bankMsbFrom, bankMsbTo)) : -1;
        const int msbHi = useMsb ? juce::jlimit (0, 127, juce::jmax (bankMsbFrom, bankMsbTo)) : -1;
        const int lsbLo = useLsb ? juce::jlimit (0, 127, juce::jmin (bankLsbFrom, bankLsbTo)) : -1;
        const int lsbHi = useLsb ? juce::jlimit (0, 127, juce::jmax (bankLsbFrom, bankLsbTo)) : -1;
        const int progLo = juce::jlimit (0, 127, juce::jmin (programFrom, programTo));
        const int progHi = juce::jlimit (0, 127, juce::jmax (programFrom, programTo));

        for (int msb = msbLo; msb <= msbHi; ++msb)
            for (int lsb = lsbLo; lsb <= lsbHi; ++lsb)
                for (int prog = progLo; prog <= progHi; ++prog)
                {
                    BatchItem item;
                    item.bankMsb = msb;
                    item.bankLsb = lsb;
                    item.program = prog;
                    item.index = (int) items.size();
                    if (item.index < programNames.size())
                        item.name = programNames[item.index].trim();
                    items.push_back (item);
                }
        return items;
    }

    void SamplingSettings::copyPresetFieldsFrom (const SamplingSettings& p)
    {
        lowNote = p.lowNote;                 highNote = p.highNote;
        stepSemitones = p.stepSemitones;     extendToFullKeyboard = p.extendToFullKeyboard;
        useNoteList = p.useNoteList;         noteList = p.noteList;
        numVelocityLayers = p.numVelocityLayers;
        velocityPlayPoint = p.velocityPlayPoint;
        noteHoldSeconds = p.noteHoldSeconds; maxReleaseSeconds = p.maxReleaseSeconds;
        gapMs = p.gapMs;
        loopEnabled = p.loopEnabled;         loopMode = p.loopMode;
        loopStartPercent = p.loopStartPercent; loopEndPercent = p.loopEndPercent;
        crossfadeEnabled = p.crossfadeEnabled; crossfadeLinear = p.crossfadeLinear;
        crossfadeMs = p.crossfadeMs;         equaliseLoopLevel = p.equaliseLoopLevel;
        oneShot = useNoteList && ! loopEnabled;
    }

    SamplingSettings::SizeEstimate SamplingSettings::estimateSize (double sampleRate) const
    {
        SizeEstimate e;
        const auto roots = getRootNotes();
        const int numSamples = (int) roots.size() * juce::jlimit (1, 127, numVelocityLayers);

        // a sample lasts the held part plus a typical release; a loop cuts it at the loop end
        double seconds = noteHoldSeconds + juce::jmin (2.0, maxReleaseSeconds * 0.5);
        if (isLooping())
        {
            const double loopEnd = noteHoldSeconds * juce::jlimit (0.05, 1.0, loopEndPercent / 100.0);
            const double loopLength = noteHoldSeconds * juce::jmax (0.05, (loopEndPercent - loopStartPercent) / 100.0);
            seconds = loopEnd;
            if (loopMode == LoopMode::backward || loopMode == LoopMode::backwardCrossfade) seconds += loopLength;
            if (loopMode == LoopMode::pingPong) seconds += loopLength;
        }
        e.secondsPerSample = seconds;

        const int channels = stereo ? 2 : 1;
        const int bytes = (int) bitDepth / 8;
        e.sampleBytes = (juce::int64) (seconds * sampleRate) * channels * bytes * numSamples;

        // formats that copy the WAV files, SF2 stores 16 + 8 bit
        double copies = 1.0;
        for (auto& id : exportFormats)
        {
            if (id == "ableton" || id == "mpc" || id == "mpc-legacy" || id == "exs" || id == "kontakt" || id == "bitwig" || id.startsWith ("moss:"))
                copies += 1.0;
            else if (id == "sf2")
                copies += 3.0 / bytes;
        }
        e.totalBytes = (juce::int64) ((double) e.sampleBytes * copies);
        return e;
    }

    std::vector<SamplingPreset> getBuiltInSamplingPresets()
    {
        auto make = [] (const char* name, int low, int high, int step, int layers, double hold, double release,
                        bool loop, LoopMode mode, double xfadeMs, double loopStart = 30.0, double loopEnd = 95.0)
        {
            SamplingPreset p;
            p.name = juce::String::fromUTF8 (name);
            auto& s = p.settings;
            s.lowNote = low; s.highNote = high; s.stepSemitones = step; s.extendToFullKeyboard = true;
            s.numVelocityLayers = layers; s.noteHoldSeconds = hold; s.maxReleaseSeconds = release;
            s.gapMs = 150.0; s.loopEnabled = loop; s.loopMode = mode;
            s.crossfadeEnabled = xfadeMs > 0.0; s.crossfadeMs = xfadeMs > 0.0 ? xfadeMs : 100.0; s.crossfadeLinear = true;
            s.loopStartPercent = loopStart; s.loopEndPercent = loopEnd; s.equaliseLoopLevel = true;
            return p;
        };

        std::vector<SamplingPreset> presets {
            make ("Piano / Keys (88 teclas, 4 capas)", 21, 108, 3, 4, 6.0, 6.0, false, LoopMode::forward, 0.0),
            make ("Pad / Ambient (loop largo)",         36, 96, 3, 1, 8.0, 8.0, true, LoopMode::forward, 400.0, 25.0, 95.0),
            make ("Pad Penrose",                        36, 96, 6, 1, 6.0, 8.0, true, LoopMode::penrose, 300.0, 30.0, 95.0),
            make ("Lead / Synth",                       36, 96, 3, 2, 3.0, 2.0, true, LoopMode::forward, 120.0),
            make ("Bajo",                               24, 60, 3, 2, 3.0, 1.5, true, LoopMode::forward, 80.0),
            make ("Cuerdas / Metales",                  36, 96, 3, 3, 5.0, 4.0, true, LoopMode::forward, 250.0),
            make ("Pluck / Mallets / Guitarra",         36, 96, 3, 3, 1.5, 4.0, false, LoopMode::forward, 0.0),
            make ("Órgano (loop corto, sin capas)",     36, 96, 3, 1, 2.5, 0.8, true, LoopMode::forward, 60.0, 40.0, 90.0),
            make ("Prueba rápida",                      48, 72, 12, 1, 1.5, 2.0, false, LoopMode::forward, 0.0),
        };

        auto drums = make ("Batería / One Shot (kit GM)", 36, 51, 1, 3, 0.5, 4.0, false, LoopMode::forward, 0.0);
        drums.settings.useNoteList = true;
        drums.settings.noteList = { 36, 38, 40, 42, 44, 46, 49, 51 };   // kick, snare, rim, hats, crash, ride
        drums.settings.oneShot = true;
        presets.push_back (drums);

        auto fx = make ("FX / Texturas (One Shot largo)", 48, 72, 12, 1, 4.0, 15.0, false, LoopMode::forward, 0.0);
        fx.settings.useNoteList = true;
        fx.settings.noteList = { 60 };
        fx.settings.oneShot = true;
        presets.push_back (fx);

        return presets;
    }

    std::unique_ptr<juce::XmlElement> SamplingSettings::toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("SamplingSettings");
        xml->setAttribute ("lowNote", lowNote);
        xml->setAttribute ("highNote", highNote);
        xml->setAttribute ("stepSemitones", stepSemitones);
        xml->setAttribute ("extendToFullKeyboard", extendToFullKeyboard);
        xml->setAttribute ("useNoteList", useNoteList);
        {
            juce::StringArray notes;
            for (int n : noteList) notes.add (juce::String (n));
            xml->setAttribute ("noteList", notes.joinIntoString (","));
        }
        xml->setAttribute ("numVelocityLayers", numVelocityLayers);
        xml->setAttribute ("velocityPlayPoint", (int) velocityPlayPoint);
        xml->setAttribute ("noteHoldSeconds", noteHoldSeconds);
        xml->setAttribute ("maxReleaseSeconds", maxReleaseSeconds);
        xml->setAttribute ("thresholdDb", thresholdDb);
        xml->setAttribute ("silenceHoldMs", silenceHoldMs);
        xml->setAttribute ("gapMs", gapMs);
        xml->setAttribute ("oneShot", oneShot);
        xml->setAttribute ("loopMode", (int) loopMode);
        xml->setAttribute ("loopEnabled", loopEnabled);
        xml->setAttribute ("crossfadeEnabled", crossfadeEnabled);
        xml->setAttribute ("crossfadeLinear", crossfadeLinear);
        xml->setAttribute ("loopStartPercent", loopStartPercent);
        xml->setAttribute ("loopEndPercent", loopEndPercent);
        xml->setAttribute ("crossfadeMs", crossfadeMs);
        xml->setAttribute ("equaliseLoopLevel", equaliseLoopLevel);
        xml->setAttribute ("renderCrossfade", renderCrossfade);
        xml->setAttribute ("trimLeadingSilence", trimLeadingSilence);
        xml->setAttribute ("normalize", normalize);
        xml->setAttribute ("normalizeTargetDb", normalizeTargetDb);
        xml->setAttribute ("fadeOutMs", fadeOutMs);
        xml->setAttribute ("bitDepth", (int) bitDepth);
        xml->setAttribute ("stereo", stereo);
        xml->setAttribute ("silentMode", silentMode);
        xml->setAttribute ("fastRender", fastRender);
        xml->setAttribute ("instrumentName", instrumentName);
        xml->setAttribute ("creator", creator);
        xml->setAttribute ("category", category);
        xml->setAttribute ("description", description);
        xml->setAttribute ("octaveForMiddleC", octaveForMiddleC);
        xml->setAttribute ("outputFolder", outputFolder.getFullPathName());
        xml->setAttribute ("exportFormats", exportFormats.joinIntoString (","));
        xml->setAttribute ("convertWithMossExecutable", convertWithMossExecutable.getFullPathName());

        xml->setAttribute ("source", (int) source);
        xml->setAttribute ("midiChannel", midiChannel);
        xml->setAttribute ("midiOutputId", midiOutputId);
        xml->setAttribute ("inputChannel", inputChannel);
        xml->setAttribute ("inputStereo", inputStereo);
        xml->setAttribute ("monitorInput", monitorInput);
        xml->setAttribute ("extraLatencyMs", extraLatencyMs);
        xml->setAttribute ("maxWaitForSilenceSeconds", maxWaitForSilenceSeconds);
        xml->setAttribute ("batchEnabled", batchEnabled);
        xml->setAttribute ("programMethod", (int) programMethod);
        xml->setAttribute ("bankMode", (int) bankMode);
        xml->setAttribute ("bankMsbFrom", bankMsbFrom);
        xml->setAttribute ("bankMsbTo", bankMsbTo);
        xml->setAttribute ("bankLsbFrom", bankLsbFrom);
        xml->setAttribute ("bankLsbTo", bankLsbTo);
        xml->setAttribute ("programFrom", programFrom);
        xml->setAttribute ("programTo", programTo);
        xml->setAttribute ("programChangeWaitMs", programChangeWaitMs);
        xml->setAttribute ("skipSilentPrograms", skipSilentPrograms);
        xml->setAttribute ("usePluginProgramNames", usePluginProgramNames);
        xml->setAttribute ("programNames", programNames.joinIntoString ("\n"));
        return xml;
    }

    void SamplingSettings::fromXml (const juce::XmlElement& xml)
    {
        const SamplingSettings d;
        lowNote = xml.getIntAttribute ("lowNote", d.lowNote);
        highNote = xml.getIntAttribute ("highNote", d.highNote);
        stepSemitones = juce::jmax (1, xml.getIntAttribute ("stepSemitones", d.stepSemitones));
        extendToFullKeyboard = xml.getBoolAttribute ("extendToFullKeyboard", d.extendToFullKeyboard);
        useNoteList = xml.getBoolAttribute ("useNoteList", false);
        noteList.clear();
        for (auto& t : juce::StringArray::fromTokens (xml.getStringAttribute ("noteList"), ",", ""))
            if (t.trim().isNotEmpty())
                noteList.push_back (juce::jlimit (0, 127, t.getIntValue()));
        numVelocityLayers = juce::jlimit (1, 16, xml.getIntAttribute ("numVelocityLayers", d.numVelocityLayers));
        velocityPlayPoint = (VelocityPlayPoint) juce::jlimit (0, 2, xml.getIntAttribute ("velocityPlayPoint", 0));
        noteHoldSeconds = xml.getDoubleAttribute ("noteHoldSeconds", d.noteHoldSeconds);
        maxReleaseSeconds = xml.getDoubleAttribute ("maxReleaseSeconds", d.maxReleaseSeconds);
        thresholdDb = xml.getDoubleAttribute ("thresholdDb", d.thresholdDb);
        silenceHoldMs = xml.getDoubleAttribute ("silenceHoldMs", d.silenceHoldMs);
        gapMs = xml.getDoubleAttribute ("gapMs", d.gapMs);
        oneShot = xml.getBoolAttribute ("oneShot", d.oneShot);
        loopMode = (LoopMode) juce::jlimit (0, 6, xml.getIntAttribute ("loopMode", (int) d.loopMode));
        loopEnabled = xml.getBoolAttribute ("loopEnabled", loopMode != LoopMode::off);
        crossfadeEnabled = xml.getBoolAttribute ("crossfadeEnabled", loopMode == LoopMode::forwardCrossfade || loopMode == LoopMode::backwardCrossfade);
        crossfadeLinear = xml.getBoolAttribute ("crossfadeLinear", true);
        // settings from older versions: the crossfade was part of the mode
        if (loopMode == LoopMode::forwardCrossfade || loopMode == LoopMode::off) loopMode = LoopMode::forward;
        if (loopMode == LoopMode::backwardCrossfade) loopMode = LoopMode::backward;
        loopStartPercent = xml.getDoubleAttribute ("loopStartPercent", d.loopStartPercent);
        loopEndPercent = xml.getDoubleAttribute ("loopEndPercent", d.loopEndPercent);
        crossfadeMs = xml.getDoubleAttribute ("crossfadeMs", d.crossfadeMs);
        equaliseLoopLevel = xml.getBoolAttribute ("equaliseLoopLevel", d.equaliseLoopLevel);
        renderCrossfade = xml.getBoolAttribute ("renderCrossfade", d.renderCrossfade);
        trimLeadingSilence = xml.getBoolAttribute ("trimLeadingSilence", d.trimLeadingSilence);
        normalize = xml.getBoolAttribute ("normalize", d.normalize);
        normalizeTargetDb = xml.getDoubleAttribute ("normalizeTargetDb", d.normalizeTargetDb);
        fadeOutMs = xml.getDoubleAttribute ("fadeOutMs", d.fadeOutMs);

        const int bits = xml.getIntAttribute ("bitDepth", 24);
        bitDepth = bits == 16 ? SampleBitDepth::pcm16 : (bits == 32 ? SampleBitDepth::float32 : SampleBitDepth::pcm24);

        stereo = xml.getBoolAttribute ("stereo", d.stereo);
        silentMode = xml.getBoolAttribute ("silentMode", false);
        fastRender = xml.getBoolAttribute ("fastRender", false);
        instrumentName = xml.getStringAttribute ("instrumentName", d.instrumentName);
        creator = xml.getStringAttribute ("creator");
        category = xml.getStringAttribute ("category");
        description = xml.getStringAttribute ("description");
        octaveForMiddleC = xml.getIntAttribute ("octaveForMiddleC", 3);

        const auto folder = xml.getStringAttribute ("outputFolder");
        if (folder.isNotEmpty() && juce::File::isAbsolutePath (folder))
            outputFolder = juce::File (folder);

        if (xml.hasAttribute ("exportFormats"))
            exportFormats = juce::StringArray::fromTokens (xml.getStringAttribute ("exportFormats"), ",", "");
        exportFormats.removeEmptyStrings();

        const auto moss = xml.getStringAttribute ("convertWithMossExecutable");
        if (moss.isNotEmpty() && juce::File::isAbsolutePath (moss))
            convertWithMossExecutable = juce::File (moss);
        source = (SoundSource) juce::jlimit (0, 1, xml.getIntAttribute ("source", 0));
        midiChannel = juce::jlimit (1, 16, xml.getIntAttribute ("midiChannel", d.midiChannel));
        midiOutputId = xml.getStringAttribute ("midiOutputId");
        inputChannel = juce::jmax (0, xml.getIntAttribute ("inputChannel", 0));
        inputStereo = xml.getBoolAttribute ("inputStereo", true);
        monitorInput = xml.getBoolAttribute ("monitorInput", true);
        extraLatencyMs = xml.getDoubleAttribute ("extraLatencyMs", 0.0);
        maxWaitForSilenceSeconds = juce::jlimit (1.0, 120.0, xml.getDoubleAttribute ("maxWaitForSilenceSeconds", d.maxWaitForSilenceSeconds));
        batchEnabled = xml.getBoolAttribute ("batchEnabled", false);
        programMethod = (ProgramChangeMethod) juce::jlimit (0, 1, xml.getIntAttribute ("programMethod", 0));
        bankMode = (BankSelectMode) juce::jlimit (0, 3, xml.getIntAttribute ("bankMode", 0));
        bankMsbFrom = xml.getIntAttribute ("bankMsbFrom", 0);
        bankMsbTo = xml.getIntAttribute ("bankMsbTo", 0);
        bankLsbFrom = xml.getIntAttribute ("bankLsbFrom", 0);
        bankLsbTo = xml.getIntAttribute ("bankLsbTo", 0);
        programFrom = juce::jlimit (0, 127, xml.getIntAttribute ("programFrom", d.programFrom));
        programTo = juce::jlimit (0, 127, xml.getIntAttribute ("programTo", d.programTo));
        programChangeWaitMs = xml.getDoubleAttribute ("programChangeWaitMs", d.programChangeWaitMs);
        skipSilentPrograms = xml.getBoolAttribute ("skipSilentPrograms", true);
        usePluginProgramNames = xml.getBoolAttribute ("usePluginProgramNames", true);
        programNames = juce::StringArray::fromLines (xml.getStringAttribute ("programNames"));
        while (programNames.size() > 0 && programNames[programNames.size() - 1].trim().isEmpty())
            programNames.remove (programNames.size() - 1);
    }
}
