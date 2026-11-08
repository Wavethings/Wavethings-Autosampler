#include "Exporters.h"
#include <map>
#include "../Core/Text.h"

// Structure follows the MPC keygroup writer of ConvertWithMoss (LGPL-3.0, (c) Jürgen Moßgraber).

namespace wt
{
    namespace
    {
        void text (juce::XmlElement& parent, const juce::String& name, const juce::String& value)
        {
            parent.createNewChildElement (name)->addTextElement (value);
        }

        juce::String f6 (double v) { return juce::String (v, 6, false); }

        double envTime (double seconds)
        {
            const double minT = 0.001, maxT = 100.0;
            return std::log (juce::jlimit (minT, maxT, seconds) / minT) / std::log (maxT / minT);
        }

        double mpcGain (double db)
        {
            const double minus12 = 0.353, plus6 = 1.0;
            const double v = 12.0 + juce::jmin (6.0, db);
            return juce::jlimit (0.0, plus6, minus12 + (plus6 - minus12) * v / 18.0);
        }
    }

    ExportResult MpcExporter::exportInstrument (const Instrument& inst, const ExportOptions& options)
    {
        const auto safe = ExportHelpers::safeName (inst.name);
        const auto folder = inst.folder.getChildFile ("MPC").getChildFile (safe);

        juce::String error;
        if (! ExportHelpers::copySamples (inst, folder, error, ".WAV"))
            return ExportResult::failure (error);

        const int layerLimit = 8;
        if (inst.numVelocityLayers > layerLimit && options.log)
            options.log ("MPC: solo se admiten 8 capas por keygroup; se exportan las 8 más fuertes"_u8);

        juce::XmlElement root ("MPCVObject");
        auto* version = root.createNewChildElement ("Version");
        text (*version, "File_Version", "2.1");
        text (*version, "Platform", "Linux");

        auto* program = root.createNewChildElement ("Program");
        program->setAttribute ("type", "Keygroup");
        text (*program, "ProgramName", inst.name);
        program->createNewChildElement ("ProgramPads-v2.11.6.6");
        text (*program, "KeygroupPitchBendRange", juce::String (200.0 / 1200.0, 3, false));
        text (*program, "KeygroupWheelToLfo", "1.000000");

        auto* instruments = program->createNewChildElement ("Instruments");

        // one keygroup per key range, the velocity layers become the layers of the keygroup
        std::map<std::pair<int, int>, std::vector<const SampleZone*>> keygroups;
        for (auto& z : inst.zones)
            keygroups[{ z.lowKey, z.highKey }].push_back (&z);

        const double release = inst.oneShot ? 0.05 : inst.releaseSeconds;
        int number = 1;

        for (auto& [range, zones] : keygroups)
        {
            std::sort (zones.begin(), zones.end(), [] (auto* a, auto* b) { return a->lowVelocity < b->lowVelocity; });
            if ((int) zones.size() > layerLimit)
                zones.erase (zones.begin(), zones.end() - layerLimit);

            auto* instrument = instruments->createNewChildElement ("Instrument");
            instrument->setAttribute ("number", number++);
            text (*instrument, "LowNote", juce::String (range.first));
            text (*instrument, "HighNote", juce::String (range.second));
            text (*instrument, "IgnoreBaseNote", "False");
            text (*instrument, "VelocitySensitivity", f6 (inst.numVelocityLayers > 1 ? 0.35 : 1.0));
            text (*instrument, "VolumeAttack", f6 (envTime (0.0)));
            text (*instrument, "VolumeHold", f6 (envTime (0.0)));
            text (*instrument, "VolumeDecay", f6 (envTime (0.0022861319686154)));
            text (*instrument, "VolumeSustain", f6 (1.0));
            text (*instrument, "VolumeRelease", f6 (envTime (release)));
            text (*instrument, "VolumeAttackCurve", f6 (0.5));
            text (*instrument, "VolumeDecayCurve", f6 (0.5));
            text (*instrument, "VolumeReleaseCurve", f6 (0.5));
            text (*instrument, "ZonePlay", "1");                         // velocity
            text (*instrument, "TriggerMode", inst.oneShot ? "0" : "2"); // one shot / note on

            auto* lfo = instrument->createNewChildElement ("LFO");
            lfo->setAttribute ("LfoNum", "0");
            text (*lfo, "Type", "Sine");
            text (*lfo, "Rate", "0.700000");
            text (*lfo, "LfoPitch", "0.044000");

            auto* layers = instrument->createNewChildElement ("Layers");
            int layerNumber = 1;
            for (auto* z : zones)
            {
                auto* layer = layers->createNewChildElement ("Layer");
                layer->setAttribute ("number", layerNumber++);
                text (*layer, "Active", "True");
                text (*layer, "Volume", juce::String (mpcGain (z->gainDb), 6, false));
                text (*layer, "Pan", "0.500000");
                text (*layer, "Pitch", "0.0");
                text (*layer, "TuneCoarse", "0");
                text (*layer, "TuneFine", "0");
                text (*layer, "VelStart", juce::String (z->lowVelocity));
                text (*layer, "VelEnd", juce::String (z->highVelocity));
                text (*layer, "SampleStart", "0");
                text (*layer, "SampleEnd", "0");
                text (*layer, "LoopStart", "0");
                text (*layer, "LoopEnd", "0");
                text (*layer, "LoopCrossfadeLength", "0");
                text (*layer, "LoopTune", "0");
                text (*layer, "RootNote", juce::String (z->rootNote + 1));
                text (*layer, "KeyTrack", "True");
                text (*layer, "SampleName", z->getNameWithoutExtension());
                text (*layer, "PitchRandom", "0.000000");
                text (*layer, "VolumeRandom", "0.000000");
                text (*layer, "PanRandom", "0.000000");
                text (*layer, "OffsetRandom", "0.000000");
                text (*layer, "SampleFile", "");
                text (*layer, "SliceIndex", "129");
                text (*layer, "Direction", "0");
                text (*layer, "Offset", "0");
                text (*layer, "SliceStart", "0");

                if (z->loop.enabled && ! z->oneShot)
                {
                    text (*layer, "SliceLoopStart", juce::String (z->loop.start));
                    text (*layer, "SliceEnd", juce::String (z->loop.end));
                    text (*layer, "SliceLoop", "1");
                    text (*layer, "SliceLoopCrossFadeLength", juce::String (z->loop.crossfadeFrames));
                    text (*layer, "SliceTailPosition", "0.500000");
                    text (*layer, "SliceTailLength", "0.000000");
                }
                else
                {
                    text (*layer, "SliceEnd", juce::String (z->numFrames));
                    text (*layer, "SliceLoop", "0");
                }
            }
        }

        text (*program, "KeygroupNumKeygroups", juce::String (number - 1));

        if (number - 1 > 128 && options.log)
            options.log ("MPC: más de 128 keygroups, la MPC podría no cargar el programa"_u8);

        const auto file = folder.getChildFile (safe + ".xpm");
        juce::XmlElement::TextFormat format;
        format.customEncoding = "UTF-8";
        format.lineWrapLength = 100000;
        if (! root.writeTo (file, format))
            return ExportResult::failure ("No se pudo escribir " + file.getFullPathName());

        return ExportResult::success (file);
    }
}
