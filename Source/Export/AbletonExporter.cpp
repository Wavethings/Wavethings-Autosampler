#include "Exporters.h"

// The XML templates are taken from ConvertWithMoss (LGPL-3.0, (c) Jürgen Moßgraber).

namespace wt
{
    ExportResult AbletonExporter::exportInstrument (const Instrument& inst, const ExportOptions&)
    {
        using namespace ExportHelpers;

        const auto safe = safeName (inst.name);
        const auto base = inst.folder.getChildFile ("Ableton");
        const auto sampleFolder = base.getChildFile ("Samples/Imported/" + safe);
        const auto presetFolder = base.getChildFile ("Presets/Instruments/Sampler");

        juce::String error;
        if (! copySamples (inst, sampleFolder, error) || ! presetFolder.createDirectory())
            return ExportResult::failure (error);

        const auto partTemplate = Templates::get ("ADV-MultiSamplePart-Template.xml");
        auto text = Templates::get ("ADV11-Template.xml");
        if (partTemplate.isEmpty() || text.isEmpty())
            return ExportResult::failure ("Faltan las plantillas de Ableton");

        juce::String parts;
        int id = 0;

        for (auto& z : inst.zones)
        {
            const auto sampleFile = sampleFolder.getChildFile (z.fileName);
            auto p = partTemplate;
            p = p.replace ("%SAMPLE_ID%", juce::String (id))
                 .replace ("%SAMPLE_FOLDER%", escapeXml (safe))
                 .replace ("%SAMPLE_FILE%", escapeXml (z.fileName))
                 .replace ("%SAMPLE_START%", "0")
                 .replace ("%SAMPLE_END%", juce::String (z.numFrames))
                 .replace ("%SAMPLE_FILE_SIZE%", juce::String (sampleFile.getSize()))
                 .replace ("%SAMPLE_FILE_TIMESTAMP%", juce::String (sampleFile.getLastModificationTime().toMilliseconds() / 1000))
                 .replace ("%SAMPLE_RATE%", juce::String ((int) z.sampleRate))
                 .replace ("%SAMPLE_DURATION%", juce::String (z.numFrames))
                 .replace ("%KEY_RANGE_LOW%", juce::String (z.lowKey))
                 .replace ("%KEY_RANGE_HIGH%", juce::String (z.highKey))
                 .replace ("%VEL_RANGE_LOW%", juce::String (z.lowVelocity))
                 .replace ("%VEL_RANGE_HIGH%", juce::String (z.highVelocity))
                 .replace ("%KEY_RANGE_LOW_CROSSFADE%", juce::String (z.lowKey))
                 .replace ("%KEY_RANGE_HIGH_CROSSFADE%", juce::String (z.highKey))
                 .replace ("%VEL_RANGE_LOW_CROSSFADE%", juce::String (z.lowVelocity))
                 .replace ("%VEL_RANGE_HIGH_CROSSFADE%", juce::String (z.highVelocity))
                 .replace ("%SELECTOR_LOW%", "0")
                 .replace ("%SELECTOR_HIGH%", "127")
                 .replace ("%ROOT_KEY%", juce::String (z.rootNote))
                 .replace ("%DETUNE%", "0")
                 .replace ("%TUNE_SCALE%", "100")
                 .replace ("%PANORAMA%", "0")
                 .replace ("%VOLUME%", "1");

            const bool loop = z.loop.enabled && ! z.oneShot;
            p = p.replace ("%LOOP_MODE%", loop ? "1" : "0")
                 .replace ("%SAMPLE_RELEASE_LOOP%", "3")
                 .replace ("%LOOP_START%", juce::String (loop ? z.loop.start : 0))
                 .replace ("%LOOP_END%", juce::String (loop ? z.loop.end : 0))
                 .replace ("%LOOP_CROSSFADE%", "0")
                 .replace ("%LOOP_DETUNE%", "0");

            if (id > 0)
                parts << "\n";
            parts << p;
            ++id;
        }

        const double release = inst.oneShot ? juce::jmax (inst.releaseSeconds, 0.05) : inst.releaseSeconds;

        // Amplitude envelope: instant attack, full sustain, release measured from the plug-in
        text = text.replace ("%AMP_EG_ATTACK_TIME%", formatDouble (0.0))
                   .replace ("%AMP_EG_DECAY_TIME%", formatDouble (0.0))
                   .replace ("%AMP_EG_RELEASE_TIME%", formatDouble (release * 1000.0))
                   .replace ("%AMP_EG_START_LEVEL%", formatDouble (0.0))
                   .replace ("%AMP_EG_HOLD_LEVEL%", formatDouble (1.0))
                   .replace ("%AMP_EG_SUSTAIN_LEVEL%", formatDouble (1.0))
                   .replace ("%AMP_EG_END_LEVEL%", formatDouble (0.0))
                   .replace ("%AMP_EG_ATTACK_SLOPE%", formatDouble (0.0))
                   .replace ("%AMP_EG_DECAY_SLOPE%", formatDouble (0.0))
                   .replace ("%AMP_EG_RELEASE_SLOPE%", formatDouble (0.0));

        text = text.replace ("%PITCH_EG_ENABLED%", "false")
                   .replace ("%PITCH_EG_AMOUNT%", "0")
                   .replace ("%PITCH_EG_ATTACK_TIME%", formatDouble (0.0))
                   .replace ("%PITCH_EG_DECAY_TIME%", formatDouble (0.0))
                   .replace ("%PITCH_EG_RELEASE_TIME%", formatDouble (0.0))
                   .replace ("%PITCH_EG_START_LEVEL%", formatDouble (0.0))
                   .replace ("%PITCH_EG_HOLD_LEVEL%", formatDouble (1.0))
                   .replace ("%PITCH_EG_SUSTAIN_LEVEL%", formatDouble (1.0))
                   .replace ("%PITCH_EG_END_LEVEL%", formatDouble (0.0))
                   .replace ("%PITCH_EG_ATTACK_SLOPE%", formatDouble (0.0))
                   .replace ("%PITCH_EG_DECAY_SLOPE%", formatDouble (0.0))
                   .replace ("%PITCH_EG_RELEASE_SLOPE%", formatDouble (0.0));

        // velocity -> volume: the layers already contain the dynamics, keep a moderate amount
        text = text.replace ("%VOLUME_VELOCITY_MOD%", formatDouble (inst.numVelocityLayers > 1 ? 0.35 : 1.0));

        text = text.replace ("%FILTER_ENABLED%", "false")
                   .replace ("%FILTER_TYPE%", "0")
                   .replace ("%FILTER_SLOPE%", "true")
                   .replace ("%FILTER_FREQ%", formatDouble (22000.0))
                   .replace ("%FILTER_RES%", formatDouble (0.0))
                   .replace ("%FILTER_EG_ENABLED%", "false")
                   .replace ("%FILTER_EG_AMOUNT%", "0")
                   .replace ("%FILTER_EG_ATTACK_TIME%", formatDouble (0.0))
                   .replace ("%FILTER_EG_DECAY_TIME%", formatDouble (0.0))
                   .replace ("%FILTER_EG_RELEASE_TIME%", formatDouble (0.0))
                   .replace ("%FILTER_EG_START_LEVEL%", formatDouble (0.0))
                   .replace ("%FILTER_EG_HOLD_LEVEL%", formatDouble (1.0))
                   .replace ("%FILTER_EG_SUSTAIN_LEVEL%", formatDouble (1.0))
                   .replace ("%FILTER_EG_END_LEVEL%", formatDouble (0.0))
                   .replace ("%FILTER_EG_ATTACK_SLOPE%", formatDouble (0.0))
                   .replace ("%FILTER_EG_DECAY_SLOPE%", formatDouble (0.0))
                   .replace ("%FILTER_EG_RELEASE_SLOPE%", formatDouble (0.0))
                   .replace ("%FILTER_VELOCITY_MOD%", formatDouble (0.0))
                   .replace ("%FILTER_KEY_MOD%", formatDouble (0.0));

        const auto presetFile = presetFolder.getChildFile (safe + ".adv");
        text = text.replace ("%PRESET_NAME%", escapeXml (inst.name.replaceCharacter ('.', '_')))
                   .replace ("%FILE_NAME%", escapeXml (presetFile.getFileName()))
                   .replace ("%MULTI_SAMPLE_PARTS%", parts)
                   .replace ("%PITCHBEND_RANGE%", "2");

        presetFile.deleteFile();
        {
            juce::FileOutputStream fileOut (presetFile);
            if (! fileOut.openedOk())
                return ExportResult::failure ("No se pudo escribir " + presetFile.getFullPathName());

            juce::GZIPCompressorOutputStream gzip (fileOut, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
            gzip.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
            gzip.flush();
        }

        return ExportResult::success (presetFile, "copia el contenido de la carpeta Ableton a tu User Library");
    }
}
