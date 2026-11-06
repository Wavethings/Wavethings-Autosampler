#include "Settings.h"
#include "Text.h"

namespace wt
{
    juce::StringArray getLoopModeNames()
    {
        return { "Sin loop",
                 "Hacia delante",
                 "Hacia atrás"_u8,
                 "Ida y vuelta",
                 "Hacia delante + crossfade",
                 "Hacia atrás + crossfade"_u8,
                 "Penrose (resíntesis)"_u8 };
    }

    bool loopModeUsesCrossfade (LoopMode mode)
    {
        return mode == LoopMode::forwardCrossfade || mode == LoopMode::backwardCrossfade || mode == LoopMode::penrose;
    }

    std::unique_ptr<juce::XmlElement> SamplingSettings::toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("SamplingSettings");
        xml->setAttribute ("lowNote", lowNote);
        xml->setAttribute ("highNote", highNote);
        xml->setAttribute ("stepSemitones", stepSemitones);
        xml->setAttribute ("extendToFullKeyboard", extendToFullKeyboard);
        xml->setAttribute ("numVelocityLayers", numVelocityLayers);
        xml->setAttribute ("velocityPlayPoint", (int) velocityPlayPoint);
        xml->setAttribute ("noteHoldSeconds", noteHoldSeconds);
        xml->setAttribute ("maxReleaseSeconds", maxReleaseSeconds);
        xml->setAttribute ("thresholdDb", thresholdDb);
        xml->setAttribute ("silenceHoldMs", silenceHoldMs);
        xml->setAttribute ("gapMs", gapMs);
        xml->setAttribute ("oneShot", oneShot);
        xml->setAttribute ("loopMode", (int) loopMode);
        xml->setAttribute ("loopStartPercent", loopStartPercent);
        xml->setAttribute ("loopEndPercent", loopEndPercent);
        xml->setAttribute ("crossfadeMs", crossfadeMs);
        xml->setAttribute ("trimLeadingSilence", trimLeadingSilence);
        xml->setAttribute ("normalize", normalize);
        xml->setAttribute ("normalizeTargetDb", normalizeTargetDb);
        xml->setAttribute ("fadeOutMs", fadeOutMs);
        xml->setAttribute ("bitDepth", (int) bitDepth);
        xml->setAttribute ("stereo", stereo);
        xml->setAttribute ("instrumentName", instrumentName);
        xml->setAttribute ("creator", creator);
        xml->setAttribute ("category", category);
        xml->setAttribute ("description", description);
        xml->setAttribute ("octaveForMiddleC", octaveForMiddleC);
        xml->setAttribute ("outputFolder", outputFolder.getFullPathName());
        xml->setAttribute ("exportFormats", exportFormats.joinIntoString (","));
        xml->setAttribute ("convertWithMossExecutable", convertWithMossExecutable.getFullPathName());
        return xml;
    }

    void SamplingSettings::fromXml (const juce::XmlElement& xml)
    {
        const SamplingSettings d;
        lowNote = xml.getIntAttribute ("lowNote", d.lowNote);
        highNote = xml.getIntAttribute ("highNote", d.highNote);
        stepSemitones = juce::jmax (1, xml.getIntAttribute ("stepSemitones", d.stepSemitones));
        extendToFullKeyboard = xml.getBoolAttribute ("extendToFullKeyboard", d.extendToFullKeyboard);
        numVelocityLayers = juce::jlimit (1, 16, xml.getIntAttribute ("numVelocityLayers", d.numVelocityLayers));
        velocityPlayPoint = (VelocityPlayPoint) juce::jlimit (0, 2, xml.getIntAttribute ("velocityPlayPoint", 0));
        noteHoldSeconds = xml.getDoubleAttribute ("noteHoldSeconds", d.noteHoldSeconds);
        maxReleaseSeconds = xml.getDoubleAttribute ("maxReleaseSeconds", d.maxReleaseSeconds);
        thresholdDb = xml.getDoubleAttribute ("thresholdDb", d.thresholdDb);
        silenceHoldMs = xml.getDoubleAttribute ("silenceHoldMs", d.silenceHoldMs);
        gapMs = xml.getDoubleAttribute ("gapMs", d.gapMs);
        oneShot = xml.getBoolAttribute ("oneShot", d.oneShot);
        loopMode = (LoopMode) juce::jlimit (0, 6, xml.getIntAttribute ("loopMode", (int) d.loopMode));
        loopStartPercent = xml.getDoubleAttribute ("loopStartPercent", d.loopStartPercent);
        loopEndPercent = xml.getDoubleAttribute ("loopEndPercent", d.loopEndPercent);
        crossfadeMs = xml.getDoubleAttribute ("crossfadeMs", d.crossfadeMs);
        trimLeadingSilence = xml.getBoolAttribute ("trimLeadingSilence", d.trimLeadingSilence);
        normalize = xml.getBoolAttribute ("normalize", d.normalize);
        normalizeTargetDb = xml.getDoubleAttribute ("normalizeTargetDb", d.normalizeTargetDb);
        fadeOutMs = xml.getDoubleAttribute ("fadeOutMs", d.fadeOutMs);

        const int bits = xml.getIntAttribute ("bitDepth", 24);
        bitDepth = bits == 16 ? SampleBitDepth::pcm16 : (bits == 32 ? SampleBitDepth::float32 : SampleBitDepth::pcm24);

        stereo = xml.getBoolAttribute ("stereo", d.stereo);
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
    }
}
