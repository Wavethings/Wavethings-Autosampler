#include "Exporters.h"

// Kontakt 1 NKI: a small binary header followed by a zlib compressed XML document.
// Header layout and XML templates from ConvertWithMoss (LGPL-3.0, (c) Jürgen Moßgraber).

namespace wt
{
    ExportResult KontaktExporter::exportInstrument (const Instrument& inst, const ExportOptions&)
    {
        using namespace ExportHelpers;

        const auto safe = safeName (inst.name);
        const auto base = inst.folder.getChildFile ("Kontakt");
        const auto sampleFolder = base.getChildFile (safe);

        juce::String error;
        if (! copySamples (inst, sampleFolder, error))
            return ExportResult::failure (error);

        auto f = [] (double v) { return formatDouble (v, 6); };
        const double release = inst.oneShot ? 0.05 : inst.releaseSeconds;
        const double velocityDepth = inst.numVelocityLayers > 1 ? 0.35 : 1.0;

        // Program -----------------------------------------------------------------------
        auto program = Templates::get ("Kontakt1_01_Program_Start.xml");
        program = program.replace ("%PROGRAM_INDEX%", "0")
                         .replace ("%PROGRAM_NAME%", escapeXml (inst.name))
                         .replace ("%PROGRAM_MIDI_CHANNEL%", "0")
                         .replace ("%MASTER_VOLUME%", f (1.0))
                         .replace ("%MASTER_PANNING%", "0.50")
                         .replace ("%MASTER_TRANSPOSE%", "0")
                         .replace ("%MASTER_TUNE%", f (1.0))
                         .replace ("%MASTER_LOW_KEY%", "0")
                         .replace ("%MASTER_HIGH_KEY%", "127");

        const auto groupTemplate = Templates::get ("Kontakt1_02_Group.xml");
        const auto zoneTemplate = Templates::get ("Kontakt1_04_Zone.xml");
        const auto loopTemplate = Templates::get ("Kontakt1_05_Loop.xml");

        juce::String groupsText, zonesText;
        const auto layers = inst.getZonesByLayer();
        int groupIndex = 0, zoneIndex = 0;

        for (auto& layer : layers)
        {
            if (layer.empty())
                continue;

            auto g = groupTemplate;
            g = g.replace ("%GROUP_INDEX%", juce::String (groupIndex))
                 .replace ("%GROUP_NAME%", escapeXml ("Velocidad " + juce::String (layer.front()->lowVelocity) + "-" + juce::String (layer.front()->highVelocity)))
                 .replace ("%ENVELOPE_INTENSITY%", f (1.0))
                 .replace ("%ENVELOPE_ATTACK_CURVE%", f (0.0))
                 .replace ("%ENVELOPE_ATTACK%", f (0.0))
                 .replace ("%ENVELOPE_DECAY%", f (0.0))
                 .replace ("%ENVELOPE_HOLD%", f (0.0))
                 .replace ("%ENVELOPE_RELEASE%", f (release * 1000.0))
                 .replace ("%ENVELOPE_SUSTAIN%", f (1.0))
                 .replace ("%PITCH_ENVELOPE_INTENSITY%", f (0.0))
                 .replace ("%PITCH_ENVELOPE_ATTACK_CURVE%", f (0.0))
                 .replace ("%PITCH_ENVELOPE_ATTACK%", f (0.0))
                 .replace ("%PITCH_ENVELOPE_DECAY%", f (0.0))
                 .replace ("%PITCH_ENVELOPE_HOLD%", f (0.0))
                 .replace ("%PITCH_ENVELOPE_RELEASE%", f (0.0))
                 .replace ("%PITCH_ENVELOPE_SUSTAIN%", f (0.0))
                 .replace ("%FILTER_TYPE%", "lp2pole")
                 .replace ("%FILTER_CUTOFF%", f (1.0))
                 .replace ("%FILTER_RESONANCE%", f (0.0))
                 .replace ("%FILTER_CUTOFF_ENVELOPE_INTENSITY%", f (0.0))
                 .replace ("%FILTER_CUTOFF_ENVELOPE_ATTACK_CURVE%", f (0.0))
                 .replace ("%FILTER_CUTOFF_ENVELOPE_ATTACK%", f (0.0))
                 .replace ("%FILTER_CUTOFF_ENVELOPE_DECAY%", f (0.0))
                 .replace ("%FILTER_CUTOFF_ENVELOPE_HOLD%", f (0.0))
                 .replace ("%FILTER_CUTOFF_ENVELOPE_RELEASE%", f (1000.0))
                 .replace ("%FILTER_CUTOFF_ENVELOPE_SUSTAIN%", f (1.0))
                 .replace ("%FILTER_CUTOFF_VELOCITY_MOD%", f (0.0))
                 .replace ("%AMP_VELOCITY_MOD%", f (velocityDepth))
                 .replace ("%PITCH_BEND%", f (200.0 / 1200.0))
                 .replace ("%GROUP_ONE_SHOT%", inst.oneShot ? "yes" : "no")
                 .replace ("%GROUP_KEY_TRACKING%", "yes")
                 .replace ("%GROUP_RELEASE_TRIGGER%", "no")
                 .replace ("%GROUP_REVERSE%", "no")
                 .replace ("%GROUP_VOICE_GROUP%", "-1");
            groupsText << g;

            for (auto* z : layer)
            {
                juce::String loops;
                if (z->oneShot || (z->loop.enabled && ! z->oneShot))
                {
                    const bool isLoop = ! z->oneShot;
                    loops = loopTemplate.replace ("%LOOP_INDEX%", "0")
                                        .replace ("%LOOP_START%", juce::String (isLoop ? z->loop.start : 0))
                                        .replace ("%LOOP_LENGTH%", juce::String (isLoop ? z->loop.getLength() : 0))
                                        .replace ("%LOOP_MODE%", isLoop ? "until_end" : "oneshot")
                                        .replace ("%LOOP_ALTERNATING%", "no")
                                        .replace ("%LOOP_TUNING%", "1.0")
                                        .replace ("%LOOP_XFADE%", juce::String (isLoop ? z->loop.crossfadeFrames : 0));
                }

                auto zone = zoneTemplate;
                zone = zone.replace ("%GROUP_INDEX%", juce::String (groupIndex))
                           .replace ("%ZONE_INDEX%", juce::String (zoneIndex++))
                           .replace ("%ZONE_SAMPLE_START%", "0")
                           .replace ("%ZONE_SAMPLE_END%", juce::String (z->numFrames))
                           .replace ("%ZONE_VEL_LOW%", juce::String (z->lowVelocity))
                           .replace ("%ZONE_VEL_HIGH%", juce::String (z->highVelocity))
                           .replace ("%ZONE_KEY_LOW%", juce::String (z->lowKey))
                           .replace ("%ZONE_KEY_HIGH%", juce::String (z->highKey))
                           .replace ("%ZONE_VEL_CROSS_LOW%", "0")
                           .replace ("%ZONE_VEL_CROSS_HIGH%", "0")
                           .replace ("%ZONE_KEY_CROSS_LOW%", "0")
                           .replace ("%ZONE_KEY_CROSS_HIGH%", "0")
                           .replace ("%ZONE_KEY_ROOT%", juce::String (z->rootNote))
                           .replace ("%ZONE_VOLUME%", f (juce::Decibels::decibelsToGain (z->gainDb)))
                           .replace ("%ZONE_TUNE%", f (1.0))
                           .replace ("%ZONE_PAN%", f (0.5))
                           .replace ("%ZONE_SAMPLE_NAME%", escapeXml (safe + "\\" + z->fileName))
                           .replace ("%ZONE_LOOPS%", loops);
                zonesText << zone;
            }

            ++groupIndex;
        }

        const juce::String xml = juce::String ("<?xml version=\"1.0\"?>\n") + program + groupsText
                               + Templates::get ("Kontakt1_03_Groups_Zones.xml") + zonesText
                               + Templates::get ("Kontakt1_06_Program_End.xml");

        // File: header (little endian) + zlib stream --------------------------------------------
        const auto file = base.getChildFile (safe + ".nki");
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk())
            return ExportResult::failure ("No se pudo escribir " + file.getFullPathName());

        out.writeInt ((int) 0xB36EE55E);
        out.writeInt (0x24);
        out.writeShort (0x50);
        out.writeShort (0x01);
        out.writeInt (0);
        out.writeInt (0);
        out.writeInt (1);
        out.writeInt ((int) (juce::Time::currentTimeMillis() / 1000));
        out.writeInt ((int) inst.getTotalSampleBytes());
        out.writeInt (0);

        {
            juce::GZIPCompressorOutputStream zlib (out, 6, 0);
            zlib.write (xml.toRawUTF8(), xml.getNumBytesAsUTF8());
            zlib.flush();
        }

        out.flush();
        return ExportResult::success (file, "abre el .nki desde el navegador de Kontakt");
    }
}
